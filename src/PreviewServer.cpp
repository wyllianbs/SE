#include "PreviewServer.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUrl>

namespace {

QByteArray contentType(const QString &path)
{
    const QString ext = QFileInfo(path).suffix().toLower();
    static const QHash<QString, QByteArray> types{
        {"html", "text/html; charset=utf-8"}, {"htm", "text/html; charset=utf-8"},
        {"css", "text/css; charset=utf-8"}, {"js", "application/javascript; charset=utf-8"},
        {"mjs", "application/javascript; charset=utf-8"}, {"json", "application/json; charset=utf-8"},
        {"svg", "image/svg+xml"}, {"png", "image/png"}, {"jpg", "image/jpeg"}, {"jpeg", "image/jpeg"},
        {"gif", "image/gif"}, {"webp", "image/webp"}, {"ico", "image/x-icon"},
        {"woff", "font/woff"}, {"woff2", "font/woff2"}, {"ttf", "font/ttf"},
        {"txt", "text/plain; charset=utf-8"}, {"xml", "application/xml; charset=utf-8"},
        {"wasm", "application/wasm"}, {"map", "application/json; charset=utf-8"},
    };
    return types.value(ext, QByteArrayLiteral("application/octet-stream"));
}

void writeResponse(QTcpSocket *sock, int code, const QByteArray &status, const QByteArray &type,
                   const QByteArray &body)
{
    QByteArray head = "HTTP/1.1 " + QByteArray::number(code) + ' ' + status + "\r\n";
    head += "Content-Type: " + type + "\r\n";
    head += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
    head += "Cache-Control: no-store\r\n";
    head += "Connection: close\r\n\r\n";
    sock->write(head);
    sock->write(body);
    sock->flush();
    sock->disconnectFromHost();
}

} // namespace

PreviewServer::PreviewServer(QObject *parent)
    : QObject(parent)
{
}

PreviewServer::~PreviewServer()
{
    stop();
}

bool PreviewServer::start(const QString &rootDir, const QString &host, quint16 port)
{
    stop();
    m_root = QDir(rootDir).absolutePath();
    m_host = host.isEmpty() ? QStringLiteral("127.0.0.1") : host;
    m_server = new QTcpServer(this);
    connect(m_server, &QTcpServer::newConnection, this, &PreviewServer::onNewConnection);
    const QHostAddress addr(m_host);
    if (!m_server->listen(addr.isNull() ? QHostAddress::LocalHost : addr, port)) {
        delete m_server;
        m_server = nullptr;
        return false;
    }
    m_port = m_server->serverPort();
    return true;
}

void PreviewServer::stop()
{
    if (m_intervalTimer) {
        m_intervalTimer->stop();
    }
    for (auto it = m_buffers.cbegin(); it != m_buffers.cend(); ++it)
        it.key()->deleteLater();
    m_buffers.clear();
    if (m_server) {
        m_server->close();
        m_server->deleteLater();
        m_server = nullptr;
    }
    m_port = 0;
}

bool PreviewServer::isRunning() const
{
    return m_server && m_server->isListening();
}

void PreviewServer::setReload(ReloadMode mode, int intervalMs)
{
    m_mode = mode;
    m_intervalMs = qMax(200, intervalMs);
    if (mode == Interval) {
        if (!m_intervalTimer) {
            m_intervalTimer = new QTimer(this);
            connect(m_intervalTimer, &QTimer::timeout, this, &PreviewServer::triggerReload);
        }
        m_intervalTimer->start(m_intervalMs);
    } else if (m_intervalTimer) {
        m_intervalTimer->stop();
    }
}

void PreviewServer::setBackend(const QString &host, quint16 port)
{
    m_backendHost = host.isEmpty() ? QStringLiteral("127.0.0.1") : host;
    m_backendPort = port;
}

void PreviewServer::triggerReload()
{
    ++m_token; // a próxima sondagem dos clientes vê a mudança e recarrega
}

QUrl PreviewServer::urlForFile(const QString &filePath) const
{
    if (!isRunning())
        return {};
    const QString abs = QFileInfo(filePath).absoluteFilePath();
    const QString rel = QDir(m_root).relativeFilePath(abs);
    if (rel.startsWith(QLatin1String("..")))
        return {};
    return QUrl(QStringLiteral("http://%1:%2/%3").arg(m_host).arg(m_port).arg(QString::fromUtf8(QUrl::toPercentEncoding(rel, "/"))));
}

void PreviewServer::onNewConnection()
{
    while (m_server && m_server->hasPendingConnections()) {
        QTcpSocket *sock = m_server->nextPendingConnection();
        m_buffers.insert(sock, QByteArray());
        connect(sock, &QTcpSocket::readyRead, this, [this, sock]() {
            QByteArray &buf = m_buffers[sock];
            buf += sock->readAll();
            if (buf.indexOf("\r\n\r\n") < 0 && buf.size() < 65536)
                return; // aguarda o fim dos cabeçalhos
            const QByteArray request = buf;
            disconnect(sock, &QTcpSocket::readyRead, this, nullptr); // processa uma vez
            serve(sock, request);
        });
        connect(sock, &QTcpSocket::disconnected, this, [this, sock]() {
            m_buffers.remove(sock);
            sock->deleteLater();
        });
    }
}

void PreviewServer::serve(QTcpSocket *sock, const QByteArray &request)
{
    const int eol = request.indexOf("\r\n");
    const QByteArray requestLine = request.left(eol < 0 ? request.size() : eol);
    const QList<QByteArray> parts = requestLine.split(' ');
    if (parts.size() < 2 || parts.at(0) != "GET") {
        writeResponse(sock, 405, "Method Not Allowed", "text/plain", "Only GET\n");
        return;
    }
    QByteArray target = parts.at(1);
    const int q = target.indexOf('?');
    if (q >= 0)
        target = target.left(q);
    QString path = QUrl::fromPercentEncoding(target);

    if (path == QLatin1String("/__slayer_live__")) {
        writeResponse(sock, 200, "OK", "text/plain", QByteArray::number(m_token));
        return;
    }

    if (m_backendPort != 0) { // modo proxy: encaminha ao servidor externo
        proxyTo(sock, request);
        return;
    }

    if (path.endsWith(QLatin1Char('/')))
        path += QStringLiteral("index.html");
    // Resolve dentro do root (sem permitir sair dele).
    const QString abs = QDir::cleanPath(m_root + path);
    if (!abs.startsWith(m_root)) {
        writeResponse(sock, 403, "Forbidden", "text/plain", "Forbidden\n");
        return;
    }
    QByteArray body;
    bool have = m_provider && m_provider(abs, body); // conteúdo ao vivo do editor
    if (!have) {
        QFile file(abs);
        if (!QFileInfo(abs).isFile() || !file.open(QIODevice::ReadOnly)) {
            writeResponse(sock, 404, "Not Found", "text/plain", "Not Found\n");
            return;
        }
        body = file.readAll();
    }
    const QByteArray type = contentType(abs);
    if (m_mode != Never && type.startsWith("text/html"))
        body = injectReload(body);
    writeResponse(sock, 200, "OK", type, body);
}

static QByteArray withContentLength(const QByteArray &headers, int len)
{
    QByteArray out;
    bool set = false;
    for (const QByteArray &raw : headers.split('\n')) {
        QByteArray l = raw;
        if (l.endsWith('\r'))
            l.chop(1);
        if (l.isEmpty())
            continue;
        if (l.toLower().startsWith("content-length:")) {
            out += "Content-Length: " + QByteArray::number(len) + "\r\n";
            set = true;
        } else {
            out += l + "\r\n";
        }
    }
    if (!set)
        out += "Content-Length: " + QByteArray::number(len) + "\r\n";
    if (out.endsWith("\r\n"))
        out.chop(2);
    return out;
}

// Encaminha a requisição ao backend (ex.: php -S) e injeta o live-reload nas
// respostas HTML. Síncrono: o backend é local e responde em milissegundos.
void PreviewServer::proxyTo(QTcpSocket *client, const QByteArray &request)
{
    QTcpSocket back;
    back.connectToHost(m_backendHost, m_backendPort);
    if (!back.waitForConnected(2000)) {
        writeResponse(client, 502, "Bad Gateway", "text/plain", "Preview backend not reachable\n");
        return;
    }
    // Força "Connection: close" para saber quando a resposta termina.
    QByteArray req = request;
    const int he = req.indexOf("\r\n\r\n");
    if (he >= 0)
        req.insert(he, "\r\nConnection: close");
    else
        req += "\r\nConnection: close\r\n\r\n";
    back.write(req);
    back.waitForBytesWritten(2000);
    QByteArray resp;
    while (back.state() == QAbstractSocket::ConnectedState && back.waitForReadyRead(5000))
        resp += back.readAll();
    resp += back.readAll();
    if (resp.isEmpty()) {
        writeResponse(client, 502, "Bad Gateway", "text/plain", "Empty response from backend\n");
        return;
    }
    const int rhe = resp.indexOf("\r\n\r\n");
    if (rhe < 0) {
        client->write(resp);
        client->flush();
        client->disconnectFromHost();
        return;
    }
    QByteArray headers = resp.left(rhe);
    QByteArray body = resp.mid(rhe + 4);
    const QByteArray lower = headers.toLower();
    const bool isHtml = lower.contains("content-type: text/html");
    const bool chunked = lower.contains("transfer-encoding: chunked");
    if (m_mode != Never && isHtml && !chunked) {
        body = injectReload(body);
        headers = withContentLength(headers, body.size());
    }
    client->write(headers + "\r\n\r\n" + body);
    client->flush();
    client->disconnectFromHost();
}

QByteArray PreviewServer::injectReload(const QByteArray &html) const
{
    // Cliente: sonda /__slayer_live__ e recarrega quando o token muda.
    const QByteArray script =
        "\n<script>(function(){var t=null;function p(){fetch('/__slayer_live__',{cache:'no-store'})"
        ".then(function(r){return r.text();}).then(function(v){if(t===null){t=v;}else if(v!==t){"
        "location.reload();}}).catch(function(){});}setInterval(p,600);p();})();</script>\n";
    QByteArray out = html;
    const int pos = out.lastIndexOf("</body>");
    if (pos >= 0)
        out.insert(pos, script);
    else
        out += script;
    return out;
}
