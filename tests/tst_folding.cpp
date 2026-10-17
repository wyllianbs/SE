#include "../src/Folding.h"

#include <QTest>

class TestFolding : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void cBraces()
    {
        const QString src = QStringLiteral(
            "int main(void)\n"      // 0
            "{\n"                   // 1
            "    if (x) {\n"        // 2
            "        y();\n"        // 3
            "    }\n"               // 4
            "    return 0;\n"       // 5
            "}\n");                 // 6
        const auto r = Folding::detect(src, QStringLiteral("c"));
        QCOMPARE(r.size(), 2);
        QCOMPARE(r[0], (FoldRegion{1, 5}));
        QCOMPARE(r[1], (FoldRegion{2, 3}));
    }
    void ignoresBracesInStringsAndComments()
    {
        const QString src = QStringLiteral(
            "void f() {\n"
            "    puts(\"{\"); // {\n"
            "    /* { */ char c = '{';\n"
            "}\n");
        const auto r = Folding::detect(src, QStringLiteral("cpp"));
        QCOMPARE(r.size(), 1);
        QCOMPARE(r[0], (FoldRegion{0, 2}));
    }
    void sameLineBlockNotFoldable()
    {
        QVERIFY(Folding::detect(QStringLiteral("int f() { return 1; }\n"), QStringLiteral("c")).isEmpty());
    }
    void python()
    {
        const QString src = QStringLiteral(
            "def f(x):  # c\n"      // 0
            "    if x:\n"           // 1
            "        return 1\n"    // 2
            "\n"                    // 3
            "    return 2\n"        // 4
            "\n"
            "print(f(1))\n");
        const auto r = Folding::detect(src, QStringLiteral("python"));
        QCOMPARE(r.size(), 2);
        QCOMPARE(r[0], (FoldRegion{0, 4}));
        QCOMPARE(r[1], (FoldRegion{1, 2}));
    }
    void unsupported()
    {
        QVERIFY(!Folding::supports(QStringLiteral("text")));
        QVERIFY(Folding::detect(QStringLiteral("a {\nb\n}"), QStringLiteral("text")).isEmpty());
    }
};

QTEST_APPLESS_MAIN(TestFolding)
#include "tst_folding.moc"
