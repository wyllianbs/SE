#pragma once

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QStringList>

#include <functional>

class QProcess;

/**
 * Cliente do Debug Adapter Protocol: o adaptador (debugpy, gdb -i dap…) roda
 * como processo filho e conversa por stdin/stdout com o mesmo enquadramento
 * Content-Length do LSP.
 */
class DapClient : public QObject
{
    Q_OBJECT
public:
    /** success, body e mensagem de erro da resposta. */
    using Callback = std::function<void(bool success, const QJsonObject &body, const QString &message)>;
    /** Responde a um pedido reverso do adaptador (runInTerminal). */
    using ReverseHandler = std::function<QJsonObject(const QString &command, const QJsonObject &arguments)>;

    explicit DapClient(QObject *parent = nullptr);
    ~DapClient() override;

    bool start(const QString &program, const QStringList &arguments, const QString &workingDir);
    void stop();
    bool isRunning() const;

    void request(const QString &command, const QJsonObject &arguments = {}, Callback callback = {});
    void setReverseHandler(ReverseHandler handler) { m_reverse = std::move(handler); }

Q_SIGNALS:
    void event(const QString &name, const QJsonObject &body);
    void exited();

private:
    void onReadyRead();
    void handle(const QJsonObject &message);
    void send(QJsonObject message);

    QProcess *m_process = nullptr;
    QByteArray m_buffer;
    int m_seq = 1;
    QHash<int, Callback> m_pending;
    ReverseHandler m_reverse;
};
