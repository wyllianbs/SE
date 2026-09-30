#pragma once

#include "Settings.h"

#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QPointer>

#include <functional>

class DapClient;
class TerminalDock;
class QProcess;

struct DebugFrame
{
    int id = 0;
    QString name;
    QString path;
    int line = 0; //< base 0
};

struct DebugVariable
{
    QString name;
    QString value;
    QString type;
    int reference = 0; //< > 0: tem filhos (expansível)
};

/**
 * Uma sessão do depurador via Debug Adapter Protocol, no estilo do Thonny:
 * Python com debugpy (o programa roda no terminal embutido, via
 * runInTerminal) e C/C++ com o DAP nativo do gdb (programa compilado com -g
 * numa aba de terminal só para ele).
 */
class DebugSession : public QObject
{
    Q_OBJECT
public:
    enum class State { Idle, Starting, Running, Stopped };

    explicit DebugSession(TerminalDock *terminal, QObject *parent = nullptr);
    ~DebugSession() override;

    static bool supports(const QString &languageId);
    State state() const { return m_state; }
    bool isActive() const { return m_state != State::Idle; }

    /** breakpoints: caminho absoluto → linhas (base 0). */
    void start(const QString &filePath, const QString &languageId, const AppSettings &settings,
               const QHash<QString, QList<int>> &breakpoints);
    void setBreakpoints(const QString &filePath, const QList<int> &lines);

    void stepOver();
    void stepInto();
    void stepOut();
    void resume();
    void stop();

    void selectFrame(int frameId);
    void variables(int reference, std::function<void(QList<DebugVariable>)> callback);

Q_SIGNALS:
    void stateChanged(DebugSession::State state);
    /** Parou (ponto de parada, passo, exceção): pilha, do topo para baixo. */
    void stopped(const QList<DebugFrame> &frames, const QString &reason);
    /** Escopos do quadro escolhido (Locals, Globals…): nome → referência. */
    void scopesReady(const QList<QPair<QString, int>> &scopes);
    void finished(const QString &message);
    void error(const QString &message);

private:
    void setState(State state);
    void launchAdapter(const QString &program, const QStringList &args, const QJsonObject &launch);
    void onEvent(const QString &name, const QJsonObject &body);
    void sendAllBreakpoints();
    void end(const QString &message);
    void step(const QString &command);
    bool compile(const QString &filePath, const QString &languageId, const AppSettings &settings, QString *binary);

    TerminalDock *m_terminal;
    QPointer<DapClient> m_dap;
    State m_state = State::Idle;
    int m_threadId = 1;
    QString m_workingDir;
    QHash<QString, QList<int>> m_breakpoints;
    bool m_configured = false;
};

Q_DECLARE_METATYPE(DebugFrame)
Q_DECLARE_METATYPE(DebugVariable)
