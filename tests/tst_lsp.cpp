#include "../src/Document.h"
#include "../src/LspClient.h"
#include "../src/LspManager.h"

#include <QApplication>
#include <QTemporaryDir>
#include <QTest>
#include <QTextDocument>

/** Integração com servidores reais; cada caso é pulado se o servidor não estiver instalado. */
class TestLsp : public QObject
{
    Q_OBJECT
    QTemporaryDir m_dir;

    Document *openFile(const QString &name, const QByteArray &content, const QString &languageId)
    {
        const QString path = m_dir.filePath(name);
        QFile f(path);
        f.open(QIODevice::WriteOnly);
        f.write(content);
        f.close();
        auto *doc = new Document(this);
        doc->load(path);
        doc->setLanguage(Language::byId(languageId));
        return doc;
    }

private Q_SLOTS:
    void framing()
    {
        QByteArray buf = LspClient::frame(QJsonObject{{"a", 1}}) + LspClient::frame(QJsonObject{{"b", 2}});
        buf += "Content-Length: 20\r\n\r\n{\"inc"; // mensagem incompleta fica no buffer
        const auto msgs = LspClient::unframe(buf);
        QCOMPARE(msgs.size(), 2);
        QCOMPARE(msgs[1].value("b").toInt(), 2);
        QVERIFY(buf.startsWith("Content-Length: 20"));
    }

    void clangdDiagnosticsAndCompletion()
    {
        if (!LspManager::isInstalled("clangd"))
            QSKIP("clangd not installed");
        LspManager lsp;
        AppSettings s = AppSettings::defaults();
        s.lspEnabled = true;
        lsp.setSettings(s);
        Document *doc = openFile("main.c", "#include <stdio.h>\nint main(void) {\n    undefined_fn();\n    pri\n}\n", "c");
        lsp.attach(doc);
        QTRY_VERIFY_WITH_TIMEOUT(!doc->diagnostics().isEmpty(), 20000);
        bool sawUndefined = false;
        for (const Diagnostic &d : doc->diagnostics())
            sawUndefined |= (d.line == 2 && d.message.contains("undefined_fn"));
        QVERIFY(sawUndefined);

        QList<LspCompletion> items;
        bool done = false;
        lsp.complete(doc, 3, 7, [&](const QList<LspCompletion> &r) { items = r; done = true; });
        QTRY_VERIFY_WITH_TIMEOUT(done, 20000);
        bool sawPrintf = false;
        for (const LspCompletion &i : items)
            sawPrintf |= i.insertText.startsWith("printf");
        QVERIFY2(sawPrintf, "printf not offered for 'pri'");
    }

    void clangdUsesSettingsFlags()
    {
        if (!LspManager::isInstalled("clangd"))
            QSKIP("clangd not installed");
        // -std=c23 (padrão de Settings → Languages): nullptr/bool são válidos sem cabeçalhos.
        LspManager lsp;
        AppSettings s = AppSettings::defaults();
        s.lspEnabled = true;
        lsp.setSettings(s);
        Document *doc = openFile("c23.c", "int main(void) { int *p = nullptr; bool b = true; return !p && b; }\nint x = ;\n", "c");
        lsp.attach(doc);
        QTRY_VERIFY_WITH_TIMEOUT(!doc->diagnostics().isEmpty(), 20000);
        for (const Diagnostic &d : doc->diagnostics())
            QVERIFY2(d.line == 1, qPrintable(d.message)); // só o erro proposital da linha 2
    }

    void pylspDiagnostics()
    {
        if (!LspManager::isInstalled("pylsp"))
            QSKIP("pylsp not installed");
        LspManager lsp;
        AppSettings s = AppSettings::defaults();
        s.lspEnabled = true;
        lsp.setSettings(s);
        Document *doc = openFile("m.py", "import os\nprint(undefined_name)\n", "python");
        lsp.attach(doc);
        QTRY_VERIFY_WITH_TIMEOUT(!doc->diagnostics().isEmpty(), 20000);
        bool sawUndefined = false;
        for (const Diagnostic &d : doc->diagnostics())
            sawUndefined |= d.message.contains("undefined_name");
        QVERIFY(sawUndefined);
    }

    void disabledDoesNotStart()
    {
        AppSettings s = AppSettings::defaults();
        s.lspEnabled = false;
        LspManager lsp;
        lsp.setSettings(s);
        Document *doc = openFile("off.c", "int x = ;\n", "c");
        lsp.attach(doc);
        QTest::qWait(1500);
        QVERIFY(doc->diagnostics().isEmpty());
        QVERIFY(lsp.statusFor(doc).isEmpty());
    }
};

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("slayer-editor-tests");
    TestLsp t;
    return QTest::qExec(&t, argc, argv);
}
#include "tst_lsp.moc"
