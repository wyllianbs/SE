#pragma once

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QObject>
#include <QStringList>

#include <functional>

class QProcess;

/**
 * Cliente de um servidor LSP: processo com pipes (stdin/stdout) e JSON-RPC 2.0
 * com cabeçalho Content-Length. Mensagens enviadas antes do "initialize"
 * terminar ficam na fila.
 */
class LspClient : public QObject
{
    Q_OBJECT
public:
    using Callback = std::function<void(const QJsonValue &result)>;

    LspClient(const QString &command, const QString &rootPath, const QJsonObject &initOptions,
              QObject *parent = nullptr);
    ~LspClient() override;

    const QString &command() const { return m_command; }
    bool start();
    void shutdown();
    bool isReady() const { return m_ready; }

    void request(const QString &method, const QJsonObject &params, Callback callback);
    void notify(const QString &method, const QJsonObject &params);

    bool hasCompletion() const { return m_completion; }
    const QStringList &completionTriggers() const { return m_triggers; }

    /** Junta um cabeçalho LSP a um corpo JSON (exposto para os testes). */
    static QByteArray frame(const QJsonObject &message);
    /** Extrai mensagens completas de buffer; o que sobra continua nele. */
    static QList<QJsonObject> unframe(QByteArray &buffer);

Q_SIGNALS:
    void ready();
    void diagnosticsPublished(const QString &uri, const QJsonArray &diagnostics);
    void failed(const QString &reason);
    void exited();

private:
    void send(const QJsonObject &message);
    void onReadyRead();
    void handle(const QJsonObject &message);

    QString m_command;
    QString m_rootPath;
    QJsonObject m_initOptions;
    QProcess *m_process = nullptr;
    QByteArray m_buffer;
    int m_nextId = 1;
    QHash<int, Callback> m_pending;
    QList<QJsonObject> m_queue;
    bool m_ready = false;
    bool m_shuttingDown = false;
    bool m_completion = false;
    QStringList m_triggers;
};
