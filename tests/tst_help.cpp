#include "../src/HelpDialog.h"

#include <QRegularExpression>
#include <QTest>

class TestHelp : public QObject
{
    Q_OBJECT
    static QStringList titles(const QString &query)
    {
        const QList<HelpTopic> all = HelpDialog::topics();
        QStringList out;
        for (int i : HelpDialog::search(all, query))
            out << all[i].title;
        return out;
    }
private Q_SLOTS:
    void emptyQueryListsAll() { QCOMPARE(titles(QString()).size(), HelpDialog::topics().size()); }
    void keyword() { QVERIFY(titles("clangd").contains("Language servers (LSP)")); }
    void allTermsMustMatch()
    {
        const QStringList r = titles("split vertical");
        QVERIFY(r.contains("Split view"));
        QVERIFY(!r.contains("Text encoding"));
        QVERIFY(titles("split zzzz").isEmpty());
    }
    void caseAndAccentInsensitive() { QVERIFY(titles("CODIFICACAO").contains("Text encoding")); }
    void noUnfilledPlaceholders()
    {
        static const QRegularExpression placeholder(QStringLiteral("%\\d"));
        for (const HelpTopic &t : HelpDialog::topics())
            QVERIFY2(!placeholder.match(t.html).hasMatch(), qPrintable(t.title));
    }
    void shortcutsListMainKeys()
    {
        QString html;
        for (const HelpTopic &t : HelpDialog::topics())
            if (t.title == QLatin1String("Keyboard shortcuts"))
                html = t.html;
        for (const char *k : {"Ctrl+P", "Ctrl+Shift+F", "Ctrl+Shift+O", "F4", "Ctrl+Shift+B", "Ctrl+F5", "F9", "Ctrl+`"})
            QVERIFY2(html.contains(QLatin1String(k)), k);
    }
    void searchesBodyText() { QVERIFY(titles("slayer_out").contains("Running code")); }
};

QTEST_APPLESS_MAIN(TestHelp)
#include "tst_help.moc"
