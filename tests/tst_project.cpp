#include "../src/Project.h"

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <sys/stat.h>

class TestProject : public QObject
{
    Q_OBJECT

    static void touch(const QString &path)
    {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
    }

private Q_SLOTS:
    void gitignoreSemantics()
    {
        IgnoreRules r;
        r.addPatterns({QStringLiteral("# comentário"), QStringLiteral("*.o"), QStringLiteral("build/"),
                       QStringLiteral("/dist"), QStringLiteral("docs/*.tmp"), QStringLiteral("**/gen/**"),
                       QStringLiteral("*.log"), QStringLiteral("!keep.log")});
        QVERIFY(r.matches(QStringLiteral("a.o"), false));
        QVERIFY(r.matches(QStringLiteral("src/deep/a.o"), false));
        QVERIFY(r.matches(QStringLiteral("build"), true));
        QVERIFY(r.matches(QStringLiteral("src/build"), true));
        QVERIFY(!r.matches(QStringLiteral("build"), false)); // "build/" só casa pastas
        QVERIFY(r.matches(QStringLiteral("dist"), true));
        QVERIFY(!r.matches(QStringLiteral("src/dist"), true)); // ancorado na raiz
        QVERIFY(r.matches(QStringLiteral("docs/a.tmp"), false));
        QVERIFY(!r.matches(QStringLiteral("other/a.tmp"), false));
        QVERIFY(r.matches(QStringLiteral("x/gen/y.c"), false));
        QVERIFY(r.matches(QStringLiteral("err.log"), false));
        QVERIFY(!r.matches(QStringLiteral("keep.log"), false)); // negação
        QVERIFY(!r.matches(QStringLiteral("main.c"), false));
    }

    void listingPrunesAndHonorsGitignore()
    {
        QTemporaryDir tmp;
        const QString root = tmp.path();
        touch(root + "/main.c");
        touch(root + "/src/util.c");
        touch(root + "/.git/config");
        touch(root + "/node_modules/x/index.js");
        touch(root + "/build/out.bin");
        touch(root + "/app.o");
        QFile gi(root + "/.gitignore");
        QVERIFY(gi.open(QIODevice::WriteOnly));
        gi.write("build/\n");
        gi.close();

        Project p(root, {".git/", "node_modules/", "*.o"});
        QSignalSpy spy(&p, &Project::filesListed);
        p.refreshFiles();
        QVERIFY(spy.wait(5000));
        QCOMPARE(p.files(), (QStringList{".gitignore", "main.c", "src/util.c"}));
        QVERIFY(!p.filesTruncated());

        // Árvore: esconde as exclusões, mas mostra o que só o .gitignore ignora.
        QVERIFY(p.isHidden(root + "/.git", true));
        QVERIFY(p.isHidden(root + "/app.o", false));
        QVERIFY(!p.isHidden(root + "/build", true));
        QVERIFY(p.isIgnored("build", true));
        QVERIFY(p.contains(root + "/src/util.c"));
        QVERIFY(!p.contains(root + "-other/x.c"));
    }

    void fuzzy()
    {
        QVERIFY(Project::fuzzyScore("mwc", "src/MainWindow.cpp") > 0);
        QCOMPARE(Project::fuzzyScore("xyz", "src/MainWindow.cpp"), -1);
        // Nome do arquivo vale mais que pastas; caminho curto desempata.
        QVERIFY(Project::fuzzyScore("main", "src/main.cpp") > Project::fuzzyScore("main", "maintenance/x/a.cpp"));
        QVERIFY(Project::fuzzyScore("doc", "src/Document.h") > Project::fuzzyScore("doc", "tests/data/old/c.h"));
    }

    void searchFiles()
    {
        QTemporaryDir tmp;
        QFile a(tmp.path() + "/a.c");
        QVERIFY(a.open(QIODevice::WriteOnly));
        a.write("int foo;\r\nfoobar();\nFOO = 1;\n");
        a.close();
        QFile bin(tmp.path() + "/b.bin");
        QVERIFY(bin.open(QIODevice::WriteOnly));
        bin.write(QByteArray("foo\0foo", 7));
        bin.close();
        const QStringList files{"a.c", "b.bin"};
        auto m = Project::search(tmp.path(), files, "foo", {}, 100);
        QCOMPARE(m.size(), 3);
        QCOMPARE(m[1].line, 1);
        QCOMPARE(m[0].text, QStringLiteral("int foo;")); // sem o \r
        Project::SearchOptions o;
        o.caseSensitive = true;
        o.wholeWord = true;
        m = Project::search(tmp.path(), files, "foo", o, 100);
        QCOMPARE(m.size(), 1);
        QCOMPARE(m[0].column, 4);
        o = {};
        o.regex = true;
        m = Project::search(tmp.path(), files, "fo+bar", o, 100);
        QCOMPARE(m.size(), 1);
        bool truncated = false;
        m = Project::search(tmp.path(), files, "foo", {}, 2, &truncated);
        QVERIFY(truncated);
    }

    void detectBuild()
    {
        QTemporaryDir tmp;
        QCOMPARE(Project::detectBuild(tmp.path()).system, QString());
        QFile cm(tmp.path() + "/CMakeLists.txt");
        QVERIFY(cm.open(QIODevice::WriteOnly));
        cm.write("project(x C)\n# add_executable(old a.c)\nadd_executable( app app/main.c)\n");
        cm.close();
        auto c = Project::detectBuild(tmp.path());
        QCOMPARE(c.system, QStringLiteral("CMake"));
        QVERIFY(c.build.startsWith("cmake -S . -B build"));
        QCOMPARE(c.run, QStringLiteral("./build/app"));
        QFile::remove(tmp.path() + "/CMakeLists.txt");

        QFile mk(tmp.path() + "/Makefile");
        QVERIFY(mk.open(QIODevice::WriteOnly));
        mk.write("all:\n\tcc a.c\nrun: all\n\t./a.out\n");
        mk.close();
        c = Project::detectBuild(tmp.path());
        QCOMPARE(c.system, QStringLiteral("Make"));
        QCOMPARE(c.run, QStringLiteral("make run"));
        QFile::remove(tmp.path() + "/Makefile");

        touch(tmp.path() + "/Cargo.toml");
        QCOMPARE(Project::detectBuild(tmp.path()).run, QStringLiteral("cargo run"));
    }

    void skipsSpecialFiles()
    {
        // Um FIFO no projeto travava a busca para sempre (open() espera um escritor).
        QTemporaryDir tmp;
        touch(tmp.path() + "/a.c");
        QVERIFY(::mkfifo(QFile::encodeName(tmp.path() + "/pipe").constData(), 0600) == 0);
        const QStringList files = Project::listFiles(tmp.path(), IgnoreRules(), IgnoreRules(), 100);
        QCOMPARE(files, QStringList{"a.c"});
    }

    void listingIsTruncated()
    {
        QTemporaryDir tmp;
        for (int i = 0; i < 5; ++i)
            touch(tmp.path() + QStringLiteral("/f%1.txt").arg(i));
        bool truncated = false;
        const QStringList files = Project::listFiles(tmp.path(), IgnoreRules(), IgnoreRules(), 3, &truncated);
        QCOMPARE(files.size(), 3);
        QVERIFY(truncated);
    }
};

QTEST_GUILESS_MAIN(TestProject)
#include "tst_project.moc"
