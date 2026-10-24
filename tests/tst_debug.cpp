#include "../src/DebugSession.h"
#include "../src/TerminalDock.h"

#include <qtermwidget.h>
#include <QAction>

#include <QApplication>
#include <QMainWindow>
#include <QProcess>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

/** Sessões reais com debugpy e gdb; cada caso é pulado se a ferramenta faltar. */
class TestDebug : public QObject
{
    Q_OBJECT
    QTemporaryDir m_dir;
    QMainWindow m_window;
    TerminalDock *m_terminal = nullptr;

    QString write(const QString &name, const QByteArray &code)
    {
        const QString path = m_dir.filePath(name);
        QFile f(path);
        f.open(QIODevice::WriteOnly);
        f.write(code);
        return path;
    }

    /** Espera parar e devolve a linha do topo da pilha (base 0). */
    static int waitStopped(QSignalSpy &spy, int timeout = 40000)
    {
        const int before = spy.count();
        if (!QTest::qWaitFor([&]() { return spy.count() > before; }, timeout))
            return -1;
        const auto frames = spy.last().at(0).value<QList<DebugFrame>>();
        return frames.isEmpty() ? -1 : frames.first().line;
    }

    static QList<DebugVariable> locals(DebugSession &s, QSignalSpy &scopes)
    {
        if (!QTest::qWaitFor([&]() { return scopes.count() > 0; }, 20000))
            return {};
        const auto list = scopes.last().at(0).value<QList<QPair<QString, int>>>();
        QList<DebugVariable> vars;
        bool done = false;
        s.variables(list.isEmpty() ? 0 : list.first().second, [&](QList<DebugVariable> v) { vars = v; done = true; });
        (void)QTest::qWaitFor([&]() { return done; }, 20000);
        return vars;
    }

    static QString valueOf(const QList<DebugVariable> &vars, const QString &name)
    {
        for (const DebugVariable &v : vars)
            if (v.name == name)
                return v.value;
        return QString();
    }

private Q_SLOTS:
    void initTestCase()
    {
        qRegisterMetaType<QList<DebugFrame>>();
        qRegisterMetaType<QList<QPair<QString, int>>>();
        m_terminal = new TerminalDock(&m_window);
        m_window.addDockWidget(Qt::BottomDockWidgetArea, m_terminal);
        m_terminal->configure(QStringLiteral("/bin/bash"), false);
        m_window.resize(900, 600);
        m_window.show();
    }

    void stopBeforeAdapterAnswers()
    {
        // Parar antes de o adaptador responder ao "initialize" derrubava o app
        // (a resposta atrasada usava a sessão já encerrada).
        if (QStandardPaths::findExecutable(QStringLiteral("python3")).isEmpty())
            QSKIP("python3 not installed");
        QTemporaryDir tmp;
        // "python" falso: -c → ok; -m debugpy.adapter → adaptador DAP que responde em 0,8 s.
        const QString fake = tmp.filePath(QStringLiteral("python"));
        QFile script(fake);
        QVERIFY(script.open(QIODevice::WriteOnly));
        script.write(R"SH(#!/bin/sh
if [ "$1" = "-c" ]; then exit 0; fi
exec python3 -u -c '
import sys, json, time
def read():
    h = b""
    while not h.endswith(b"\r\n\r\n"):
        c = sys.stdin.buffer.read(1)
        if not c:
            sys.exit(0)
        h += c
    n = int([l for l in h.split(b"\r\n") if l.lower().startswith(b"content-length")][0].split(b":")[1])
    return json.loads(sys.stdin.buffer.read(n))
def send(o):
    b = json.dumps(o).encode()
    sys.stdout.buffer.write(b"Content-Length: %d\r\n\r\n" % len(b) + b)
    sys.stdout.buffer.flush()
first = True
while True:
    m = read()
    if first:
        time.sleep(0.8)
        first = False
    send({"type": "response", "request_seq": m["seq"], "success": True, "command": m.get("command"), "body": {}})
'
)SH");
        script.close();
        QFile::setPermissions(fake, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);

        AppSettings settings = AppSettings::defaults();
        LanguageConfig py = settings.languageConfig(QStringLiteral("python"));
        py.tool = fake;
        settings.languages.insert(QStringLiteral("python"), py);
        const QString file = tmp.filePath(QStringLiteral("a.py"));
        QFile src(file);
        QVERIFY(src.open(QIODevice::WriteOnly));
        src.write("print(1)\n");
        src.close();

        DebugSession session(m_terminal);
        session.start(file, QStringLiteral("python"), settings, {});
        QCOMPARE(session.state(), DebugSession::State::Starting);
        session.stop();
        QTest::qWait(1500); // a resposta atrasada chega aqui: não pode derrubar o teste
        QCOMPARE(session.state(), DebugSession::State::Idle);
    }

    void ctrlShiftFFindsInTerminal()
    {
        // Ctrl+Shift+F com o foco no terminal abre a busca dele (e não chega ao shell).
        QTermWidget *t = m_terminal->current();
        QVERIFY(t);
        QWidget *display = nullptr;
        for (QWidget *w : t->findChildren<QWidget *>())
            if (QString::fromLatin1(w->metaObject()->className()).endsWith(QLatin1String("TerminalDisplay")))
                display = w;
        QVERIFY(display);
        QWidget *searchBar = nullptr;
        for (QWidget *w : t->findChildren<QWidget *>())
            if (QString::fromLatin1(w->metaObject()->className()) == QLatin1String("SearchBar"))
                searchBar = w;
        QVERIFY(searchBar);
        QVERIFY(!searchBar->isVisible());
        QTest::keyClick(display, Qt::Key_F, Qt::ControlModifier | Qt::ShiftModifier);
        QTRY_VERIFY(searchBar->isVisible());
    }

    void python()
    {
        QProcess probe;
        probe.start("python3", {"-c", "import debugpy"});
        probe.waitForFinished();
        if (probe.exitCode() != 0)
            QSKIP("debugpy not installed");
        const QString file = write("prog.py", "a = 1\nx = a + 40\ny = x * 2\nprint(y)\n");
        DebugSession s(m_terminal);
        QSignalSpy stopped(&s, &DebugSession::stopped);
        QSignalSpy scopes(&s, &DebugSession::scopesReady);
        QSignalSpy finished(&s, &DebugSession::finished);
        QSignalSpy errors(&s, &DebugSession::error);
        s.start(file, "python", AppSettings::defaults(), {{file, {2}}}); // para em "y = x * 2"
        QCOMPARE(waitStopped(stopped), 2);
        QVERIFY2(errors.isEmpty(), errors.isEmpty() ? "" : qPrintable(errors.first().at(0).toString()));
        QCOMPARE(valueOf(locals(s, scopes), "x"), QStringLiteral("41"));
        scopes.clear();
        s.stepOver();
        QCOMPARE(waitStopped(stopped), 3);
        QCOMPARE(valueOf(locals(s, scopes), "y"), QStringLiteral("82"));
        s.resume();
        QVERIFY(QTest::qWaitFor([&]() { return finished.count() > 0; }, 20000));
        QCOMPARE(s.state(), DebugSession::State::Idle);
    }

    void runAgainLeavesInteractiveRepl()
    {
        const QString prog = write("repl.py", "z = 1\n");
        const QString marker = m_dir.filePath("ran.txt");
        m_terminal->runCommand("python3 -i " + prog, m_dir.path());
        // Espera o REPL assumir o terminal.
        QVERIFY(QTest::qWaitFor([&]() {
            auto *t = m_terminal->current();
            const int fg = t ? t->getForegroundProcessId() : 0;
            return fg > 0 && fg != t->getShellPID() && QFileInfo::exists(QStringLiteral("/proc/%1").arg(fg));
        }, 30000));
        m_terminal->runCommand("echo ok > '" + marker + "'", m_dir.path());
        QVERIFY(QTest::qWaitFor([&]() { return QFile::exists(marker); }, 30000));
        QVERIFY(m_terminal->current()->getShellPID() > 0); // o shell continua vivo
    }

    void cReadsInputFromDebugTab()
    {
        if (QStandardPaths::findExecutable("gdb").isEmpty())
            QSKIP("gdb not installed");
        const QString file = write("input.c",
                                   "#include <stdio.h>\n"
                                   "int main(void) {\n"
                                   "    int n = 0;\n"
                                   "    if (scanf(\"%d\", &n) != 1) return 1;\n"
                                   "    int dobro = n * 2;\n"          // linha 4 (base 0)
                                   "    printf(\"%d\\n\", dobro);\n"
                                   "    return 0;\n"
                                   "}\n");
        const int tabs = m_terminal->terminals().size();
        DebugSession s(m_terminal);
        QSignalSpy stopped(&s, &DebugSession::stopped);
        QSignalSpy scopes(&s, &DebugSession::scopesReady);
        QSignalSpy finished(&s, &DebugSession::finished);
        s.start(file, "c", AppSettings::defaults(), {{file, {5}}});
        // A compilação é assíncrona: start() volta na hora.
        QCOMPARE(s.state(), DebugSession::State::Starting);
        QVERIFY(QTest::qWaitFor([&]() { return s.state() == DebugSession::State::Running; }, 40000));
        QTest::qWait(500);
        m_terminal->current()->sendText("5\n"); // o usuário digita na aba do programa
        QCOMPARE(waitStopped(stopped), 5);
        QCOMPARE(valueOf(locals(s, scopes), "dobro"), QStringLiteral("10"));
        s.resume();
        QVERIFY(QTest::qWaitFor([&]() { return finished.count() > 0; }, 40000));
        // Mesma aba do Run (nenhuma aba nova) e o prompt volta ao fim da sessão.
        QCOMPARE(m_terminal->terminals().size(), tabs);
        QTermWidget *t = m_terminal->current();
        QVERIFY(QTest::qWaitFor([t]() { return t->getForegroundProcessId() == t->getShellPID(); }, 15000));
    }

    void rustStopsAtBreakpoint()
    {
        if (QStandardPaths::findExecutable("rustc").isEmpty())
            QSKIP("rustc not installed");
        if (QStandardPaths::findExecutable("gdb").isEmpty())
            QSKIP("gdb not installed"); // rust-gdb precisa dele; sem rust-gdb, usa o gdb puro
        const QString file = write("prog.rs",
                                   "fn main() {\n"
                                   "    let n = 5;\n"
                                   "    let dobro = n * 2;\n"        // linha 2 (base 0)
                                   "    println!(\"{}\", dobro);\n"  // linha 3
                                   "}\n");
        DebugSession s(m_terminal);
        QSignalSpy stopped(&s, &DebugSession::stopped);
        QSignalSpy scopes(&s, &DebugSession::scopesReady);
        QSignalSpy finished(&s, &DebugSession::finished);
        QSignalSpy errors(&s, &DebugSession::error);
        s.start(file, "rust", AppSettings::defaults(), {{file, {3}}}); // para no println!
        // A compilação é assíncrona: start() volta na hora.
        QCOMPARE(s.state(), DebugSession::State::Starting);
        QCOMPARE(waitStopped(stopped, 60000), 3);
        QVERIFY2(errors.isEmpty(), errors.isEmpty() ? "" : qPrintable(errors.first().at(0).toString()));
        QCOMPARE(valueOf(locals(s, scopes), "dobro"), QStringLiteral("10"));
        s.resume();
        QVERIFY(QTest::qWaitFor([&]() { return finished.count() > 0; }, 40000));
        QCOMPARE(s.state(), DebugSession::State::Idle);
    }

    void compileErrorIsReported()
    {
        if (QStandardPaths::findExecutable("gcc").isEmpty())
            QSKIP("gcc not installed");
        const QString file = write("broken.c", "int main(void) { return x; }\n");
        DebugSession s(m_terminal);
        QSignalSpy errors(&s, &DebugSession::error);
        s.start(file, "c", AppSettings::defaults(), {});
        QVERIFY(QTest::qWaitFor([&]() { return errors.count() > 0; }, 20000));
        QVERIFY(errors.first().at(0).toString().contains("x"));
        QCOMPARE(s.state(), DebugSession::State::Idle);
    }

    void terminalHasCopyPasteMenu()
    {
        QTermWidget *t = m_terminal->current();
        QVERIFY(t);
        // Menu de contexto habilitado no terminal e no display interno.
        QCOMPARE(t->contextMenuPolicy(), Qt::CustomContextMenu);
        // Ações Copiar/Colar instaladas (Ctrl+Shift+C / Ctrl+Shift+V).
        bool copy = false, paste = false;
        for (QAction *a : t->actions()) {
            if (a->shortcut() == QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_C)) copy = true;
            if (a->shortcut() == QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_V)) paste = true;
        }
        QVERIFY(copy && paste);
        // Copiar/colar não quebram sem seleção.
        t->copyClipboard();
        t->pasteClipboard();
    }

    void editedThemeReachesTerminal()
    {
        // Temas criados/alterados durante a sessão precisam valer no terminal
        // (o QTermWidget guarda cada esquema na primeira vez que o usa).
        auto sample = [this]() {
            QTest::qWait(300);
            const QImage img = m_terminal->current()->grab().toImage();
            return img.pixelColor(img.width() / 2, img.height() - 8);
        };
        Theme red = Theme::builtIns().first();
        red.id = "test-red";
        red.terminalBackground = QColor("#ff0000");
        m_terminal->setTheme(red);
        QCOMPARE(sample().name(), QStringLiteral("#ff0000"));
        Theme blue = red;
        blue.terminalBackground = QColor("#0000ff");
        m_terminal->setTheme(blue); // mesmo id, cores novas
        QCOMPARE(sample().name(), QStringLiteral("#0000ff"));
    }

    void cWithGdb()
    {
        if (QStandardPaths::findExecutable("gdb").isEmpty() || QStandardPaths::findExecutable("gcc").isEmpty())
            QSKIP("gdb/gcc not installed");
        const QString file = write("prog.c",
                                   "#include <stdio.h>\n"          // 0
                                   "int square(int v) {\n"         // 1
                                   "    return v * v;\n"           // 2
                                   "}\n"                           // 3
                                   "int main(void) {\n"            // 4
                                   "    int n = 7;\n"              // 5
                                   "    int r = square(n);\n"      // 6
                                   "    printf(\"%d\\n\", r);\n"   // 7
                                   "    return 0;\n"               // 8
                                   "}\n");
        DebugSession s(m_terminal);
        QSignalSpy stopped(&s, &DebugSession::stopped);
        QSignalSpy scopes(&s, &DebugSession::scopesReady);
        QSignalSpy finished(&s, &DebugSession::finished);
        QSignalSpy errors(&s, &DebugSession::error);
        s.start(file, "c", AppSettings::defaults(), {{file, {6}}});
        QCOMPARE(waitStopped(stopped), 6);
        QVERIFY2(errors.isEmpty(), errors.isEmpty() ? "" : qPrintable(errors.first().at(0).toString()));
        QCOMPARE(valueOf(locals(s, scopes), "n"), QStringLiteral("7"));
        s.stepInto();
        QCOMPARE(waitStopped(stopped), 2); // dentro de square()
        s.stepOut();
        QVERIFY(waitStopped(stopped) >= 6);
        s.resume();
        QVERIFY(QTest::qWaitFor([&]() { return finished.count() > 0; }, 20000));
    }
};

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    TestDebug t;
    return QTest::qExec(&t, argc, argv);
}
#include "tst_debug.moc"
