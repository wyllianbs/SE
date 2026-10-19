#include "PreviewServer.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QPointer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUrl>

#include <memory>

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

// Cabeçalhos que ativam o isolamento de origem cruzada (habilita
// SharedArrayBuffer/Atomics — necessário para apps WASM com threads, como
// compiladores C/Python no navegador).
const QByteArray kCoiHeaders =
    "Cross-Origin-Opener-Policy: same-origin\r\n"
    "Cross-Origin-Embedder-Policy: require-corp\r\n"
    "Cross-Origin-Resource-Policy: cross-origin\r\n";

void writeResponse(QTcpSocket *sock, int code, const QByteArray &status, const QByteArray &type,
                   const QByteArray &body, const QByteArray &extra = {}, const QByteArray &cacheControl = {})
{
    QByteArray head = "HTTP/1.1 " + QByteArray::number(code) + ' ' + status + "\r\n";
    head += "Content-Type: " + type + "\r\n";
    head += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
    head += "Cache-Control: " + (cacheControl.isEmpty() ? QByteArray("no-store") : cacheControl) + "\r\n";
    head += extra;
    head += "Connection: close\r\n\r\n";
    sock->write(head);
    sock->write(body);
    // Sem flush() bloqueante: disconnectFromHost() fecha depois que os dados
    // bufferizados forem enviados pelo laço de eventos (serve arquivos grandes,
    // ex.: WASM, sem travar a interface).
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
    m_sseClients.clear();
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
    ++m_token;
    // Notifica todos os clientes SSE conectados; sem polling, sem delay.
    const QByteArray event = "data: reload\n\n";
    for (QTcpSocket *s : std::as_const(m_sseClients))
        s->write(event);
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
            m_sseClients.removeOne(sock);
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

    if (path == QLatin1String("/__slayer_live_sse__")) {
        // Mantém a conexão aberta para Server-Sent Events; sem polling.
        const QByteArray headers =
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/event-stream\r\n"
            "Cache-Control: no-cache\r\n"
            "Connection: keep-alive\r\n"
            + (m_coi ? kCoiHeaders : QByteArray())
            + "\r\n";
        sock->write(headers);
        sock->write("data: connected\n\n"); // confirma que o canal está aberto
        m_sseClients.append(sock);
        return;
    }

    if (m_backendPort != 0) { // modo proxy: encaminha ao servidor externo
        proxyTo(sock, request);
        return;
    }

    if (!path.startsWith(QLatin1Char('/')))
        path.prepend(QLatin1Char('/'));
    if (path.endsWith(QLatin1Char('/')))
        path += QStringLiteral("index.html");
    // Resolve dentro do root (sem permitir sair dele). O "/" final evita que
    // "/../site-x/…" passe por "/…/site" (prefixo igual, outra pasta).
    const QString abs = QDir::cleanPath(m_root + path);
    const QString rootSlash = m_root.endsWith(QLatin1Char('/')) ? m_root : m_root + QLatin1Char('/');
    if (!abs.startsWith(rootSlash)) {
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
    // Fontes (html/css/js/json/xml/texto) ficam sempre frescas para o live-reload;
    // binários pesados (wasm, .data, octet-stream, imagens, fontes) podem ser
    // cacheados por pouco tempo, para não rebaixar cada recarga a um novo
    // download de vários MB (ex.: runtimes WASM).
    const bool source = type.startsWith("text/") || type.startsWith("application/javascript")
                        || type.startsWith("application/json") || type.startsWith("application/xml");
    const QByteArray cache = source ? QByteArray() : QByteArrayLiteral("max-age=30");
    writeResponse(sock, 200, "OK", type, body, m_coi ? kCoiHeaders : QByteArray(), cache);
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
// respostas HTML. Totalmente assíncrono: nenhuma espera bloqueante no laço de
// eventos, para que a interface não trave enquanto o backend responde.
void PreviewServer::proxyTo(QTcpSocket *client, const QByteArray &request)
{
    QPointer<QTcpSocket> clientPtr(client);
    auto *back = new QTcpSocket(this);
    auto *timer = new QTimer(this);
    timer->setSingleShot(true);
    auto resp = std::make_shared<QByteArray>();
    auto finished = std::make_shared<bool>(false);

    auto cleanup = [back, timer, finished]() {
        if (*finished)
            return;
        *finished = true;
        back->disconnect();
        timer->stop();
        timer->deleteLater();
        back->deleteLater();
    };

    connect(timer, &QTimer::timeout, this, [this, clientPtr, resp, cleanup]() {
        if (clientPtr) {
            if (resp->isEmpty())
                writeResponse(clientPtr, 504, "Gateway Timeout", "text/plain", "Preview backend timed out\n");
            else
                clientPtr->disconnectFromHost();
        }
        cleanup();
    });
    connect(back, &QTcpSocket::connected, this, [back, request]() {
        // Força "Connection: close" para saber quando a resposta termina.
        QByteArray req = request;
        const int he = req.indexOf("\r\n\r\n");
        if (he >= 0)
            req.insert(he, "\r\nConnection: close");
        else
            req += "\r\nConnection: close\r\n\r\n";
        back->write(req);
    });
    connect(back, &QTcpSocket::readyRead, this, [back, resp, timer]() {
        *resp += back->readAll();
        timer->start(15000); // enquanto chegam dados, adia o timeout
    });
    connect(back, &QTcpSocket::disconnected, this, [this, clientPtr, resp, cleanup]() {
        if (clientPtr)
            finishProxyResponse(clientPtr, *resp);
        cleanup();
    });
    connect(back, &QTcpSocket::errorOccurred, this,
            [this, clientPtr, resp, cleanup](QAbstractSocket::SocketError e) {
        if (e == QAbstractSocket::RemoteHostClosedError)
            return; // fim normal da resposta: tratado em disconnected
        if (resp->isEmpty() && clientPtr)
            writeResponse(clientPtr, 502, "Bad Gateway", "text/plain", "Preview backend not reachable\n");
        cleanup();
    });

    timer->start(15000);
    back->connectToHost(m_backendHost, m_backendPort);
}

void PreviewServer::finishProxyResponse(QTcpSocket *client, const QByteArray &resp)
{
    if (resp.isEmpty()) {
        writeResponse(client, 502, "Bad Gateway", "text/plain", "Empty response from backend\n");
        return;
    }
    const int rhe = resp.indexOf("\r\n\r\n");
    if (rhe < 0) {
        client->write(resp);
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
    if (m_coi) {
        // Substitui quaisquer cabeçalhos de isolamento do backend pelos nossos.
        QByteArray filtered;
        for (const QByteArray &raw : headers.split('\n')) {
            QByteArray l = raw;
            if (l.endsWith('\r'))
                l.chop(1);
            const QByteArray low = l.toLower();
            if (low.startsWith("cross-origin-opener-policy:")
                || low.startsWith("cross-origin-embedder-policy:")
                || low.startsWith("cross-origin-resource-policy:"))
                continue;
            if (!l.isEmpty())
                filtered += l + "\r\n";
        }
        if (filtered.endsWith("\r\n"))
            filtered.chop(2);
        headers = filtered + "\r\n" + kCoiHeaders.chopped(2);
    }
    client->write(headers + "\r\n\r\n" + body);
    client->disconnectFromHost();
}

QByteArray PreviewServer::injectReload(const QByteArray &html) const
{
    // SSE: o browser mantém uma conexão aberta; o servidor envia "data: reload"
    // apenas quando o conteúdo muda — zero requisições periódicas.
    const QByteArray script =
        "\n<script>(function(){var es=new EventSource('/__slayer_live_sse__');"
        "es.onmessage=function(e){if(e.data==='reload')location.reload();};"
        "})();</script>\n";
    QByteArray out = html;
    const int pos = out.lastIndexOf("</body>");
    if (pos >= 0)
        out.insert(pos, script);
    else
        out += script;
    return out;
}
