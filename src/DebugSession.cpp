#include "DebugSession.h"
#include "DapClient.h"
#include "Language.h"
#include "TerminalDock.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QPointer>
#include <QProcess>
#include <QStandardPaths>
#include <QTimer>

namespace {

QString shellQuote(const QString &s)
{
    QString out = s;
    out.replace(QLatin1Char('\''), QLatin1String("'\\''"));
    return QLatin1Char('\'') + out + QLatin1Char('\'');
}

} // namespace

DebugSession::DebugSession(TerminalDock *terminal, QObject *parent)
    : QObject(parent)
    , m_terminal(terminal)
{
}

DebugSession::~DebugSession()
{
    // Na destruição (fechar a janela durante uma sessão), nada de sinais: a
    // janela principal que os receberia já está sendo destruída.
    if (m_dap) {
        m_dap->disconnect(this);
        m_dap->stop();
    }
}

bool DebugSession::supports(const QString &id)
{
    return Language::debuggable(id);
}

void DebugSession::setState(State state)
{
    if (m_state == state)
        return;
    m_state = state;
    Q_EMIT stateChanged(state);
}

/**
 * C/C++ com -g -O0 (sem as otimizações das configurações), sem bloquear a
 * interface: done(binário) é chamado ao terminar com sucesso.
 */
void DebugSession::compileAsync(const QString &filePath, const QString &id, const AppSettings &settings,
                                std::function<void(const QString &binary)> done)
{
    const LanguageConfig cfg = settings.languageConfig(id);
    QStringList before, after;
    for (const QString &f : QProcess::splitCommand(cfg.flags)) {
        if (f.startsWith(QLatin1String("-O")) || f == QLatin1String("-s"))
            continue;
        // -lm e -Wl,… vão depois do arquivo, como no terminal.
        (f.startsWith(QLatin1String("-l")) || f.startsWith(QLatin1String("-Wl,")) ? after : before) << f;
    }
    const QString outDir = QStringLiteral("/tmp/slayer_out");
    QDir().mkpath(outDir);
    const QString binary = outDir + QLatin1Char('/') + QFileInfo(filePath).completeBaseName() + QStringLiteral("-debug");

    auto *cc = new QProcess(this);
    m_compiler = cc;
    cc->setWorkingDirectory(QFileInfo(filePath).absolutePath());
    cc->setProcessChannelMode(QProcess::MergedChannels);
    // Só a compilação atual conta: uma interrompida (parar, iniciar de novo)
    // termina depois e não pode encerrar a sessão nova.
    connect(cc, &QProcess::errorOccurred, this, [this, cc, tool = cfg.tool](QProcess::ProcessError e) {
        if (e != QProcess::FailedToStart)
            return;
        cc->deleteLater();
        if (m_compiler != cc)
            return;
        m_compiler = nullptr;
        setState(State::Idle);
        Q_EMIT error(tr("Could not start the compiler “%1”.").arg(tool));
    });
    connect(cc, &QProcess::finished, this, [this, cc, binary, done](int code, QProcess::ExitStatus status) {
        cc->deleteLater();
        if (m_compiler != cc || m_state != State::Starting)
            return; // parado (ou reiniciado) durante a compilação
        m_compiler = nullptr;
        if (status != QProcess::NormalExit || code != 0) {
            setState(State::Idle);
            Q_EMIT error(tr("Compilation failed:\n\n%1").arg(QString::fromLocal8Bit(cc->readAll()).trimmed()));
            return;
        }
        done(binary);
    });
    Q_EMIT status(tr("Compiling %1 with -g…").arg(QFileInfo(filePath).fileName()));
    cc->start(cfg.tool, before + QStringList{"-g", "-O0", filePath, "-o", binary} + after);
}

void DebugSession::start(const QString &filePath, const QString &id, const AppSettings &settings,
                         const QHash<QString, QList<int>> &breakpoints)
{
    if (isActive())
        stop();
    m_breakpoints = breakpoints;
    m_workingDir = QFileInfo(filePath).absolutePath();
    m_configured = false;
    m_threadId = 1;

    if (id == QLatin1String("python")) {
        const QString python = settings.languageConfig(id).tool;
        QProcess probe;
        probe.start(python, {"-c", "import debugpy"});
        probe.waitForFinished(5000);
        if (probe.exitStatus() != QProcess::NormalExit || probe.exitCode() != 0) {
            Q_EMIT error(tr("debugpy is not installed for %1.\n\nInstall it with:\nsudo apt install python3-debugpy")
                             .arg(python));
            return;
        }
        const QJsonObject launch{{"name", "Slayer Editor"}, {"type", "python"}, {"request", "launch"},
                                 {"program", filePath}, {"cwd", m_workingDir}, {"python", QJsonArray{python}},
                                 {"console", "integratedTerminal"}, {"justMyCode", true}};
        setState(State::Starting);
        launchAdapter(python, {"-m", "debugpy.adapter"}, launch);
        return;
    }

    // C / C++: gdb com DAP nativo (gdb 14+).
    if (QStandardPaths::findExecutable(QStringLiteral("gdb")).isEmpty()) {
        Q_EMIT error(tr("gdb is not installed.\n\nInstall it with:\nsudo apt install gdb"));
        return;
    }
    setState(State::Starting);
    compileAsync(filePath, id, settings, [this, filePath](const QString &binary) {
        // A aba de shell do terminal, com o shell em espera: o programa lê e escreve nela.
        const QString tty = m_terminal->debugTty();
        QStringList args{"-q", "-nx", "-i", "dap"};
        if (!tty.isEmpty())
            args << "-iex" << QStringLiteral("set inferior-tty %1").arg(tty);
        const QJsonObject launch{{"name", "Slayer Editor"}, {"request", "launch"}, {"program", binary},
                                 {"cwd", m_workingDir}};
        launchAdapter(QStringLiteral("gdb"), args, launch);
    });
}

void DebugSession::launchAdapter(const QString &program, const QStringList &args, const QJsonObject &launch)
{
    m_dap = new DapClient(this);
    connect(m_dap, &DapClient::event, this, &DebugSession::onEvent);
    connect(m_dap, &DapClient::exited, this, [this]() { end(tr("Debugger finished.")); });
    // debugpy pede para rodar o programa num terminal: vai para o embutido.
    m_dap->setReverseHandler([this](const QString &, const QJsonObject &a) {
        QStringList parts;
        const QJsonObject env = a.value("env").toObject();
        if (!env.isEmpty()) {
            parts << QStringLiteral("env");
            for (auto it = env.begin(); it != env.end(); ++it)
                parts << shellQuote(it.key() + QLatin1Char('=') + it.value().toString());
        }
        for (const QJsonValue &v : a.value("args").toArray())
            parts << shellQuote(v.toString());
        const QString cwd = a.value("cwd").toString(m_workingDir);
        m_terminal->runCommand(parts.join(QLatin1Char(' ')), cwd);
        return QJsonObject{};
    });
    if (!m_dap->start(program, args, m_workingDir)) {
        end(QString());
        Q_EMIT error(tr("Could not start the debugger “%1”.").arg(program));
        return;
    }
    const QJsonObject init{{"clientID", "slayer-editor"}, {"clientName", "Slayer Editor"},
                           {"adapterID", launch.value("type").toString("gdb")}, {"linesStartAt1", true},
                           {"columnsStartAt1", true}, {"pathFormat", "path"},
                           {"supportsRunInTerminalRequest", true}, {"supportsVariableType", true}};
    // As respostas podem chegar depois de a sessão parar (o adaptador vive mais
    // 2 s): só valem para o adaptador ainda atual.
    const QPointer<DapClient> dap = m_dap;
    m_dap->request("initialize", init, [this, dap, launch](bool ok, const QJsonObject &, const QString &msg) {
        if (!dap || dap != m_dap)
            return;
        if (!ok) {
            Q_EMIT error(tr("The debugger refused to start: %1").arg(msg));
            stop();
            return;
        }
        // A resposta do launch só chega depois do configurationDone.
        m_dap->request("launch", launch, [this, dap](bool ok, const QJsonObject &, const QString &msg) {
            if (!dap || dap != m_dap)
                return;
            if (!ok) {
                Q_EMIT error(tr("Could not launch the program: %1").arg(msg));
                stop();
            }
        });
    });
}

void DebugSession::sendAllBreakpoints()
{
    for (auto it = m_breakpoints.cbegin(); it != m_breakpoints.cend(); ++it)
        setBreakpoints(it.key(), it.value());
}

void DebugSession::setBreakpoints(const QString &filePath, const QList<int> &lines)
{
    m_breakpoints.insert(filePath, lines);
    if (!m_dap || !m_configured)
        return;
    QJsonArray bps;
    for (int l : lines)
        bps.append(QJsonObject{{"line", l + 1}});
    m_dap->request("setBreakpoints", QJsonObject{{"source", QJsonObject{{"path", filePath}}}, {"breakpoints", bps}});
}

void DebugSession::onEvent(const QString &name, const QJsonObject &body)
{
    if (name == QLatin1String("initialized")) {
        m_configured = true;
        sendAllBreakpoints();
        m_dap->request("configurationDone");
        setState(State::Running);
    } else if (name == QLatin1String("stopped")) {
        m_threadId = body.value("threadId").toInt(m_threadId);
        const QString reason = body.value("reason").toString();
        QString detail = body.value("text").toString();
        if (detail.isEmpty())
            detail = body.value("description").toString();
        setState(State::Stopped);
        const QPointer<DapClient> dap = m_dap;
        m_dap->request("stackTrace", QJsonObject{{"threadId", m_threadId}, {"levels", 50}},
                       [this, dap, reason, detail](bool ok, const QJsonObject &b, const QString &) {
                           if (!dap || dap != m_dap)
                               return; // sessão encerrada enquanto a pilha vinha
                           QList<DebugFrame> frames;
                           if (ok) {
                               for (const QJsonValue &v : b.value("stackFrames").toArray()) {
                                   const QJsonObject f = v.toObject();
                                   frames.append({f.value("id").toInt(), f.value("name").toString(),
                                                  f.value("source").toObject().value("path").toString(),
                                                  qMax(0, f.value("line").toInt() - 1)});
                               }
                           }
                           Q_EMIT stopped(frames, detail.isEmpty() ? reason : reason + QStringLiteral(": ") + detail);
                           if (!frames.isEmpty())
                               selectFrame(frames.first().id);
                       });
    } else if (name == QLatin1String("continued")) {
        setState(State::Running);
    } else if (name == QLatin1String("terminated") || name == QLatin1String("exited")) {
        if (name == QLatin1String("exited"))
            end(tr("Program exited with code %1.").arg(body.value("exitCode").toInt()));
        else
            end(tr("Debugging finished."));
    }
}

void DebugSession::selectFrame(int frameId)
{
    if (!m_dap)
        return;
    const QPointer<DapClient> dap = m_dap;
    m_dap->request("scopes", QJsonObject{{"frameId", frameId}}, [this, dap](bool ok, const QJsonObject &b, const QString &) {
        if (!dap || dap != m_dap)
            return;
        QList<QPair<QString, int>> scopes;
        if (ok) {
            for (const QJsonValue &v : b.value("scopes").toArray()) {
                const QJsonObject s = v.toObject();
                const QString name = s.value("name").toString();
                if (name == QLatin1String("Registers")) // gdb: pouco útil em aula
                    continue;
                scopes.append({name, s.value("variablesReference").toInt()});
            }
        }
        Q_EMIT scopesReady(scopes);
    });
}

void DebugSession::variables(int reference, std::function<void(QList<DebugVariable>)> callback)
{
    if (!m_dap || reference <= 0) {
        callback({});
        return;
    }
    m_dap->request("variables", QJsonObject{{"variablesReference", reference}},
                   [callback](bool ok, const QJsonObject &b, const QString &) {
                       QList<DebugVariable> out;
                       if (ok) {
                           for (const QJsonValue &v : b.value("variables").toArray()) {
                               const QJsonObject o = v.toObject();
                               const QString name = o.value("name").toString();
                               // debugpy agrupa internos como "special variables".
                               if (name.startsWith(QLatin1String("special variables"))
                                   || name.startsWith(QLatin1String("function variables")))
                                   continue;
                               out.append({name, o.value("value").toString(), o.value("type").toString(),
                                           o.value("variablesReference").toInt()});
                           }
                       }
                       callback(out);
                   });
}

void DebugSession::step(const QString &command)
{
    if (!m_dap || m_state != State::Stopped)
        return;
    setState(State::Running);
    m_dap->request(command, QJsonObject{{"threadId", m_threadId}});
}

void DebugSession::stepOver() { step(QStringLiteral("next")); }
void DebugSession::stepInto() { step(QStringLiteral("stepIn")); }
void DebugSession::stepOut() { step(QStringLiteral("stepOut")); }
void DebugSession::resume() { step(QStringLiteral("continue")); }

void DebugSession::stop()
{
    if (m_compiler) {
        m_compiler->kill(); // parar durante a compilação
        m_compiler = nullptr;
    }
    if (!m_dap) {
        setState(State::Idle);
        return;
    }
    QPointer<DapClient> dap = m_dap;
    dap->request("disconnect", QJsonObject{{"terminateDebuggee", true}});
    // Se o adaptador não sair sozinho, encerra.
    QTimer::singleShot(1500, this, [dap]() {
        if (dap)
            dap->stop();
    });
    end(tr("Debugging stopped."));
}

void DebugSession::end(const QString &message)
{
    if (m_state == State::Idle && !m_dap)
        return;
    if (m_dap) {
        m_dap->disconnect(this);
        QPointer<DapClient> dap = m_dap;
        m_dap = nullptr;
        QTimer::singleShot(2000, dap, [dap]() {
            if (dap) {
                dap->stop();
                dap->deleteLater();
            }
        });
    }
    m_configured = false;
    m_terminal->releaseDebugTty();
    setState(State::Idle);
    if (!message.isEmpty())
        Q_EMIT finished(message);
}
