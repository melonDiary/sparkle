#pragma once

#include <QSslCertificate>
#include <QSslKey>
#include <QString>

#include <memory>

namespace sparkle::core {

// MITM 证书颁发组件。
//
// CA 私钥只保存在用户数据目录，不会自动安装到系统信任库。用户需要显式
// 将 caCertificatePath() 导入系统/浏览器信任列表后，HTTPS 客户端才会信任
// MITM 代理签发的站点证书。
class MITMCertificateAuthority final {
public:
  MITMCertificateAuthority();
  ~MITMCertificateAuthority();

  MITMCertificateAuthority(const MITMCertificateAuthority&) = delete;
  MITMCertificateAuthority& operator=(const MITMCertificateAuthority&) = delete;

  bool initialize(const QString& directory, QString* error = nullptr);
  bool isReady() const;
  QString caCertificatePath() const;
  QSslCertificate caCertificate() const;

  bool certificateFor(const QString& hostname, QSslCertificate* certificate,
                      QSslKey* privateKey, QString* error = nullptr);

private:
  struct Impl;
  std::unique_ptr<Impl> d_;
};

}  // namespace sparkle::core
