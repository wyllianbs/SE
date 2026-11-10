#include "LspManager.h"
#include "Document.h"
#include "LspClient.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QTextDocument>
#include <QTimer>
#include <QUrl>
#include <QVector>

#include <memory>

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

QString LspManager::settingsExample(const QString &command)
{
    // A chave é o binário do servidor (o JSON é por servidor, não por linguagem):
    // mostra opções reais daquele servidor. Vazio = sem exemplo específico.
    QString bin = QProcess::splitCommand(command).value(0);
    const int slash = bin.lastIndexOf(QLatin1Char('/'));
    if (slash >= 0)
        bin = bin.mid(slash + 1);

    if (bin == QLatin1String("gopls"))
        return QStringLiteral("{\n  \"gopls\": {\n    \"formatting.gofumpt\": true,\n"
                              "    \"ui.inlayhint.hints\": {\n      \"parameterNames\": true\n    }\n  }\n}");
    if (bin == QLatin1String("pylsp"))
        // Sugestão para o pylsp como secundário do pyright: lint + formatação,
        // com o jedi desligado (completação/hover/definição ficam com o pyright).
        return QStringLiteral(
            "{\n"
            "  \"pylsp\": {\n"
            "    \"plugins\": {\n"
            "      \"pyflakes\":            { \"enabled\": true },\n"
            "      \"pycodestyle\":         { \"enabled\": true, \"maxLineLength\": 100 },\n"
            "      \"mccabe\":              { \"enabled\": false },\n"
            "      \"autopep8\":            { \"enabled\": true },\n"
            "      \"yapf\":                { \"enabled\": false },\n"
            "      \"jedi_completion\":     { \"enabled\": false },\n"
            "      \"jedi_hover\":          { \"enabled\": false },\n"
            "      \"jedi_definition\":     { \"enabled\": false },\n"
            "      \"jedi_signature_help\": { \"enabled\": false },\n"
            "      \"jedi_symbols\":        { \"enabled\": false }\n"
            "    }\n"
            "  }\n"
            "}");
    if (bin == QLatin1String("pyright-langserver"))
        return QStringLiteral(
            "{\n"
            "  \"python\": {\n"
            "    \"analysis\": {\n"
            "      \"typeCheckingMode\": \"basic\",\n"
            "      \"diagnosticMode\": \"openFilesOnly\",\n"
            "      \"autoImportCompletions\": true,\n"
            "      \"useLibraryCodeForTypes\": true,\n"
            "      \"inlayHints\": {\n"
            "        \"variableTypes\": true,\n"
            "        \"functionReturnTypes\": true,\n"
            "        \"callArgumentNames\": true\n"
            "      }\n"
            "    }\n"
            "  }\n"
            "}");
    if (bin == QLatin1String("ruff"))
        return QStringLiteral("{\n  \"ruff\": {\n    \"lineLength\": 88,\n"
                              "    \"lint\": {\n      \"select\": [\"E\", \"F\", \"I\"]\n    }\n  }\n}");
    if (bin == QLatin1String("rust-analyzer"))
        return QStringLiteral("{\n  \"rust-analyzer\": {\n    \"check\": { \"command\": \"clippy\" },\n"
                              "    \"cargo\": { \"features\": \"all\" }\n  }\n}");
    if (bin == QLatin1String("typescript-language-server"))
        return QStringLiteral("{\n  \"typescript\": {\n    \"inlayHints\": {\n"
                              "      \"includeInlayParameterNameHints\": \"all\"\n    }\n  }\n}");
    if (bin == QLatin1String("lua-language-server"))
        return QStringLiteral("{\n  \"Lua\": {\n    \"runtime\": { \"version\": \"LuaJIT\" },\n"
                              "    \"diagnostics\": { \"globals\": [\"vim\"] }\n  }\n}");
    if (bin == QLatin1String("intelephense"))
        return QStringLiteral("{\n  \"intelephense\": {\n    \"files\": { \"maxSize\": 5000000 }\n  }\n}");
    if (bin == QLatin1String("bash-language-server"))
        return QStringLiteral("{\n  \"bashIde\": {\n    \"globPattern\": \"**/*@(.sh|.inc|.bash)\"\n  }\n}");
    if (bin == QLatin1String("solargraph"))
        return QStringLiteral("{\n  \"solargraph\": {\n    \"diagnostics\": true,\n    \"formatting\": true\n  }\n}");
    // clangd configura-se por arquivo .clangd na raiz do projeto, não por este
    // canal (workspace/configuration); por isso não há exemplo JSON aqui.
    return QString();
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

/** Raiz: o projeto aberto, se contém o arquivo; senão o repositório git dele, ou a pasta. */
QString LspManager::rootFor(const QString &filePath) const
{
    if (!m_projectRoot.isEmpty()
        && (filePath == m_projectRoot || filePath.startsWith(m_projectRoot + QLatin1Char('/'))))
        return m_projectRoot;
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
    const bool lspRelevant = old.lspEnabled != settings.lspEnabled || old.lsp != settings.lsp
                             || old.languages != settings.languages;
    m_settings = settings;
    if (!lspRelevant)
        return;

    // Mudou algo do LSP: encerra todos os servidores (principal e secundário) e
    // reassocia os documentos. Teardown completo (em vez de comparar comando/JSON
    // por chave) cobre o slot secundário sem casos especiais, e só acontece
    // quando a configuração do LSP de fato mudou.
    restartClients();
}

void LspManager::setProjectRoot(const QString &root)
{
    if (m_projectRoot == root)
        return;
    m_projectRoot = root;
    restartClients(); // um servidor por linguagem: a raiz nova exige reiniciá-los
}

void LspManager::restartClients()
{
    const QStringList ids = m_clients.keys();
    for (const QString &id : ids) {
        if (LspClient *c = m_clients.take(id)) {
            c->shutdown();
            c->deleteLater();
        }
    }
    m_failures.clear(); // config/raiz nova: zera o contador de quedas e tenta de novo
    const QList<Document *> docs = m_docs.keys();
    for (Document *d : docs)
        attach(d);
    Q_EMIT statusChanged();
}

QString LspManager::commandFor(const QString &id) const
{
    return m_settings.lspConfig(id).command.trimmed();
}

QString LspManager::secondaryKey(const QString &id)
{
    return id + QStringLiteral("\x1f#2"); // separador improvável de colidir com um id real
}

LspClient *LspManager::clientFor(const QString &id, const QString &rootPath)
{
    return makeClient(id, id, commandFor(id), m_settings.lspConfig(id).settingsJson, rootPath);
}

LspClient *LspManager::makeClient(const QString &key, const QString &langId, const QString &command,
                                  const QString &settingsJson, const QString &rootPath)
{
    if (LspClient *c = m_clients.value(key))
        return c;
    const QString cmd = command.trimmed();
    if (cmd.isEmpty() || !isInstalled(cmd) || m_failures.value(cmd) >= kMaxFailures)
        return nullptr;

    QJsonObject init;
    if (langId == QLatin1String("c") || langId == QLatin1String("cpp")) {
        // Sem compile_commands.json, o clangd usa as flags de Settings → Languages.
        QJsonArray flags;
        for (const QString &f : QProcess::splitCommand(m_settings.languageConfig(langId).flags)) {
            if (!f.startsWith(QLatin1String("-Wl,")) && !f.startsWith(QLatin1String("-l")))
                flags.append(f);
        }
        if (langId == QLatin1String("cpp"))
            flags.prepend(QStringLiteral("-xc++"));
        init[QStringLiteral("fallbackFlags")] = flags;
    }

    // Configuração do usuário (JSON): respondida em workspace/configuration.
    QJsonObject configuration;
    const QString raw = settingsJson.trimmed();
    if (!raw.isEmpty()) {
        const QJsonDocument parsed = QJsonDocument::fromJson(raw.toUtf8());
        if (parsed.isObject())
            configuration = parsed.object();
    }

    auto *client = new LspClient(cmd, rootPath, init, configuration, this);
    connect(client, &LspClient::diagnosticsPublished, this, &LspManager::onDiagnostics);
    connect(client, &LspClient::ready, this, &LspManager::statusChanged);
    auto onDown = [this, key, cmd, client](const QString &reason) {
        m_clients.remove(key);
        const int n = m_failures.value(cmd) + 1;
        m_failures.insert(cmd, n);
        if (!reason.isEmpty())
            Q_EMIT message(reason);
        if (n >= kMaxFailures)
            Q_EMIT message(tr("%1 stopped %n time(s); disabled until the settings change.", nullptr, n).arg(cmd));
        client->deleteLater();
        // Documentos que dependiam dele largam esse slot (e seus diagnósticos) e
        // tentam reabrir (se ainda houver tentativas).
        QList<Document *> affected;
        for (auto it = m_docs.begin(); it != m_docs.end(); ++it) {
            bool hit = false;
            if (it->client == client) { it->client = nullptr; it->diag.clear(); hit = true; }
            if (it->client2 == client) { it->client2 = nullptr; it->diag2.clear(); hit = true; }
            if (hit)
                affected << it.key();
        }
        for (Document *d : affected)
            applyMerged(d);
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
        onDown(tr("Could not start “%1”").arg(cmd));
        return nullptr;
    }
    m_clients.insert(key, client);
    return client;
}

void LspManager::attach(Document *doc)
{
    const QString id = doc->language().id;
    const LspConfig cfg = m_settings.lspConfig(id);
    const bool wanted = m_settings.lspEnabled && !doc->filePath().isEmpty() && languages().contains(id)
                        && cfg.enabled;
    Tracked &t = m_docs[doc];
    LspClient *const before = t.client;   // sinaliza status só se algum anexo mudar
    LspClient *const before2 = t.client2;
    const QString uri = QUrl::fromLocalFile(doc->filePath()).toString();
    const QString root = rootFor(doc->filePath());

    LspClient *client = wanted ? clientFor(id, root) : nullptr;
    // Secundário (hoje só o Python o expõe): servidor diferente do principal.
    const QString sec = cfg.secondaryCommand.trimmed();
    LspClient *client2 = (wanted && cfg.secondaryEnabled && !sec.isEmpty() && sec != commandFor(id))
                             ? makeClient(secondaryKey(id), id, sec, cfg.secondarySettingsJson, root)
                             : nullptr;

    bool closed = false;
    if (t.client && (t.client != client || t.uri != uri)) {
        t.client->notify(QStringLiteral("textDocument/didClose"),
                         QJsonObject{{"textDocument", QJsonObject{{"uri", t.uri}}}});
        t.client = nullptr;
        t.diag.clear();
        closed = true;
    }
    if (t.client2 && (t.client2 != client2 || t.uri != uri)) {
        t.client2->notify(QStringLiteral("textDocument/didClose"),
                          QJsonObject{{"textDocument", QJsonObject{{"uri", t.uri}}}});
        t.client2 = nullptr;
        t.diag2.clear();
        closed = true;
    }
    if (closed)
        applyMerged(doc); // reflete no editor a remoção dos diagnósticos do(s) servidor(es) fechado(s)

    if (!t.timer) {
        t.timer = new QTimer(this);
        t.timer->setSingleShot(true);
        t.timer->setInterval(300);
        connect(t.timer, &QTimer::timeout, this, [this, doc]() { sendChange(doc); });
        connect(doc->text(), &QTextDocument::contentsChanged, t.timer, qOverload<>(&QTimer::start));
        connect(doc, &QObject::destroyed, this, [this, doc]() {
            Tracked gone = m_docs.take(doc);
            const QJsonObject td{{"textDocument", QJsonObject{{"uri", gone.uri}}}};
            if (gone.client)
                gone.client->notify(QStringLiteral("textDocument/didClose"), td);
            if (gone.client2)
                gone.client2->notify(QStringLiteral("textDocument/didClose"), td);
            if (gone.timer)
                gone.timer->deleteLater();
        });
    }

    t.uri = uri;
    const QString text = doc->text()->toPlainText();
    auto open = [&](LspClient *c, int &version) {
        version = 1;
        c->notify(QStringLiteral("textDocument/didOpen"),
                  QJsonObject{{"textDocument", QJsonObject{{"uri", uri},
                                                           {"languageId", lspLanguageId(id)},
                                                           {"version", version},
                                                           {"text", text}}}});
    };
    if (client && t.client != client) {
        t.client = client;
        open(client, t.version);
    }
    if (client2 && t.client2 != client2) {
        t.client2 = client2;
        open(client2, t.version2);
    }
    // Só quando o anexo mudou: evita reconfigurar todos os editores (e re-rehighlight,
    // que reentra em attach) a cada alteração do documento — isso recursava e crashava.
    if (t.client != before || t.client2 != before2)
        Q_EMIT statusChanged();
}

void LspManager::applyMerged(Document *doc)
{
    auto it = m_docs.find(doc);
    if (it == m_docs.end())
        return;
    QList<Diagnostic> all = it->diag;
    all += it->diag2;
    doc->setDiagnostics(all);
}

LspClient *LspManager::provider(const Tracked &t, bool (LspClient::*has)() const) const
{
    if (t.client && (t.client->*has)())
        return t.client; // principal tem prioridade
    if (t.client2 && (t.client2->*has)())
        return t.client2;
    return nullptr;
}

void LspManager::detach(Document *doc)
{
    Tracked t = m_docs.take(doc);
    const QJsonObject td{{"textDocument", QJsonObject{{"uri", t.uri}}}};
    if (t.client)
        t.client->notify(QStringLiteral("textDocument/didClose"), td);
    if (t.client2)
        t.client2->notify(QStringLiteral("textDocument/didClose"), td);
    if (t.timer)
        t.timer->deleteLater();
}

void LspManager::sendChange(Document *doc)
{
    auto it = m_docs.find(doc);
    if (it == m_docs.end() || (!it->client && !it->client2))
        return;
    it->timer->stop();
    // Envio do texto completo: válido para servidores Full e Incremental.
    const QString text = doc->text()->toPlainText();
    auto change = [&](LspClient *c, int &version) {
        ++version;
        c->notify(QStringLiteral("textDocument/didChange"),
                  QJsonObject{{"textDocument", QJsonObject{{"uri", it->uri}, {"version", version}}},
                              {"contentChanges", QJsonArray{QJsonObject{{"text", text}}}}});
    };
    if (it->client)
        change(it->client, it->version);
    if (it->client2)
        change(it->client2, it->version2);
}

void LspManager::documentSaved(Document *doc)
{
    // Salvar com outro nome ou mudar a extensão troca uri e linguagem.
    attach(doc);
    auto it = m_docs.find(doc);
    if (it == m_docs.end() || (!it->client && !it->client2))
        return;
    if (it->timer->isActive())
        sendChange(doc);
    const QJsonObject td{{"textDocument", QJsonObject{{"uri", it->uri}}}};
    if (it->client)
        it->client->notify(QStringLiteral("textDocument/didSave"), td);
    if (it->client2)
        it->client2->notify(QStringLiteral("textDocument/didSave"), td);
}

void LspManager::complete(Document *doc, int line, int column, CompletionCallback callback)
{
    auto it = m_docs.find(doc);
    if (it == m_docs.end()) {
        callback({});
        return;
    }
    // Pergunta aos dois servidores que oferecem completação e une as respostas.
    QVector<LspClient *> servers;
    if (it->client && it->client->hasCompletion())
        servers << it->client;
    if (it->client2 && it->client2->hasCompletion())
        servers << it->client2;
    if (servers.isEmpty()) {
        callback({});
        return;
    }
    if (it->timer->isActive())
        sendChange(doc); // o servidor precisa do texto atual
    const QJsonObject params{{"textDocument", QJsonObject{{"uri", it->uri}}},
                             {"position", QJsonObject{{"line", line}, {"character", column}}}};
    struct Acc
    {
        int pending = 0;
        QList<LspCompletion> out;
        QSet<QString> seen;
        CompletionCallback cb;
    };
    auto acc = std::make_shared<Acc>();
    acc->pending = servers.size();
    acc->cb = callback;
    for (LspClient *s : std::as_const(servers)) {
        s->request(QStringLiteral("textDocument/completion"), params, [acc](const QJsonValue &result) {
            const QJsonArray items = result.isArray() ? result.toArray() : result.toObject().value("items").toArray();
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
                insert = insert.trimmed();
                const QString keyd = label + QLatin1Char('\x1f') + insert;
                if (acc->seen.contains(keyd))
                    continue; // dedupe entre os dois servidores
                acc->seen.insert(keyd);
                acc->out.append({label, insert, o.value("detail").toString()});
            }
            if (--acc->pending == 0)
                acc->cb(acc->out);
        });
    }
}

QStringList LspManager::triggerCharacters(Document *doc) const
{
    const auto it = m_docs.constFind(doc);
    if (it == m_docs.cend())
        return QStringList();
    QStringList out;
    if (it->client)
        out = it->client->completionTriggers();
    if (it->client2)
        for (const QString &c : it->client2->completionTriggers())
            if (!out.contains(c))
                out << c;
    return out;
}

bool LspManager::hasDefinition(Document *doc) const
{
    const auto it = m_docs.constFind(doc);
    return it != m_docs.cend() && provider(*it, &LspClient::hasDefinition);
}

void LspManager::definition(Document *doc, int line, int column, DefinitionCallback callback)
{
    auto it = m_docs.find(doc);
    LspClient *c = (it != m_docs.end()) ? provider(*it, &LspClient::hasDefinition) : nullptr;
    if (!c) {
        callback({});
        return;
    }
    if (it->timer->isActive())
        sendChange(doc); // o servidor precisa do texto atual
    const QJsonObject params{{"textDocument", QJsonObject{{"uri", it->uri}}},
                             {"position", QJsonObject{{"line", line}, {"character", column}}}};
    c->request(QStringLiteral("textDocument/definition"), params, [callback](const QJsonValue &result) {
        // A resposta pode ser Location, Location[] ou LocationLink[]: pega o primeiro.
        QJsonObject loc;
        if (result.isArray()) {
            const QJsonArray arr = result.toArray();
            if (!arr.isEmpty())
                loc = arr.first().toObject();
        } else if (result.isObject()) {
            loc = result.toObject();
        }
        LspLocation out;
        if (!loc.isEmpty()) {
            QString uri = loc.value("uri").toString();
            QJsonObject range = loc.value("range").toObject();
            if (uri.isEmpty()) { // LocationLink
                uri = loc.value("targetUri").toString();
                range = loc.value("targetSelectionRange").toObject();
                if (range.isEmpty())
                    range = loc.value("targetRange").toObject();
            }
            const QJsonObject start = range.value("start").toObject();
            out.uri = uri;
            out.line = start.value("line").toInt();
            out.column = start.value("character").toInt();
        }
        callback(out);
    });
}

bool LspManager::hasHover(Document *doc) const
{
    const auto it = m_docs.constFind(doc);
    return it != m_docs.cend() && provider(*it, &LspClient::hasHover);
}

void LspManager::hover(Document *doc, int line, int column, HoverCallback callback)
{
    auto it = m_docs.find(doc);
    LspClient *c = (it != m_docs.end()) ? provider(*it, &LspClient::hasHover) : nullptr;
    if (!c) {
        callback(QString());
        return;
    }
    if (it->timer->isActive())
        sendChange(doc);
    const QJsonObject params{{"textDocument", QJsonObject{{"uri", it->uri}}},
                             {"position", QJsonObject{{"line", line}, {"character", column}}}};
    c->request(QStringLiteral("textDocument/hover"), params, [callback](const QJsonValue &result) {
        const QJsonValue contents = result.toObject().value("contents");
        // contents: MarkupContent {kind,value}, MarkedString (texto ou {language,value}) ou um vetor deles.
        auto textOf = [](const QJsonValue &v) {
            return v.isString() ? v.toString() : v.toObject().value("value").toString();
        };
        QString text;
        if (contents.isArray()) {
            QStringList parts;
            for (const QJsonValue &v : contents.toArray()) {
                const QString s = textOf(v);
                if (!s.isEmpty())
                    parts << s;
            }
            text = parts.join(QStringLiteral("\n\n"));
        } else {
            text = textOf(contents);
        }
        callback(text.trimmed());
    });
}

bool LspManager::hasFormatting(Document *doc) const
{
    const auto it = m_docs.constFind(doc);
    return it != m_docs.cend() && provider(*it, &LspClient::hasFormatting);
}

bool LspManager::hasRangeFormatting(Document *doc) const
{
    const auto it = m_docs.constFind(doc);
    return it != m_docs.cend() && provider(*it, &LspClient::hasRangeFormatting);
}

namespace {
QList<LspTextEdit> parseTextEdits(const QJsonValue &result)
{
    QList<LspTextEdit> out;
    for (const QJsonValue &v : result.toArray()) {
        const QJsonObject o = v.toObject();
        const QJsonObject range = o.value("range").toObject();
        const QJsonObject start = range.value("start").toObject();
        const QJsonObject end = range.value("end").toObject();
        LspTextEdit e;
        e.startLine = start.value("line").toInt();
        e.startColumn = start.value("character").toInt();
        e.endLine = end.value("line").toInt();
        e.endColumn = end.value("character").toInt();
        e.newText = o.value("newText").toString();
        out.append(e);
    }
    return out;
}
} // namespace

void LspManager::format(Document *doc, int tabSize, bool insertSpaces, FormatCallback callback)
{
    auto it = m_docs.find(doc);
    LspClient *c = (it != m_docs.end()) ? provider(*it, &LspClient::hasFormatting) : nullptr;
    if (!c) {
        callback({});
        return;
    }
    if (it->timer->isActive())
        sendChange(doc); // o servidor precisa do texto atual
    const QJsonObject options{{"tabSize", tabSize},
                              {"insertSpaces", insertSpaces},
                              {"trimTrailingWhitespace", true},
                              {"insertFinalNewline", true},
                              {"trimFinalNewlines", true}};
    const QJsonObject params{{"textDocument", QJsonObject{{"uri", it->uri}}}, {"options", options}};
    c->request(QStringLiteral("textDocument/formatting"), params,
               [callback](const QJsonValue &result) { callback(parseTextEdits(result)); });
}

void LspManager::formatRange(Document *doc, int startLine, int startColumn, int endLine, int endColumn, int tabSize,
                             bool insertSpaces, FormatCallback callback)
{
    auto it = m_docs.find(doc);
    LspClient *c = (it != m_docs.end()) ? provider(*it, &LspClient::hasRangeFormatting) : nullptr;
    if (!c) {
        callback({});
        return;
    }
    if (it->timer->isActive())
        sendChange(doc);
    const QJsonObject options{{"tabSize", tabSize},
                              {"insertSpaces", insertSpaces},
                              {"trimTrailingWhitespace", true},
                              {"insertFinalNewline", true},
                              {"trimFinalNewlines", true}};
    const QJsonObject range{{"start", QJsonObject{{"line", startLine}, {"character", startColumn}}},
                            {"end", QJsonObject{{"line", endLine}, {"character", endColumn}}}};
    const QJsonObject params{{"textDocument", QJsonObject{{"uri", it->uri}}}, {"range", range}, {"options", options}};
    c->request(QStringLiteral("textDocument/rangeFormatting"), params,
               [callback](const QJsonValue &result) { callback(parseTextEdits(result)); });
}

bool LspManager::hasInlayHints(Document *doc) const
{
    const auto it = m_docs.constFind(doc);
    return it != m_docs.cend() && provider(*it, &LspClient::hasInlayHints);
}

void LspManager::inlayHints(Document *doc, int startLine, int startColumn, int endLine, int endColumn,
                            InlayHintsCallback callback)
{
    auto it = m_docs.find(doc);
    LspClient *c = (it != m_docs.end()) ? provider(*it, &LspClient::hasInlayHints) : nullptr;
    if (!c) {
        callback({});
        return;
    }
    if (it->timer->isActive())
        sendChange(doc); // o servidor precisa do texto atual
    const QJsonObject range{{"start", QJsonObject{{"line", startLine}, {"character", startColumn}}},
                            {"end", QJsonObject{{"line", endLine}, {"character", endColumn}}}};
    const QJsonObject params{{"textDocument", QJsonObject{{"uri", it->uri}}}, {"range", range}};
    c->request(QStringLiteral("textDocument/inlayHint"), params, [callback](const QJsonValue &result) {
        QList<LspInlayHint> out;
        for (const QJsonValue &v : result.toArray()) {
            const QJsonObject o = v.toObject();
            const QJsonObject pos = o.value("position").toObject();
            LspInlayHint h;
            h.line = pos.value("line").toInt();
            h.column = pos.value("character").toInt();
            // label: string, ou vetor de InlayHintLabelPart {value}.
            const QJsonValue label = o.value("label");
            if (label.isString()) {
                h.label = label.toString();
            } else {
                QString s;
                for (const QJsonValue &part : label.toArray())
                    s += part.toObject().value("value").toString();
                h.label = s;
            }
            h.kind = o.value("kind").toInt();
            h.paddingLeft = o.value("paddingLeft").toBool();
            h.paddingRight = o.value("paddingRight").toBool();
            h.label = h.label.trimmed();
            if (!h.label.isEmpty())
                out.append(h);
        }
        callback(out);
    });
}

QString LspManager::statusFor(Document *doc) const
{
    const auto it = m_docs.constFind(doc);
    if (it == m_docs.cend() || (!it->client && !it->client2))
        return QString();
    auto label = [this](LspClient *c) {
        const QString name = QProcess::splitCommand(c->command()).value(0);
        return c->isReady() ? name : tr("%1 — starting").arg(name);
    };
    QStringList parts;
    if (it->client)
        parts << label(it->client);
    if (it->client2)
        parts << label(it->client2);
    return parts.join(QStringLiteral(" + "));
}

void LspManager::onDiagnostics(const QString &uri, const QJsonArray &list)
{
    LspClient *src = qobject_cast<LspClient *>(sender());
    for (auto it = m_docs.begin(); it != m_docs.end(); ++it) {
        if (it->uri != uri)
            continue;
        // Só o slot que publicou: com dois servidores, cada um atualiza o seu.
        const bool isPrimary = (it->client == src);
        const bool isSecondary = (it->client2 == src);
        if (src && !isPrimary && !isSecondary)
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
        // Guarda no slot que publicou (secundário quando o remetente for ele) e
        // aplica no editor a união dos dois. Sem remetente conhecido, trata como
        // principal (compatível com o caso de um único servidor).
        if (isSecondary)
            it->diag2 = diags;
        else
            it->diag = diags;
        applyMerged(it.key());
    }
}
