#include "mihomo_page.h"

#include <QLabel>
#include <QVBoxLayout>

namespace sparkle::ui {

MihomoPage::MihomoPage(QWidget* parent) : PageBase(parent) {
  auto* layout = new QVBoxLayout(this);
  layout->addWidget(new QLabel(QStringLiteral("内核"), this));
  layout->addWidget(new QLabel(QStringLiteral("（骨架占位：版本/升级/TUN/mixed-port P1 接入）"), this));
  layout->addStretch();
}

void MihomoPage::refresh() { /* TODO(phase 1) */ }

}  // namespace sparkle::ui