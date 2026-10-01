#include "../src/Language.h"
#include "../src/Settings.h"

#include <QCoreApplication>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

class TestLanguages : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void everyRunnableLanguageHasTemplate()
    {
        for (const Language &lang : Language::all()) {
            if (lang.runCommand.isEmpty())
                continue;
            QVERIFY2(lang.helloWorld.contains("Hello, World!"), qPrintable(lang.id));
            QVERIFY2(!lang.newFileName.isEmpty(), qPrintable(lang.id));
            QVERIFY2(lang.extensions.contains(QFileInfo(lang.newFileName).suffix()), qPrintable(lang.id));
        }
    }

    void detectionMatchesSystem()
    {
        const AppSettings s = AppSettings::defaults();
        for (const Language &lang : Language::all()) {
            const QString tool = s.languageConfig(lang.id).tool;
            bool expected = !QStandardPaths::findExecutable(tool).isEmpty();
            for (const QString &r : lang.requires)
                expected = expected && !QStandardPaths::findExecutable(r).isEmpty();
            QCOMPARE(s.languageInstalled(lang.id), expected);
        }
        // Java só conta com o compilador (o JRE sozinho não roda arquivos .java).
        if (QStandardPaths::findExecutable("javac").isEmpty())
            QVERIFY(!s.languageInstalled("java"));
        QVERIFY(!s.installedLanguages().contains("text"));
    }

    void helloWorldRuns_data()
    {
        QTest::addColumn<QString>("id");
        const AppSettings s = AppSettings::defaults();
        for (const QString &id : s.installedLanguages())
            QTest::newRow(qPrintable(id)) << id;
    }
    void helloWorldRuns()
    {
        // O mesmo comando que o F5 manda ao terminal, rodado no bash.
        QFETCH(QString, id);
        const Language &lang = Language::byId(id);
        const LanguageConfig cfg = AppSettings::defaults().languageConfig(id);
        QTemporaryDir dir;
        const QString path = dir.filePath(lang.newFileName);
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(lang.helloWorld.toUtf8());
        f.close();
        QProcess p;
        p.setWorkingDirectory(dir.path());
        p.setProcessChannelMode(QProcess::MergedChannels);
        p.start("bash", {"-c", lang.commandFor(path, cfg.tool, cfg.flags)});
        QVERIFY(p.waitForFinished(60000));
        const QString out = QString::fromUtf8(p.readAll());
        QVERIFY2(p.exitCode() == 0 && out.contains("Hello, World!"), qPrintable(id + ": " + out));
    }

    void customLanguageTemplates()
    {
        QVERIFY(Language::helloWorldForTool("ruby").contains("puts"));
        QVERIFY(Language::helloWorldForTool("/usr/bin/python3.13").contains("print"));
        QVERIFY(Language::helloWorldForTool("tclsh").contains("Hello, World!"));
        QCOMPARE(Language::helloWorldForTool("unknowntool"), QStringLiteral("Hello, World!\n"));
    }
};

QTEST_GUILESS_MAIN(TestLanguages)
#include "tst_languages.moc"
