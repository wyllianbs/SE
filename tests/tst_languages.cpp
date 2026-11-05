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
    void specialFileNames()
    {
        QCOMPARE(Language::forFileName(QStringLiteral("/x/CMakeLists.txt")).id, QStringLiteral("cmake"));
        QCOMPARE(Language::forFileName(QStringLiteral("Makefile")).id, QStringLiteral("makefile"));
        QCOMPARE(Language::forFileName(QStringLiteral("rules.mk")).id, QStringLiteral("makefile"));
        QCOMPARE(Language::forFileName(QStringLiteral("notes.txt")).id, QStringLiteral("text"));
    }
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
            // Linguagens só de realce (HTML, CSS, JSON, Markdown…): sem ferramenta,
            // sempre disponíveis.
            bool expected = true;
            if (!tool.isEmpty()) {
                expected = !QStandardPaths::findExecutable(tool).isEmpty();
                for (const QString &r : lang.requiredTools)
                    expected = expected && !QStandardPaths::findExecutable(r).isEmpty();
            }
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
            if (!Language::byId(id).runCommand.isEmpty()) // só as executáveis rodam um hello-world
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
        // Pula se o compilador/interpretador não está instalado ou sem toolchain.
        if (p.exitCode() != 0) {
            const bool indisponivel = out.contains("command not found", Qt::CaseInsensitive)
                                  || out.contains("not found", Qt::CaseInsensitive)
                                  || out.contains("not installed", Qt::CaseInsensitive)
                                  || out.contains("no default is configured", Qt::CaseInsensitive)
                                  || out.contains("could not choose", Qt::CaseInsensitive);
            if (indisponivel)
                QSKIP(qPrintable(id + ": " + out.trimmed()));
        }
        QVERIFY2(p.exitCode() == 0 && out.contains("Hello, World!"), qPrintable(id + ": " + out));
    }

    // Programas repartidos em vários arquivos: o F5 deve compilar/rodar o
    // conjunto (C/C++ ligam o módulo irmão; as demais resolvem import/require/
    // use/mod a partir da pasta do arquivo de entrada).
    void multiFileRuns_data()
    {
        QTest::addColumn<QString>("id");
        for (const char *id : {"c", "cpp", "python", "javascript", "rust", "perl", "ruby"})
            QTest::newRow(id) << QString::fromLatin1(id);
    }
    void multiFileRuns()
    {
        QFETCH(QString, id);
        // Cada caso: arquivos a criar (nome → conteúdo) e o arquivo de entrada.
        // Todos imprimem "R=21" (triple(7)) a partir de uma função num outro arquivo.
        QList<QPair<QString, QString>> files;
        QString entry;
        if (id == "c") {
            entry = "main.c";
            files = {{"main.c", "#include <stdio.h>\n#include \"mod.h\"\nint main(void){ printf(\"R=%d\\n\", triple(7)); return 0; }\n"},
                     {"mod.h", "int triple(int);\n"},
                     {"mod.c", "int triple(int x){ return x * 3; }\n"}};
        } else if (id == "cpp") {
            entry = "main.cpp";
            files = {{"main.cpp", "#include <iostream>\n#include \"mod.hpp\"\nint main(){ std::cout << \"R=\" << triple(7) << \"\\n\"; }\n"},
                     {"mod.hpp", "int triple(int);\n"},
                     {"mod.cpp", "int triple(int x){ return x * 3; }\n"}};
        } else if (id == "python") {
            entry = "main.py";
            files = {{"main.py", "from helper import triple\nprint(\"R=%d\" % triple(7))\n"},
                     {"helper.py", "def triple(x):\n    return x * 3\n"}};
        } else if (id == "javascript") {
            entry = "main.js";
            files = {{"main.js", "const { triple } = require('./helper');\nconsole.log('R=' + triple(7));\n"},
                     {"helper.js", "module.exports.triple = (x) => x * 3;\n"}};
        } else if (id == "rust") {
            entry = "main.rs";
            files = {{"main.rs", "mod helper;\nfn main(){ println!(\"R={}\", helper::triple(7)); }\n"},
                     {"helper.rs", "pub fn triple(x: i32) -> i32 { x * 3 }\n"}};
        } else if (id == "perl") {
            entry = "main.pl";
            files = {{"main.pl", "use strict; use warnings;\nrequire './helper.pl';\nprint 'R=', triple(7), \"\\n\";\n"},
                     {"helper.pl", "sub triple { return $_[0] * 3; }\n1;\n"}};
        } else if (id == "ruby") {
            entry = "main.rb";
            files = {{"main.rb", "require_relative 'helper'\nputs \"R=#{triple(7)}\"\n"},
                     {"helper.rb", "def triple(x)\n  x * 3\nend\n"}};
        }
        QVERIFY(!entry.isEmpty());

        QTemporaryDir dir;
        for (const auto &pair : files) {
            QFile f(dir.filePath(pair.first));
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(pair.second.toUtf8());
            f.close();
        }
        const Language &lang = Language::byId(id);
        const LanguageConfig cfg = AppSettings::defaults().languageConfig(id);
        QProcess p;
        p.setWorkingDirectory(dir.path());
        p.setProcessChannelMode(QProcess::MergedChannels);
        p.start("bash", {"-c", lang.commandFor(dir.filePath(entry), cfg.tool, cfg.flags)});
        QVERIFY(p.waitForFinished(60000));
        const QString out = QString::fromUtf8(p.readAll());
        if (p.exitCode() != 0) {
            const bool unavailable = out.contains("command not found", Qt::CaseInsensitive)
                                  || out.contains("not found", Qt::CaseInsensitive)
                                  || out.contains("not installed", Qt::CaseInsensitive);
            if (unavailable)
                QSKIP(qPrintable(id + ": " + out.trimmed()));
        }
        QVERIFY2(p.exitCode() == 0 && out.contains("R=21"), qPrintable(id + ": " + out));
    }

    // %F seleciona o arquivo atual e os módulos irmãos sem main(), mas não
    // outros programas (com main) nem os headers. Determinístico, sem compilar.
    void compilesSiblingModulesNotOtherMains()
    {
        QTemporaryDir dir;
        auto wr = [&](const QString &n, const QString &c) {
            QFile f(dir.filePath(n));
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(c.toUtf8());
            f.close();
        };
        wr("main.c", "#include \"mod.h\"\nint main(void){ return triple(1); }\n");
        wr("mod.c", "int triple(int x){ return x * 3; }\n");  // módulo: sem main()
        wr("mod.h", "int triple(int);\n");
        wr("other.c", "int main(void){ return 0; }\n");       // outro programa: tem main()
        const QString cmd = Language::byId(QStringLiteral("c"))
                                .commandFor(dir.filePath(QStringLiteral("main.c")), QStringLiteral("gcc"), QString());
        QVERIFY2(cmd.contains(QLatin1String("'main.c'")), qPrintable(cmd));
        QVERIFY2(cmd.contains(QLatin1String("'mod.c'")), qPrintable(cmd));
        QVERIFY2(!cmd.contains(QLatin1String("other.c")), qPrintable(cmd));
        QVERIFY2(!cmd.contains(QLatin1String("mod.h")), qPrintable(cmd));
    }

    void effectiveSymbolsOrderingAndFilter()
    {
        AppSettings s;
        s.symbols = {{"<=", "≤", true}, {"<=>", "⇔", true}, {"->", "→", false},
                     {"", "x", true}, {"&&", "", true}};
        const QList<SymbolRule> eff = s.effectiveSymbols();
        // Desligados e vazios saem; mais longo primeiro.
        QCOMPARE(eff.size(), 2);
        QCOMPARE(eff.first().sequence, QStringLiteral("<=>"));
        QCOMPARE(eff.last().sequence, QStringLiteral("<="));
        // Vazio usa o conjunto padrão.
        AppSettings d;
        QVERIFY(d.effectiveSymbols().size() >= 10);
        QVERIFY(AppSettings::defaults().prettySymbols == false); // padrão: desligado
    }

    // Linguagem personalizada marcada como "compilada": gera o binário em
    // /tmp/slayer_out e o executa; a interpretada só passa o arquivo à ferramenta.
    void customCompiledBuildsAndRuns()
    {
        const QString c = Language::custom(QStringLiteral("X"), {QStringLiteral("c")}, true)
                              .commandFor(QStringLiteral("/d/main.c"), QStringLiteral("gcc"), QString());
        QVERIFY2(c.contains(QLatin1String("mkdir -p /tmp/slayer_out")), qPrintable(c));
        QVERIFY2(c.contains(QLatin1String("&& /tmp/slayer_out/")), qPrintable(c)); // executa o binário
        const QString i = Language::custom(QStringLiteral("X"), {QStringLiteral("c")}, false)
                              .commandFor(QStringLiteral("/d/main.c"), QStringLiteral("gcc"), QString());
        QVERIFY2(!i.contains(QLatin1String("slayer_out")), qPrintable(i));

        if (QStandardPaths::findExecutable(QStringLiteral("gcc")).isEmpty())
            return; // integração só com gcc instalado
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("hello.c"));
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("#include <stdio.h>\nint main(){ printf(\"R=42\\n\"); return 0; }\n");
        f.close();
        const Language lang = Language::custom(QStringLiteral("CC"), {QStringLiteral("c")}, true);
        QProcess p;
        p.setWorkingDirectory(dir.path());
        p.setProcessChannelMode(QProcess::MergedChannels);
        p.start(QStringLiteral("bash"), {QStringLiteral("-c"), lang.commandFor(path, QStringLiteral("gcc"), QString())});
        QVERIFY(p.waitForFinished(60000));
        const QString out = QString::fromUtf8(p.readAll());
        QVERIFY2(p.exitCode() == 0 && out.contains(QStringLiteral("R=42")), qPrintable(out));
    }

    // "Executar após compilar" (runAfterBuild) liga/desliga o passo de execução
    // das linguagens compiladas (embutidas e personalizadas).
    void runAfterBuildTogglesExecution()
    {
        const Language c = Language::byId(QStringLiteral("c"));
        QVERIFY(c.isCompiled());
        QVERIFY(!Language::byId(QStringLiteral("python")).isCompiled());
        // Com execução: aparece o passo final de rodar o binário.
        const QString run = c.commandFor(QStringLiteral("/d/main.c"), QStringLiteral("gcc"), QString(), false, true);
        QCOMPARE(run.count(QLatin1String("/tmp/slayer_out")), 3); // mkdir, -o e execução
        // Sem execução: só compila (some o " && /tmp/slayer_out/%b").
        const QString no = c.commandFor(QStringLiteral("/d/main.c"), QStringLiteral("gcc"), QString(), false, false);
        QCOMPARE(no.count(QLatin1String("/tmp/slayer_out")), 2); // mkdir e -o apenas
        QVERIFY2(no.contains(QLatin1String("-o /tmp/slayer_out/")), qPrintable(no));
        // Vale também para personalizada compilada.
        const Language x = Language::custom(QStringLiteral("X"), {QStringLiteral("x")}, true);
        QCOMPARE(x.commandFor(QStringLiteral("/d/a.x"), QStringLiteral("cc"), QString(), false, false)
                     .count(QLatin1String("/tmp/slayer_out")), 2);
    }

    // Nome exibido personalizável em qualquer linguagem embutida: vazio usa o
    // nome do catálogo; a personalização sobrevive ao round-trip do JSON.
    void languageNameOverride()
    {
        AppSettings s = AppSettings::defaults();
        const QString def = Language::byId(QStringLiteral("c")).displayName;
        QCOMPARE(s.languageDisplayName(QStringLiteral("c")), def); // sem override: catálogo

        LanguageConfig cfg = s.languageConfig(QStringLiteral("c"));
        cfg.displayName = QStringLiteral("LLVM");
        s.languages.insert(QStringLiteral("c"), cfg);
        QCOMPARE(s.languageDisplayName(QStringLiteral("c")), QStringLiteral("LLVM"));

        const AppSettings r = AppSettings::fromJson(s.toJson());
        QCOMPARE(r.languageDisplayName(QStringLiteral("c")), QStringLiteral("LLVM"));
        QCOMPARE(r.languageDisplayName(QStringLiteral("python")),
                 Language::byId(QStringLiteral("python")).displayName); // outras intactas
    }

    // Badge personalizável: "glyph:<chave>" numa embutida e caminho de imagem
    // numa personalizada sobrevivem ao round-trip do JSON.
    void languageBadgeOverride()
    {
        AppSettings s = AppSettings::defaults();
        QCOMPARE(s.languageIconSpec(QStringLiteral("c")), QString()); // sem override

        LanguageConfig cfg = s.languageConfig(QStringLiteral("c"));
        cfg.icon = QStringLiteral("glyph:zig");
        s.languages.insert(QStringLiteral("c"), cfg);
        s.customLanguages.append({QStringLiteral("LLVM"), QStringLiteral("clang"), QStringLiteral("-O3"),
                                  QStringLiteral("ll"), true, false, QStringLiteral("/home/u/llvm.svg")});

        const AppSettings r = AppSettings::fromJson(s.toJson());
        QCOMPARE(r.languageIconSpec(QStringLiteral("c")), QStringLiteral("glyph:zig"));
        QCOMPARE(r.languageIconSpec(QStringLiteral("python")), QString()); // outras intactas
        QCOMPARE(r.customLanguages.size(), 1);
        QCOMPARE(r.customLanguages.first().icon, QStringLiteral("/home/u/llvm.svg"));
        QVERIFY(r.customLanguages.first().compiled);
        QVERIFY(!r.customLanguages.first().runAfterBuild);
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
