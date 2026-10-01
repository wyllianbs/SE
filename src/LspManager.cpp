#include "LspManager.h"
#include "Document.h"
#include "LspClient.h"

#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTextDocument>
#include <QTimer>
#include <QUrl>

namespace {
constexpr int kMaxFailures = 3;
}

LspManager::LspManager(QObject *parent)
    : QObject(parent)
{
}

LspManager::~LspManager()
{
    for (const auto &c : std::as_const(m_clients)) {
        if (c)
            c->shutdown();
    }
}

QStringList LspManager::languages()
{
    return {"c", "cpp", "python", "javascript", "rust", "go", "java", "bash", "ruby", "perl", "lua", "php", "r",
            "haskell", "fortran"};
}

QList<LspPreset> LspManager::presets(const QString &id)
{
    if (id == QLatin1String("c") || id == QLatin1String("cpp"))
        return {{"clangd", "clangd --background-index", "sudo apt install clangd"}};
    if (id == QLatin1String("python"))
        return {{"pylsp", "pylsp", "sudo apt install python3-pylsp python3-pyflakes python3-pycodestyle"},
                {"Pyright", "pyright-langserver --stdio", "npm install -g pyright"},
                {"Ruff", "ruff server", "pipx install ruff"}};
    if (id == QLatin1String("javascript"))
        return {{"typescript-language-server", "typescript-language-server --stdio",
                 "npm install -g typescript typescript-language-server"}};
    if (id == QLatin1String("rust"))
        return {{"rust-analyzer", "rust-analyzer", "rustup component add rust-analyzer"}};
    if (id == QLatin1String("go"))
        return {{"gopls", "gopls", "sudo apt install gopls"}};
    if (id == QLatin1String("java"))
        return {{"jdtls", "jdtls", "sudo apt install jdtls"}};
    if (id == QLatin1String("bash"))
        return {{"bash-language-server", "bash-language-server start", "npm install -g bash-language-server"}};
    if (id == QLatin1String("ruby"))
        return {{"Solargraph", "solargraph stdio", "gem install --user-install solargraph"},
                {"Ruby LSP", "ruby-lsp", "gem install --user-install ruby-lsp"}};
    if (id == QLatin1String("perl"))
        return {{"Perl Navigator", "perlnavigator --stdio", "npm install -g perlnavigator-server"}};
    if (id == QLatin1String("lua"))
        return {{"lua-language-server", "lua-language-server", "sudo apt install lua-language-server"}};
    if (id == QLatin1String("php"))
        return {{"Intelephense", "intelephense --stdio", "npm install -g intelephense"},
                {"Phpactor", "phpactor language-server", "composer global require phpactor/phpactor"}};
    if (id == QLatin1String("r"))
        return {{"languageserver", "R --slave -e languageserver::run()",
                 "sudo apt install r-cran-languageserver"}};
    if (id == QLatin1String("haskell"))
        return {{"haskell-language-server", "haskell-language-server-wrapper --lsp", "ghcup install hls"}};
    if (id == QLatin1String("fortran"))
        return {{"fortls", "fortls", "pipx install fortls"}};
    return {};
}

bool LspManager::isInstalled(const QString &command)
{
    const QStringList args = QProcess::splitCommand(command);
    return !args.isEmpty() && !QStandardPaths::findExecutable(args.first()).isEmpty();
}

QString LspManager::lspLanguageId(const QString &id)
{
    if (id == QLatin1String("bash"))
        return QStringLiteral("shellscript");
    return id; // c, cpp, python, javascript, rust, go, java
}

/** Raiz do projeto: repositório git que contém o arquivo, senão a pasta dele. */
QString LspManager::rootFor(const QString &filePath)
{
    QDir dir = QFileInfo(filePath).absoluteDir();
    const QString fallback = dir.absolutePath();
    for (int i = 0; i < 64; ++i) {
        if (QFileInfo::exists(dir.filePath(QStringLiteral(".git")))
            || QFileInfo::exists(dir.filePath(QStringLiteral("compile_commands.json"))))
            return dir.absolutePath();
        if (!dir.cdUp())
            break;
    }
    return fallback;
}

void LspManager::setSettings(const AppSettings &settings)
{
    const AppSettings old = m_settings;
    m_settings = settings;

    // Servidor desligado ou trocado: encerra e reassocia os documentos.
    const QStringList ids = m_clients.keys();
    for (const QString &id : ids) {
        LspClient *c = m_clients.value(id);
        if (!c || !m_settings.lspEnabled || !m_settings.lspConfig(id).enabled || c->command() != commandFor(id)
            || (id.startsWith(QLatin1Char('c')) && old.languageConfig(id).flags != m_settings.languageConfig(id).flags)) {
            m_clients.remove(id);
            if (c) {
                c->shutdown();
                c->deleteLater();
            }
        }
    }
    m_failures.clear();
    const QList<Document *> docs = m_docs.keys();
    for (Document *d : docs)
        attach(d);
    Q_EMIT statusChanged();
}

QString LspManager::commandFor(const QString &id) const
{
    return m_settings.lspConfig(id).command.trimmed();
}

LspClient *LspManager::clientFor(const QString &id, const QString &rootPath)
{
    if (LspClient *c = m_clients.value(id))
        return c;
    const QString command = commandFor(id);
    if (command.isEmpty() || !isInstalled(command) || m_failures.value(command) >= kMaxFailures)
        return nullptr;

    QJsonObject init;
    if (id == QLatin1String("c") || id == QLatin1String("cpp")) {
        // Sem compile_commands.json, o clangd usa as flags de Settings → Languages.
        QJsonArray flags;
        for (const QString &f : QProcess::splitCommand(m_settings.languageConfig(id).flags)) {
            if (!f.startsWith(QLatin1String("-Wl,")) && !f.startsWith(QLatin1String("-l")))
                flags.append(f);
        }
        if (id == QLatin1String("cpp"))
            flags.prepend(QStringLiteral("-xc++"));
        init[QStringLiteral("fallbackFlags")] = flags;
    }

    auto *client = new LspClient(command, rootPath, init, this);
    connect(client, &LspClient::diagnosticsPublished, this, &LspManager::onDiagnostics);
    connect(client, &LspClient::ready, this, &LspManager::statusChanged);
    auto onDown = [this, id, command, client](const QString &reason) {
        m_clients.remove(id);
        const int n = m_failures.value(command) + 1;
        m_failures.insert(command, n);
        if (!reason.isEmpty())
            Q_EMIT message(reason);
        if (n >= kMaxFailures)
            Q_EMIT message(tr("%1 stopped %n time(s); disabled until the settings change.", nullptr, n).arg(command));
        client->deleteLater();
        // Documentos que dependiam dele tentam reabrir (se ainda houver tentativas).
        QList<Document *> affected;
        for (auto it = m_docs.begin(); it != m_docs.end(); ++it) {
            if (it->client == client) {
                it->client = nullptr;
                affected << it.key();
            }
        }
        QTimer::singleShot(1000, this, [this, affected]() {
            for (Document *d : affected) {
                if (m_docs.contains(d))
                    attach(d);
            }
        });
        Q_EMIT statusChanged();
    };
    connect(client, &LspClient::failed, this, onDown);
    connect(client, &LspClient::exited, this, [onDown]() { onDown(QString()); });
    if (!client->start()) {
        onDown(tr("Could not start “%1”").arg(command));
        return nullptr;
    }
    m_clients.insert(id, client);
    return client;
}

void LspManager::attach(Document *doc)
{
    const QString id = doc->language().id;
    const bool wanted = m_settings.lspEnabled && !doc->filePath().isEmpty() && languages().contains(id)
                        && m_settings.lspConfig(id).enabled;
    Tracked &t = m_docs[doc];
    const QString uri = QUrl::fromLocalFile(doc->filePath()).toString();
    LspClient *client = wanted ? clientFor(id, rootFor(doc->filePath())) : nullptr;

    if (t.client && (t.client != client || t.uri != uri)) {
        t.client->notify(QStringLiteral("textDocument/didClose"),
                         QJsonObject{{"textDocument", QJsonObject{{"uri", t.uri}}}});
        t.client = nullptr;
        doc->setDiagnostics({});
    }
    if (!t.timer) {
        t.timer = new QTimer(this);
        t.timer->setSingleShot(true);
        t.timer->setInterval(300);
        connect(t.timer, &QTimer::timeout, this, [this, doc]() { sendChange(doc); });
        connect(doc->text(), &QTextDocument::contentsChanged, t.timer, qOverload<>(&QTimer::start));
        connect(doc, &QObject::destroyed, this, [this, doc]() {
            Tracked gone = m_docs.take(doc);
            if (gone.client)
                gone.client->notify(QStringLiteral("textDocument/didClose"),
                                    QJsonObject{{"textDocument", QJsonObject{{"uri", gone.uri}}}});
            if (gone.timer)
                gone.timer->deleteLater();
        });
    }
    if (client && t.client != client) {
        t.client = client;
        t.uri = uri;
        t.version = 1;
        client->notify(QStringLiteral("textDocument/didOpen"),
                       QJsonObject{{"textDocument", QJsonObject{{"uri", uri},
                                                                {"languageId", lspLanguageId(id)},
                                                                {"version", t.version},
                                                                {"text", doc->text()->toPlainText()}}}});
    }
    Q_EMIT statusChanged();
}

void LspManager::detach(Document *doc)
{
    Tracked t = m_docs.take(doc);
    if (t.client)
        t.client->notify(QStringLiteral("textDocument/didClose"),
                         QJsonObject{{"textDocument", QJsonObject{{"uri", t.uri}}}});
    if (t.timer)
        t.timer->deleteLater();
}

void LspManager::sendChange(Document *doc)
{
    auto it = m_docs.find(doc);
    if (it == m_docs.end() || !it->client)
        return;
    it->timer->stop();
    ++it->version;
    // Envio do texto completo: válido para servidores Full e Incremental.
    it->client->notify(QStringLiteral("textDocument/didChange"),
                       QJsonObject{{"textDocument", QJsonObject{{"uri", it->uri}, {"version", it->version}}},
                                   {"contentChanges", QJsonArray{QJsonObject{{"text", doc->text()->toPlainText()}}}}});
}

void LspManager::documentSaved(Document *doc)
{
    // Salvar com outro nome ou mudar a extensão troca uri e linguagem.
    attach(doc);
    auto it = m_docs.find(doc);
    if (it == m_docs.end() || !it->client)
        return;
    if (it->timer->isActive())
        sendChange(doc);
    it->client->notify(QStringLiteral("textDocument/didSave"),
                       QJsonObject{{"textDocument", QJsonObject{{"uri", it->uri}}}});
}

void LspManager::complete(Document *doc, int line, int column, CompletionCallback callback)
{
    auto it = m_docs.find(doc);
    if (it == m_docs.end() || !it->client || !it->client->hasCompletion()) {
        callback({});
        return;
    }
    if (it->timer->isActive())
        sendChange(doc); // o servidor precisa do texto atual
    const QJsonObject params{{"textDocument", QJsonObject{{"uri", it->uri}}},
                             {"position", QJsonObject{{"line", line}, {"character", column}}}};
    it->client->request(QStringLiteral("textDocument/completion"), params, [callback](const QJsonValue &result) {
        const QJsonArray items = result.isArray() ? result.toArray() : result.toObject().value("items").toArray();
        QList<LspCompletion> out;
        for (const QJsonValue &v : items) {
            const QJsonObject o = v.toObject();
            QString insert = o.value("textEdit").toObject().value("newText").toString();
            if (insert.isEmpty())
                insert = o.value("insertText").toString();
            QString label = o.value("label").toString().trimmed();
            if (insert.isEmpty())
                insert = label;
            // insertFormat 2 = snippet: remove marcadores ${1:x} / $0.
            if (o.value("insertTextFormat").toInt() == 2) {
                static const QRegularExpression placeholder(QStringLiteral("\\$\\{\\d+:([^}]*)\\}|\\$\\d+"));
                insert.replace(placeholder, QStringLiteral("\\1"));
            }
            out.append({label, insert.trimmed(), o.value("detail").toString()});
        }
        callback(out);
    });
}

QStringList LspManager::triggerCharacters(Document *doc) const
{
    const auto it = m_docs.constFind(doc);
    return (it != m_docs.cend() && it->client) ? it->client->completionTriggers() : QStringList();
}

QString LspManager::statusFor(Document *doc) const
{
    const auto it = m_docs.constFind(doc);
    if (it == m_docs.cend() || !it->client)
        return QString();
    const QString name = QProcess::splitCommand(it->client->command()).value(0);
    return it->client->isReady() ? name : tr("%1 — starting").arg(name);
}

void LspManager::onDiagnostics(const QString &uri, const QJsonArray &list)
{
    for (auto it = m_docs.begin(); it != m_docs.end(); ++it) {
        if (it->uri != uri)
            continue;
        QList<Diagnostic> diags;
        for (const QJsonValue &v : list) {
            const QJsonObject o = v.toObject();
            const QJsonObject range = o.value("range").toObject();
            const QJsonObject start = range.value("start").toObject();
            const QJsonObject end = range.value("end").toObject();
            Diagnostic d;
            d.line = start.value("line").toInt();
            d.column = start.value("character").toInt();
            d.endLine = end.value("line").toInt();
            d.endColumn = end.value("character").toInt();
            d.severity = o.value("severity").toInt(1);
            d.message = o.value("message").toString();
            d.source = o.value("source").toString();
            diags << d;
        }
        it.key()->setDiagnostics(diags);
    }
}
