#include "LspClient.h"

#include <QCoreApplication>
#include <QDir>
#include <QJsonDocument>
#include <QProcess>
#include <QUrl>

LspClient::LspClient(const QString &command, const QString &rootPath, const QJsonObject &initOptions,
                     QObject *parent)
    : QObject(parent)
    , m_command(command)
    , m_rootPath(rootPath)
    , m_initOptions(initOptions)
{
}

LspClient::~LspClient()
{
    if (m_process && m_process->state() != QProcess::NotRunning) {
        m_process->disconnect(this);
        m_process->kill();
        m_process->waitForFinished(500);
    }
}

QByteArray LspClient::frame(const QJsonObject &message)
{
    const QByteArray body = QJsonDocument(message).toJson(QJsonDocument::Compact);
    return "Content-Length: " + QByteArray::number(body.size()) + "\r\n\r\n" + body;
}

QList<QJsonObject> LspClient::unframe(QByteArray &buffer)
{
    QList<QJsonObject> out;
    while (true) {
        const int headerEnd = buffer.indexOf("\r\n\r\n");
        if (headerEnd < 0)
            break;
        int length = -1;
        for (const QByteArray &line : buffer.left(headerEnd).split('\n')) {
            const QByteArray l = line.trimmed();
            if (l.toLower().startsWith("content-length:"))
                length = l.mid(15).trimmed().toInt();
        }
        if (length < 0) {
            buffer.remove(0, headerEnd + 4); // cabeçalho inválido: descarta
            continue;
        }
        if (buffer.size() < headerEnd + 4 + length)
            break;
        const QByteArray body = buffer.mid(headerEnd + 4, length);
        buffer.remove(0, headerEnd + 4 + length);
        const QJsonDocument doc = QJsonDocument::fromJson(body);
        if (doc.isObject())
            out << doc.object();
    }
    return out;
}

bool LspClient::start()
{
    QStringList parts = QProcess::splitCommand(m_command);
    if (parts.isEmpty())
        return false;
    const QString program = parts.takeFirst();

    m_process = new QProcess(this);
    // stderr do servidor é só log; descartá-lo evita encher o pipe.
    m_process->setStandardErrorFile(QProcess::nullDevice());
    m_process->setWorkingDirectory(m_rootPath);
    connect(m_process, &QProcess::readyReadStandardOutput, this, &LspClient::onReadyRead);
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart)
            Q_EMIT failed(tr("Could not start “%1”").arg(m_command));
    });
    connect(m_process, &QProcess::finished, this, [this]() {
        m_ready = false;
        if (!m_shuttingDown)
            Q_EMIT exited();
    });
    m_process->start(program, parts);
    if (!m_process->waitForStarted(3000))
        return false;

    const QJsonObject capabilities{
        {"textDocument", QJsonObject{
             {"synchronization", QJsonObject{{"didSave", true}, {"dynamicRegistration", false}}},
             {"publishDiagnostics", QJsonObject{{"relatedInformation", false}}},
             {"completion", QJsonObject{
                  {"completionItem", QJsonObject{{"snippetSupport", false}, {"insertReplaceSupport", false}}},
                  {"contextSupport", true}}},
             {"definition", QJsonObject{{"dynamicRegistration", false}, {"linkSupport", true}}},
             {"hover", QJsonObject{
                  {"dynamicRegistration", false},
                  {"contentFormat", QJsonArray{"plaintext", "markdown"}}}},
         }},
        {"workspace", QJsonObject{{"configuration", true}, {"workspaceFolders", true}}},
        {"general", QJsonObject{{"positionEncodings", QJsonArray{"utf-16"}}}},
    };
    const QString rootUri = QUrl::fromLocalFile(m_rootPath).toString();
    QJsonObject params{
        {"processId", int(QCoreApplication::applicationPid())},
        {"clientInfo", QJsonObject{{"name", "Slayer Editor"}}},
        {"rootUri", rootUri},
        {"rootPath", m_rootPath},
        {"workspaceFolders", QJsonArray{QJsonObject{{"uri", rootUri}, {"name", QDir(m_rootPath).dirName()}}}},
        {"capabilities", capabilities},
    };
    if (!m_initOptions.isEmpty())
        params["initializationOptions"] = m_initOptions;

    // initialize vai direto (não entra na fila); o resto espera o "initialized".
    const int id = m_nextId++;
    m_pending.insert(id, [this](const QJsonValue &result) {
        const QJsonObject caps = result.toObject().value("capabilities").toObject();
        const QJsonValue completion = caps.value("completionProvider");
        m_completion = completion.isObject() || completion.toBool();
        const QJsonValue definition = caps.value("definitionProvider");
        m_definition = definition.isObject() || definition.toBool();
        const QJsonValue hover = caps.value("hoverProvider");
        m_hover = hover.isObject() || hover.toBool();
        m_triggers.clear();
        for (const QJsonValue &t : completion.toObject().value("triggerCharacters").toArray())
            m_triggers << t.toString();
        send(QJsonObject{{"jsonrpc", "2.0"}, {"method", "initialized"}, {"params", QJsonObject{}}});
        m_ready = true;
        const QList<QJsonObject> queued = m_queue;
        m_queue.clear();
        for (const QJsonObject &m : queued)
            send(m);
        Q_EMIT ready();
    });
    send(QJsonObject{{"jsonrpc", "2.0"}, {"id", id}, {"method", "initialize"}, {"params", params}});
    return true;
}

void LspClient::shutdown()
{
    if (!m_process || m_process->state() == QProcess::NotRunning)
        return;
    m_shuttingDown = true;
    if (m_ready) {
        request("shutdown", {}, [this](const QJsonValue &) {
            notify("exit", {});
            if (m_process)
                m_process->closeWriteChannel();
        });
    } else {
        m_process->kill();
    }
}

void LspClient::send(const QJsonObject &message)
{
    if (m_process && m_process->state() == QProcess::Running)
        m_process->write(frame(message));
}

void LspClient::request(const QString &method, const QJsonObject &params, Callback callback)
{
    const int id = m_nextId++;
    if (callback)
        m_pending.insert(id, std::move(callback));
    const QJsonObject msg{{"jsonrpc", "2.0"}, {"id", id}, {"method", method}, {"params", params}};
    if (m_ready)
        send(msg);
    else
        m_queue << msg;
}

void LspClient::notify(const QString &method, const QJsonObject &params)
{
    const QJsonObject msg{{"jsonrpc", "2.0"}, {"method", method}, {"params", params}};
    if (m_ready || method == QLatin1String("exit"))
        send(msg);
    else
        m_queue << msg;
}

void LspClient::onReadyRead()
{
    m_buffer += m_process->readAllStandardOutput();
    for (const QJsonObject &m : unframe(m_buffer))
        handle(m);
}

void LspClient::handle(const QJsonObject &message)
{
    const QString method = message.value("method").toString();
    const QJsonValue id = message.value("id");

    if (method.isEmpty()) {
        // Resposta a uma requisição nossa.
        const auto cb = m_pending.take(id.toInt());
        if (cb)
            cb(message.contains("error") ? QJsonValue() : message.value("result"));
        return;
    }
    if (!id.isUndefined()) {
        // Requisição do servidor: responde o mínimo que os servidores esperam.
        QJsonValue result;
        if (method == QLatin1String("workspace/configuration")) {
            QJsonArray items;
            const int n = message.value("params").toObject().value("items").toArray().size();
            for (int i = 0; i < n; ++i)
                items.append(QJsonValue());
            result = items;
        }
        send(QJsonObject{{"jsonrpc", "2.0"}, {"id", id}, {"result", result}});
        return;
    }
    if (method == QLatin1String("textDocument/publishDiagnostics")) {
        const QJsonObject p = message.value("params").toObject();
        Q_EMIT diagnosticsPublished(p.value("uri").toString(), p.value("diagnostics").toArray());
    }
}
