#include "MITMCertificateAuthority.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileDevice>
#include <QFileInfo>
#include <QHostAddress>
#include <QRandomGenerator>
#include <QSaveFile>

#if defined(SPARKLE_HAS_OPENSSL)
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>
#endif

#include <memory>

namespace sparkle::core {

struct MITMCertificateAuthority::Impl {
  QString directory;
  QString caCertificatePath;
  QSslCertificate caCertificate;
  QSslKey caKey;
  bool ready = false;
};

#if defined(SPARKLE_HAS_OPENSSL)
namespace {

struct EvpKeyDeleter {
  void operator()(EVP_PKEY* value) const noexcept { EVP_PKEY_free(value); }
};
struct X509Deleter {
  void operator()(X509* value) const noexcept { X509_free(value); }
};
struct BioDeleter {
  void operator()(BIO* value) const noexcept { BIO_free(value); }
};
using EvpKeyPtr = std::unique_ptr<EVP_PKEY, EvpKeyDeleter>;
using X509Ptr = std::unique_ptr<X509, X509Deleter>;
using BioPtr = std::unique_ptr<BIO, BioDeleter>;

QString opensslError() {
  const unsigned long code = ERR_get_error();
  if (!code) return QStringLiteral("OpenSSL error");
  char buffer[256] = {};
  ERR_error_string_n(code, buffer, sizeof(buffer));
  return QString::fromLatin1(buffer);
}

EvpKeyPtr generateKey(QString* error) {
  EvpKeyPtr key(EVP_PKEY_new());
  RSA* rsa = RSA_new();
  BIGNUM* exponent = BN_new();
  if (!key || !rsa || !exponent || !BN_set_word(exponent, RSA_F4) ||
      !RSA_generate_key_ex(rsa, 2048, exponent, nullptr) ||
      !EVP_PKEY_assign_RSA(key.get(), rsa)) {
    if (rsa) RSA_free(rsa);
    BN_free(exponent);
    if (error) *error = opensslError();
    return nullptr;
  }
  // EVP_PKEY_assign_RSA transfers ownership of rsa to key.
  BN_free(exponent);
  return key;
}

bool addExtension(X509* certificate, int nid, const char* value, X509* issuer,
                  QString* error) {
  X509V3_CTX context;
  X509V3_set_ctx_nodb(&context);
  X509V3_set_ctx(&context, issuer, certificate, nullptr, nullptr, 0);
  X509_EXTENSION* extension = X509V3_EXT_conf_nid(
      nullptr, &context, nid, const_cast<char*>(value));
  if (!extension || !X509_add_ext(certificate, extension, -1)) {
    if (extension) X509_EXTENSION_free(extension);
    if (error) *error = opensslError();
    return false;
  }
  X509_EXTENSION_free(extension);
  return true;
}

bool savePem(const QString& path, bool privateKey, EVP_PKEY* key, X509* certificate,
             QString* error) {
  QSaveFile file(path);
  if (!file.open(QIODevice::WriteOnly)) {
    if (error) *error = file.errorString();
    return false;
  }
  BioPtr bio(BIO_new(BIO_s_mem()));
  const bool written = bio &&
      (privateKey ? PEM_write_bio_PrivateKey(bio.get(), key, nullptr, nullptr, 0, nullptr, nullptr)
                  : PEM_write_bio_X509(bio.get(), certificate));
  if (!written) {
    if (error) *error = opensslError();
    return false;
  }
  BUF_MEM* memory = nullptr;
  BIO_get_mem_ptr(bio.get(), &memory);
  if (!memory || file.write(memory->data, static_cast<qint64>(memory->length)) !=
                    static_cast<qint64>(memory->length) || !file.commit()) {
    if (error) *error = file.errorString();
    return false;
  }
  return true;
}

bool readPem(const QString& certPath, const QString& keyPath, QSslCertificate* certificate,
             QSslKey* key, QString* error) {
  QFile certFile(certPath);
  QFile keyFile(keyPath);
  if (!certFile.open(QIODevice::ReadOnly) || !keyFile.open(QIODevice::ReadOnly)) {
    if (error) *error = QStringLiteral("cannot read persisted MITM CA files");
    return false;
  }
  *certificate = QSslCertificate(certFile.readAll(), QSsl::Pem);
  *key = QSslKey(keyFile.readAll(), QSsl::Rsa, QSsl::Pem);
  if (certificate->isNull() || key->isNull()) {
    if (error) *error = QStringLiteral("persisted MITM CA is invalid");
    return false;
  }
  return true;
}

bool makeCa(const QString& certPath, const QString& keyPath, QSslCertificate* certificate,
            QSslKey* key, QString* error) {
  EvpKeyPtr caKey = generateKey(error);
  X509Ptr ca(X509_new());
  if (!caKey || !ca || !X509_set_version(ca.get(), 2) ||
      !ASN1_INTEGER_set(X509_get_serialNumber(ca.get()), 1) ||
      !X509_gmtime_adj(X509_get_notBefore(ca.get()), 0) ||
      !X509_gmtime_adj(X509_get_notAfter(ca.get()), 60L * 60L * 24L * 3650L) ||
      !X509_set_pubkey(ca.get(), caKey.get())) {
    if (error && error->isEmpty()) *error = opensslError();
    return false;
  }

  X509_NAME* name = X509_get_subject_name(ca.get());
  X509_NAME_add_entry_by_txt(name, "C", MBSTRING_ASC,
                             reinterpret_cast<const unsigned char*>("XX"), -1, -1, 0);
  X509_NAME_add_entry_by_txt(name, "O", MBSTRING_ASC,
                             reinterpret_cast<const unsigned char*>("Sparkle"), -1, -1, 0);
  X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
                             reinterpret_cast<const unsigned char*>("Sparkle MITM Local CA"),
                             -1, -1, 0);
  if (!X509_set_issuer_name(ca.get(), name) ||
      !addExtension(ca.get(), NID_basic_constraints, "critical,CA:TRUE,pathlen:1", ca.get(),
                   error) ||
      !addExtension(ca.get(), NID_key_usage, "critical,keyCertSign,cRLSign", ca.get(), error) ||
      !X509_sign(ca.get(), caKey.get(), EVP_sha256()) ||
      !savePem(keyPath, true, caKey.get(), nullptr, error) ||
      !savePem(certPath, false, nullptr, ca.get(), error)) {
    if (error && error->isEmpty()) *error = opensslError();
    return false;
  }

  QFile::setPermissions(keyPath, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
  QFile certFile(certPath);
  QFile keyFile(keyPath);
  if (!certFile.open(QIODevice::ReadOnly) || !keyFile.open(QIODevice::ReadOnly)) {
    if (error) *error = QStringLiteral("cannot reopen generated MITM CA");
    return false;
  }
  *certificate = QSslCertificate(certFile.readAll(), QSsl::Pem);
  *key = QSslKey(keyFile.readAll(), QSsl::Rsa, QSsl::Pem);
  return !certificate->isNull() && !key->isNull();
}

bool makeLeaf(const QString& hostname, const QString& certPath, const QString& keyPath,
              const QSslCertificate& caCertificate, const QSslKey& caKey,
              QSslCertificate* certificate, QSslKey* key, QString* error) {
  const QByteArray caCertPem = caCertificate.toPem();
  const QByteArray caKeyPem = caKey.toPem();
  BioPtr certBio(BIO_new_mem_buf(caCertPem.constData(), caCertPem.size()));
  BioPtr keyBio(BIO_new_mem_buf(caKeyPem.constData(), caKeyPem.size()));
  X509Ptr ca(certBio ? PEM_read_bio_X509(certBio.get(), nullptr, nullptr, nullptr) : nullptr);
  EvpKeyPtr issuerKey(keyBio ? PEM_read_bio_PrivateKey(keyBio.get(), nullptr, nullptr, nullptr)
                              : nullptr);
  EvpKeyPtr leafKey = generateKey(error);
  X509Ptr leaf(X509_new());
  if (!ca || !issuerKey || !leafKey || !leaf || !X509_set_version(leaf.get(), 2) ||
      !ASN1_INTEGER_set(X509_get_serialNumber(leaf.get()),
                        static_cast<long>(QRandomGenerator::global()->generate() & 0x7fffffff)) ||
      !X509_gmtime_adj(X509_get_notBefore(leaf.get()), -300) ||
      !X509_gmtime_adj(X509_get_notAfter(leaf.get()), 60L * 60L * 24L * 825L) ||
      !X509_set_pubkey(leaf.get(), leafKey.get())) {
    if (error && error->isEmpty()) *error = opensslError();
    return false;
  }

  const QByteArray hostBytes = hostname.toUtf8();
  X509_NAME* subject = X509_get_subject_name(leaf.get());
  X509_NAME_add_entry_by_txt(subject, "O", MBSTRING_ASC,
                             reinterpret_cast<const unsigned char*>("Sparkle MITM"), -1, -1, 0);
  X509_NAME_add_entry_by_txt(subject, "CN", MBSTRING_ASC,
                             reinterpret_cast<const unsigned char*>(hostBytes.constData()), -1, -1,
                             0);
  if (!X509_set_issuer_name(leaf.get(), X509_get_subject_name(ca.get())) ||
      !addExtension(leaf.get(), NID_basic_constraints, "critical,CA:FALSE", ca.get(), error) ||
      !addExtension(leaf.get(), NID_key_usage, "critical,digitalSignature,keyEncipherment", ca.get(),
                   error) ||
      !addExtension(leaf.get(), NID_ext_key_usage, "serverAuth", ca.get(), error)) {
    if (error && error->isEmpty()) *error = opensslError();
    return false;
  }

  const QHostAddress address(hostname);
  const QByteArray san = address.isNull() ? QByteArrayLiteral("DNS:") + hostBytes
                                          : QByteArrayLiteral("IP:") + hostBytes;
  if (!addExtension(leaf.get(), NID_subject_alt_name, san.constData(), ca.get(), error) ||
      !X509_sign(leaf.get(), issuerKey.get(), EVP_sha256()) ||
      !savePem(keyPath, true, leafKey.get(), nullptr, error) ||
      !savePem(certPath, false, nullptr, leaf.get(), error)) {
    if (error && error->isEmpty()) *error = opensslError();
    return false;
  }
  QFile::setPermissions(keyPath, QFileDevice::ReadOwner | QFileDevice::WriteOwner);

  QFile certFile(certPath);
  QFile keyFile(keyPath);
  if (!certFile.open(QIODevice::ReadOnly) || !keyFile.open(QIODevice::ReadOnly)) {
    if (error) *error = QStringLiteral("cannot reopen generated MITM certificate");
    return false;
  }
  *certificate = QSslCertificate(certFile.readAll(), QSsl::Pem);
  *key = QSslKey(keyFile.readAll(), QSsl::Rsa, QSsl::Pem);
  return !certificate->isNull() && !key->isNull();
}

}  // namespace
#endif

MITMCertificateAuthority::MITMCertificateAuthority() : d_(std::make_unique<Impl>()) {}
MITMCertificateAuthority::~MITMCertificateAuthority() = default;

bool MITMCertificateAuthority::initialize(const QString& directory, QString* error) {
  d_->directory = QDir::cleanPath(directory);
  d_->caCertificatePath = QDir(d_->directory).filePath(QStringLiteral("ca.pem"));
  QDir().mkpath(d_->directory);
#if defined(SPARKLE_HAS_OPENSSL)
  const QString keyPath = QDir(d_->directory).filePath(QStringLiteral("ca.key"));
  if (QFileInfo::exists(d_->caCertificatePath) && QFileInfo::exists(keyPath)) {
    d_->ready = readPem(d_->caCertificatePath, keyPath, &d_->caCertificate, &d_->caKey, error);
  } else {
    d_->ready = makeCa(d_->caCertificatePath, keyPath, &d_->caCertificate, &d_->caKey, error);
  }
#else
  Q_UNUSED(error);
  d_->ready = false;
#endif
  return d_->ready;
}

bool MITMCertificateAuthority::isReady() const { return d_->ready; }
QString MITMCertificateAuthority::caCertificatePath() const { return d_->caCertificatePath; }
QSslCertificate MITMCertificateAuthority::caCertificate() const { return d_->caCertificate; }

bool MITMCertificateAuthority::certificateFor(const QString& hostname, QSslCertificate* certificate,
                                              QSslKey* privateKey, QString* error) {
  if (!d_->ready || !certificate || !privateKey || hostname.isEmpty()) {
    if (error) *error = QStringLiteral("MITM CA is not ready");
    return false;
  }
#if defined(SPARKLE_HAS_OPENSSL)
  const QByteArray hash = QCryptographicHash::hash(hostname.toLower().toUtf8(),
                                                   QCryptographicHash::Sha256).toHex();
  const QString base = QDir(d_->directory).filePath(QString::fromLatin1(hash));
  const QString certPath = base + QStringLiteral(".pem");
  const QString keyPath = base + QStringLiteral(".key");
  if (QFileInfo::exists(certPath) && QFileInfo::exists(keyPath)) {
    QFile certFile(certPath);
    QFile keyFile(keyPath);
    if (certFile.open(QIODevice::ReadOnly) && keyFile.open(QIODevice::ReadOnly)) {
      *certificate = QSslCertificate(certFile.readAll(), QSsl::Pem);
      *privateKey = QSslKey(keyFile.readAll(), QSsl::Rsa, QSsl::Pem);
      if (!certificate->isNull() && !privateKey->isNull()) return true;
    }
  }
  return makeLeaf(hostname, certPath, keyPath, d_->caCertificate, d_->caKey,
                  certificate, privateKey, error);
#else
  Q_UNUSED(hostname);
  Q_UNUSED(certificate);
  Q_UNUSED(privateKey);
  if (error) *error = QStringLiteral("OpenSSL support is not available");
  return false;
#endif
}

}  // namespace sparkle::core
