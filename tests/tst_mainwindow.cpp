#include "../src/CodeEditor.h"
#include "../src/Document.h"
#include "../src/Icons.h"
#include "../src/MainWindow.h"
#include "../src/ProjectPanel.h"
#include "../src/TerminalDock.h"

#include <qtermwidget.h>
#include <QApplication>
#include <QComboBox>
#include <QDockWidget>
#include <QLabel>
#include <QStatusBar>
#include <QToolButton>
#include <QSpinBox>
#include <QDir>
#include <QTextCursor>
#include <QTextDocument>
#include <QRandomGenerator>
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

    // Undo/redo não pode ser corrompido pelo recuo de wrap, pelo auto-save nem
    // por ligar/desligar a quebra de linha. Fuzz: digita um conteúdo conhecido
    // e faz um passeio aleatório de undo/redo + toggles; ao fim, navegar aos
    // extremos tem de devolver exatamente o início e o fim do histórico.
    void undoRedoSurvivesWrapAndAutoSave()
    {
        QTemporaryDir tmp;
        const QString file = tmp.filePath(QStringLiteral("u.c"));
        write(file, "int main() {\n}\n");
        MainWindow w;
        w.resize(520, 400);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        w.openFiles({file});
        CodeEditor *e = editorFor(w, file);
        QVERIFY(e);
        QTextDocument *d = e->doc()->text();
        e->setWordWrap(true);
        e->setWrapIndentEnabled(true); // o recuo de wrap é o que corrompia o redo
        e->setFocus();
        QTextCursor c = e->textCursor();
        c.movePosition(QTextCursor::Start);
        c.movePosition(QTextCursor::EndOfLine);
        e->setTextCursor(c);
        QTest::qWait(20);
        const QString base = d->toPlainText();
        for (const QString &s : {QStringLiteral("\n    int a = 1;"), QStringLiteral("\n    int b = 22;"),
                                 QStringLiteral("\n    int c = 333;"), QStringLiteral("\n    return a+b+c;")}) {
            e->textCursor().insertText(s);
            QTest::qWait(12);
        }
        const QString full = d->toPlainText();
        QVERIFY(full != base);

        QRandomGenerator rng(1234567u); // determinístico
        for (int step = 0; step < 120; ++step) {
            switch (rng.bounded(6)) {
            case 0: if (d->isUndoAvailable()) e->undo(); break;
            case 1: if (d->isRedoAvailable()) e->redo(); break;
            case 2: e->setWordWrap(rng.bounded(2)); break;
            case 3: e->setWrapIndentEnabled(rng.bounded(2)); break;
            case 4: QTest::qWait(1600); break; // deixa o auto-save disparar
            default: break;
            }
            QTest::qWait(4);
        }

        // Desfaz até o início: tem de voltar exatamente ao conteúdo aberto.
        int guard = 0;
        while (d->isUndoAvailable() && guard++ < 500) { e->undo(); QTest::qWait(3); }
        QCOMPARE(d->toPlainText(), base);
        // Refaz até o fim: tem de reconstruir exatamente o conteúdo digitado.
        guard = 0;
        while (d->isRedoAvailable() && guard++ < 500) { e->redo(); QTest::qWait(3); }
        QCOMPARE(d->toPlainText(), full);
        e->doc()->text()->setModified(false);
    }

    // O indicador de linguagem no rodapé é só um rótulo (não um combo com lista):
    // mostra a linguagem da aba ativa e muda ao trocar de aba.
    void statusBarLanguageIsLabel()
    {
        QTemporaryDir tmp;
        const QString py = tmp.filePath(QStringLiteral("a.py"));
        const QString c = tmp.filePath(QStringLiteral("b.c"));
        write(py, "print(1)\n");
        write(c, "int main(){}\n");
        MainWindow w;
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        // Não há mais um QComboBox (seletor com lista) no rodapé.
        QVERIFY(!w.statusBar()->findChild<QComboBox *>());
        auto *label = w.findChild<QLabel *>(QStringLiteral("LanguageLabel"));
        QVERIFY(label);

        w.openFiles({py});
        QVERIFY2(label->text().endsWith(QStringLiteral("&nbsp;Python")), qPrintable(label->text()));
        w.openFiles({c});
        QVERIFY2(label->text().endsWith(QStringLiteral("&nbsp;C")), qPrintable(label->text()));
    }

    // Se o terminal estava visível na sessão anterior, ao reabrir o software ele
    // deve subir com um shell pronto (não um painel vazio).
    void terminalRestoredVisibleGetsShell()
    {
        {
            MainWindow w1;
            w1.show();
            QVERIFY(QTest::qWaitForWindowExposed(&w1));
            auto *dock = w1.findChild<QDockWidget *>(QStringLiteral("TerminalDock"));
            QVERIFY(dock);
            dock->setVisible(true);
            QSettings().setValue(QStringLiteral("ui/state"), w1.saveState());
        }
        MainWindow w2; // restaura o estado: terminal visível
        w2.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w2));
        auto *term = w2.findChild<TerminalDock *>();
        QVERIFY(term);
        QTRY_VERIFY_WITH_TIMEOUT(term->current() && term->current()->getShellPID() > 0, 15000);
    }

    // No rodapé: clicar em INSERT/OVERWRITE alterna o modo; clicar na posição
    // do cursor abre a barra "ir para linha".
    void statusModeAndCursorClickable()
    {
        QTemporaryDir tmp;
        const QString f = tmp.filePath(QStringLiteral("a.c"));
        write(f, "int main() {\n    return 0;\n}\n");
        MainWindow w;
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        w.openFiles({f});
        CodeEditor *e = editorFor(w, f);
        QVERIFY(e);
        QToolButton *mode = nullptr, *cursor = nullptr;
        for (QToolButton *b : w.findChildren<QToolButton *>()) {
            if (b->text() == QLatin1String("INSERT") || b->text() == QLatin1String("OVERWRITE"))
                mode = b;
            else if (b->text().contains(QLatin1Char(':')) && !b->text().isEmpty() && b->text().at(0).isDigit())
                cursor = b;
        }
        QVERIFY(mode);
        QVERIFY(cursor);
        // Alterna o modo de inserção.
        QVERIFY(!e->overwriteMode());
        mode->click();
        QVERIFY(e->overwriteMode());
        QCOMPARE(mode->text(), QStringLiteral("OVERWRITE"));
        mode->click();
        QVERIFY(!e->overwriteMode());
        // Clique na posição do cursor é um toggle: abre, e um novo clique fecha.
        QSpinBox *gotoSpin = w.findChild<QSpinBox *>();
        QVERIFY(gotoSpin);
        cursor->click();
        QVERIFY(gotoSpin->isVisible());
        cursor->click();
        QVERIFY(!gotoSpin->isVisible());
    }

    // "Abrir arquivo" começa na pasta da aba ATIVA, não na do último arquivo aberto.
    void openDialogUsesActiveTabDir()
    {
        QTemporaryDir dirA, dirB;
        const QString fileA = dirA.filePath(QStringLiteral("a.c"));
        const QString fileB = dirB.filePath(QStringLiteral("b.c"));
        write(fileA, "int a;\n");
        write(fileB, "int b;\n");
        MainWindow w;
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        w.openFiles({fileA});
        w.openFiles({fileB}); // ativa B; o "último diretório" passa a ser o de B
        QCOMPARE(QDir(w.currentFileDir()).absolutePath(), QDir(dirB.path()).absolutePath());
        w.openFiles({fileA}); // volta o foco para a aba de A
        // Deve ser a pasta de A (aba ativa), não a de B (último aberto).
        QCOMPARE(QDir(w.currentFileDir()).absolutePath(), QDir(dirA.path()).absolutePath());
    }

    // O terminal acoplado em cima/embaixo (horizontal) deve poder crescer tanto
    // quanto acoplado às laterais — a tela de boas-vindas não pode travar a
    // altura mínima do editor (ficava presa em ~422 px).
    void terminalDockResizesLikeVertical()
    {
        MainWindow w;
        w.resize(1000, 700);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        auto *dock = w.findChild<QDockWidget *>(QStringLiteral("TerminalDock"));
        QVERIFY(dock);
        QWidget *central = w.centralWidget();

        // Embaixo, puxado ao máximo: o terminal deve ocupar a maior parte (o
        // editor encolhe a um resto pequeno). Antes do ajuste o editor travava
        // em ~422 px e o terminal mal passava de ~165 px.
        w.addDockWidget(Qt::BottomDockWidgetArea, dock);
        QTest::qWait(120);
        w.resizeDocks({dock}, {10000}, Qt::Vertical);
        QTest::qWait(120);
        QVERIFY2(dock->height() > central->height(),
                 qPrintable(QStringLiteral("dock=%1 central=%2").arg(dock->height()).arg(central->height())));
        QVERIFY(dock->height() > w.height() / 2);

        // À direita, o mesmo já funcionava; confirma a simetria.
        w.addDockWidget(Qt::RightDockWidgetArea, dock);
        QTest::qWait(120);
        w.resizeDocks({dock}, {10000}, Qt::Horizontal);
        QTest::qWait(120);
        QVERIFY(dock->width() > central->width());
    }

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
