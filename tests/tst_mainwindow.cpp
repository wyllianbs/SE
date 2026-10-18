#include "../src/CodeEditor.h"
#include "../src/Document.h"
#include "../src/Icons.h"
#include "../src/MainWindow.h"
#include "../src/ProjectPanel.h"

#include <QApplication>
#include <QDir>
#include <QTextCursor>
#include <QTextDocument>
#include <QFile>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

// Janela principal de ponta a ponta, com as configurações num diretório
// temporário (XDG_CONFIG_HOME): nada do usuário é lido nem alterado.
class TestMainWindow : public QObject
{
    Q_OBJECT

    static void write(const QString &path, const QByteArray &data)
    {
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write(data);
    }
    static QByteArray read(const QString &path)
    {
        QFile f(path);
        return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
    }
    static CodeEditor *editorFor(MainWindow &w, const QString &path)
    {
        for (CodeEditor *e : w.findChildren<CodeEditor *>())
            if (e->filePath() == path)
                return e;
        return nullptr;
    }

private Q_SLOTS:
    void init() { QSettings().clear(); }

    void autoSaveKeepsExternalChanges()
    {
        // Arquivo alterado por outro programa (git pull…): o auto-save não pode
        // gravar a versão do editor por cima sem perguntar.
        QTemporaryDir tmp;
        const QString file = tmp.filePath(QStringLiteral("a.txt"));
        write(file, "one\n");
        MainWindow w;
        w.show();
        w.openFiles({file});
        CodeEditor *e = editorFor(w, file);
        QVERIFY(e);
        e->textCursor().insertText(QStringLiteral("x"));
        QTRY_COMPARE_WITH_TIMEOUT(read(file), QByteArray("xone\n"), 4000); // o auto-save funciona

        QTest::qWait(1100); // mtime diferente
        write(file, "external change\n");
        QTRY_VERIFY_WITH_TIMEOUT(w.findChild<QWidget *>(QStringLiteral("DiskBar"))->isVisible(), 3000);
        e->textCursor().insertText(QStringLiteral("y"));
        QTest::qWait(2500);
        QCOMPARE(read(file), QByteArray("external change\n"));
        e->doc()->text()->setModified(false); // fechar sem perguntar
    }

    void autoSaveFailureHasNoDialog()
    {
        // Falha ao salvar (aqui: caractere fora da codificação) não abre janela
        // a cada pausa na digitação; avisa na barra de status.
        QTemporaryDir tmp;
        const QString file = tmp.filePath(QStringLiteral("latin1.txt"));
        write(file, "abc\n");
        MainWindow w;
        w.show();
        w.openFiles({file});
        CodeEditor *e = editorFor(w, file);
        QVERIFY(e);
        e->doc()->setEncoding(QStringLiteral("ISO-8859-1"), false);
        bool dialog = false;
        // Se uma janela modal abrir, fecha-a (senão o teste ficaria parado nela).
        QTimer closer;
        connect(&closer, &QTimer::timeout, this, [&dialog]() {
            if (QWidget *m = QApplication::activeModalWidget()) {
                dialog = true;
                m->close();
            }
        });
        closer.start(200);
        e->textCursor().insertText(QStringLiteral("€"));
        QTest::qWait(2500);
        QVERIFY(!dialog);
        QCOMPARE(read(file), QByteArray("abc\n"));
        e->doc()->text()->setModified(false);
    }

    void startupWithProjectKeepsLooseSession()
    {
        // Reabrir o app com uma pasta aberta apagava a lista das abas soltas.
        QTemporaryDir tmp;
        const QString loose = tmp.filePath(QStringLiteral("loose.txt"));
        write(loose, "x\n");
        QDir(tmp.path()).mkpath(QStringLiteral("proj"));
        {
            QSettings s;
            s.setValue(QStringLiteral("session/files"), QStringList{loose});
            s.setValue(QStringLiteral("session/project"), tmp.filePath(QStringLiteral("proj")));
        }
        MainWindow w;
        QCOMPARE(QSettings().value(QStringLiteral("session/files")).toStringList(), QStringList{loose});
    }

    void directoryArgumentOpensProject()
    {
        QTemporaryDir tmp;
        QDir(tmp.path()).mkpath(QStringLiteral("myproj"));
        MainWindow w;
        w.openFiles({tmp.filePath(QStringLiteral("myproj"))});
        auto *panel = w.findChild<ProjectPanel *>();
        QVERIFY(panel);
        QCOMPARE(panel->windowTitle(), QStringLiteral("myproj"));
    }
};

int main(int argc, char **argv)
{
    QTemporaryDir config;
    qputenv("XDG_CONFIG_HOME", QFile::encodeName(config.path()));
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("slayer-editor-test"));
    QApplication::setApplicationName(QStringLiteral("slayer-editor-test"));
    Icons::loadFonts();
    TestMainWindow t;
    return QTest::qExec(&t, argc, argv);
}

#include "tst_mainwindow.moc"
