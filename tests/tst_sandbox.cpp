#include "../src/Sandbox.h"

#include <QTest>

// Força o caminho Flatpak antes de qualquer chamada (inFlatpak() faz cache).
static const bool s_forced = []() { qputenv("FLATPAK_ID", "io.github.wyllianbs.SlayerEditor"); return true; }();

class TestSandbox : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void wrapsThroughHostInFlatpak()
    {
        QVERIFY(Sandbox::inFlatpak());
        const auto [prog, args] = Sandbox::command("gcc", {"-O2", "a.c"});
        QCOMPARE(prog, QStringLiteral("flatpak-spawn"));
        QCOMPARE(args, (QStringList{"--host", "--watch-bus", "gcc", "-O2", "a.c"}));
        QCOMPARE(Sandbox::wrap("python3", {"-i"}),
                 (QStringList{"flatpak-spawn", "--host", "--watch-bus", "python3", "-i"}));
    }
};

QTEST_GUILESS_MAIN(TestSandbox)
#include "tst_sandbox.moc"
