#include "../src/SettingsDialog.h"

#include <QApplication>
#include <QTest>

class TestSettings : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void dialogRoundTripKeepsLanguageSettings()
    {
        AppSettings in = AppSettings::defaults();
        in.languages.insert("rust", {"rustc", "-C opt-level=3", "rs"});       // linguagem não instalada: oculta
        in.lsp.insert("go", {false, QString()});                              // idem, LSP
        in.languages.insert("python", {"python3", "-B", "py"});               // instalada e alterada
        in.lsp.insert("python", {true, "pyright-langserver --stdio"});        // servidor próprio
        in.lsp.insert("c", {false, QString()});                               // LSP desligado
        in.customLanguages.append({"Tcl", "tclsh", "", "tcl"});

        SettingsDialog dialog(in, Theme::builtIns().first());
        const AppSettings out = dialog.settings();

        QCOMPARE(out.languages.value("rust").flags, QStringLiteral("-C opt-level=3"));
        QVERIFY(out.lsp.contains("go") && !out.lsp.value("go").enabled);
        QCOMPARE(out.languages.value("python").flags, QStringLiteral("-B"));
        QCOMPARE(out.lsp.value("python").command, QStringLiteral("pyright-langserver --stdio"));
        QVERIFY(!out.lsp.value("c").enabled);
        QCOMPARE(out.customLanguages.size(), 1);
        QCOMPARE(out.customLanguages.first().tool, QStringLiteral("tclsh"));
        // Linguagens no padrão não são gravadas (acompanham mudanças futuras dos padrões).
        QVERIFY(!out.languages.contains("cpp"));
        QVERIFY(!out.lsp.contains("cpp"));
    }

    void lspSecondaryServerRoundTrip()
    {
        // Servidor secundário (Python): pyright principal + pylsp secundário.
        AppSettings in = AppSettings::defaults();
        LspConfig cfg;
        cfg.enabled = true;
        cfg.command = "pyright-langserver --stdio";
        cfg.secondaryEnabled = true;
        cfg.secondaryCommand = "pylsp";
        cfg.secondarySettingsJson = "{\"pylsp\":{\"plugins\":{\"pyflakes\":{\"enabled\":true}}}}";
        in.lsp.insert("python", cfg);

        const AppSettings out = AppSettings::fromJson(in.toJson());
        const LspConfig r = out.lsp.value("python");
        QVERIFY(r.secondaryEnabled);
        QCOMPARE(r.command, QStringLiteral("pyright-langserver --stdio"));
        QCOMPARE(r.secondaryCommand, QStringLiteral("pylsp"));
        QCOMPARE(r.secondarySettingsJson, cfg.secondarySettingsJson);
        // Padrão: sem secundário quando nada foi configurado.
        QVERIFY(!AppSettings::defaults().lspConfig("python").secondaryEnabled);
    }

    void dialogPythonSecondaryServer()
    {
        // Caminho da UI: Python com servidor secundário (pyright principal + pylsp).
        AppSettings in = AppSettings::defaults();
        LspConfig cfg;
        cfg.enabled = true;
        cfg.command = "pyright-langserver --stdio";
        cfg.secondaryEnabled = true;
        cfg.secondaryCommand = "pylsp";
        cfg.secondarySettingsJson = "{\"pylsp\":{}}";
        in.lsp.insert("python", cfg);

        SettingsDialog dialog(in, Theme::builtIns().first());
        dialog.showLanguageServer("python"); // seleciona Python e carrega o formulário
        const AppSettings out = dialog.settings();
        const LspConfig r = out.lsp.value("python");
        QVERIFY(r.secondaryEnabled);
        QCOMPARE(r.command, QStringLiteral("pyright-langserver --stdio"));
        QCOMPARE(r.secondaryCommand, QStringLiteral("pylsp"));
        QCOMPARE(r.secondarySettingsJson, QStringLiteral("{\"pylsp\":{}}"));
    }

    void symbolsRoundTrip()
    {
        AppSettings in = AppSettings::defaults();
        in.prettySymbols = true;
        in.symbols = AppSettings::defaultSymbols();
        in.symbols[0].enabled = false;                    // desliga o primeiro (<=)
        in.symbols.append({"<>", QString::fromUtf8("≠"), true}); // adiciona um
        SettingsDialog dialog(in, Theme::builtIns().first());
        const AppSettings out = dialog.settings();
        QVERIFY(out.prettySymbols);
        bool hasCustom = false, firstOff = false;
        for (const SymbolRule &r : out.symbols) {
            if (r.sequence == QLatin1String("<>") && r.symbol == QString::fromUtf8("≠")) hasCustom = true;
            if (r.sequence == QLatin1String("<=") && !r.enabled) firstOff = true;
        }
        QVERIFY(hasCustom && firstOff);
    }
};

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("slayer-editor-tests");
    TestSettings t;
    return QTest::qExec(&t, argc, argv);
}
#include "tst_settings.moc"
