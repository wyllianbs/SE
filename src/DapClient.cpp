#include "DapClient.h"
#include "LspClient.h"

#include <QProcess>

DapClient::DapClient(QObject *parent)
    : QObject(parent)
{
}

DapClient::~DapClient()
{
    if (m_process && m_process->state() != QProcess::NotRunning) {
        m_process->disconnect(this);
        m_process->kill();
        m_process->waitForFinished(500);
    }
}

bool DapClient::start(const QString &program, const QStringList &arguments, const QString &workingDir)
{
    m_process = new QProcess(this);
    m_process->setStandardErrorFile(QProcess::nullDevice());
    m_process->setWorkingDirectory(workingDir);
    connect(m_process, &QProcess::readyReadStandardOutput, this, &DapClient::onReadyRead);
    connect(m_process, &QProcess::finished, this, &DapClient::exited);
    m_process->start(program, arguments);
    return m_process->waitForStarted(3000);
}

void DapClient::stop()
{
    if (m_process && m_process->state() != QProcess::NotRunning) {
        m_process->terminate();
        if (!m_process->waitForFinished(1000))
            m_process->kill();
    }
}

bool DapClient::isRunning() const
{
    return m_process && m_process->state() == QProcess::Running;
}

void DapClient::send(QJsonObject message)
{
    message[QStringLiteral("seq")] = m_seq++;
    if (isRunning())
        m_process->write(LspClient::frame(message));
}

void DapClient::request(const QString &command, const QJsonObject &arguments, Callback callback)
{
    const int seq = m_seq;
    if (callback)
        m_pending.insert(seq, std::move(callback));
    QJsonObject msg{{"type", "request"}, {"command", command}};
    if (!arguments.isEmpty())
        msg[QStringLiteral("arguments")] = arguments;
    send(msg);
}

void DapClient::onReadyRead()
{
    m_buffer += m_process->readAllStandardOutput();
    for (const QJsonObject &m : LspClient::unframe(m_buffer))
        handle(m);
}

void DapClient::handle(const QJsonObject &message)
{
    const QString type = message.value("type").toString();
    if (type == QLatin1String("response")) {
        const auto cb = m_pending.take(message.value("request_seq").toInt());
        if (cb)
            cb(message.value("success").toBool(), message.value("body").toObject(), message.value("message").toString());
    } else if (type == QLatin1String("event")) {
        Q_EMIT event(message.value("event").toString(), message.value("body").toObject());
    } else if (type == QLatin1String("request")) {
        // Pedido reverso (runInTerminal, startDebugging…).
        const QString command = message.value("command").toString();
        QJsonObject response{{"type", "response"}, {"request_seq", message.value("seq")}, {"command", command}};
        if (m_reverse && command == QLatin1String("runInTerminal")) {
            response[QStringLiteral("success")] = true;
            response[QStringLiteral("body")] = m_reverse(command, message.value("arguments").toObject());
        } else {
            response[QStringLiteral("success")] = false;
            response[QStringLiteral("message")] = QStringLiteral("not supported");
        }
        send(response);
    }
}
