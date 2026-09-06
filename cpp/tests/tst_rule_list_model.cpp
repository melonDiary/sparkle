#include "models/rule_list_model.h"

#include <QtTest>

using namespace sparkle::core;
using sparkle::ui::RuleListModel;

class TstRuleListModel : public QObject {
  Q_OBJECT

private slots:
  void displaysRulesAndCounters() {
    RuleListModel model;
    model.setRules({RuleItem{4, QStringLiteral("DOMAIN"), QStringLiteral("example.com"),
                             QStringLiteral("Proxy"), 1, true, 12, 3}});

    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.data(model.index(0, 0), Qt::DisplayRole).toString(),
             QStringLiteral("DOMAIN · example.com → Proxy"));
    QVERIFY(model.data(model.index(0, 0), Qt::ToolTipRole).toString().contains(QStringLiteral("12")));
    QVERIFY(model.data(model.index(0, 0), Qt::ToolTipRole).toString().contains(QStringLiteral("3")));
    QVERIFY(model.data(model.index(0, 0), Qt::ForegroundRole).isValid());
  }

  void filtersByTypePayloadOrProxy() {
    RuleListModel model;
    model.setRules({
        RuleItem{0, QStringLiteral("DOMAIN"), QStringLiteral("example.com"),
                 QStringLiteral("Proxy"), 0, false, 0, 0},
        RuleItem{1, QStringLiteral("IP-CIDR"), QStringLiteral("10.0.0.0/8"),
                 QStringLiteral("DIRECT"), 0, false, 0, 0},
    });

    model.setFilter(QStringLiteral("example"));
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.ruleAt(model.index(0, 0))->type, QStringLiteral("DOMAIN"));

    model.setFilter(QStringLiteral("direct"));
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.ruleAt(model.index(0, 0))->type, QStringLiteral("IP-CIDR"));

    model.setFilter(QString());
    QCOMPARE(model.rowCount(), 2);
  }
};

QTEST_GUILESS_MAIN(TstRuleListModel)
#include "tst_rule_list_model.moc"
