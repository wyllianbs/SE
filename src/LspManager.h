#pragma once

#include "Settings.h"

#include <QHash>
#include <QJsonArray>
#include <QObject>
#include <QPointer>

#include <functional>

class Document;
class LspClient;
class QTimer;

/** Servidor conhecido para uma linguagem. */
struct LspPreset
{
    QString name;     //< nome exibido
    QString command;  //< linha de comando
    QString install;  //< como instalar (exibido ao usuário)
};

/** Uma sugestão de completion vinda do servidor. */
struct LspCompletion
{
    QString label;
    QString insertText;
    QString detail;
};

/**
 * Um servidor por linguagem, iniciado sob demanda quando um documento dela é
 * aberto. Sincroniza o texto (envio completo, com atraso curto), repassa
 * diagnósticos ao Document e atende pedidos de completion do editor.
 */
class LspManager : public QObject
{
    Q_OBJECT
public:
    explicit LspManager(QObject *parent = nullptr);
    ~LspManager() override;

    static QList<LspPreset> presets(const QString &languageId);
    static bool isInstalled(const QString &command);
    /** Linguagens com servidor conhecido, na ordem exibida em Settings. */
    static QStringList languages();

    void setSettings(const AppSettings &settings);

    /** (Re)associa o documento ao servidor da sua linguagem, ou o solta. */
    void attach(Document *doc);
    void detach(Document *doc);
    void documentSaved(Document *doc);

    using CompletionCallback = std::function<void(const QList<LspCompletion> &)>;
    /** Pede sugestões na posição (linha e coluna UTF-16, base 0). */
    void complete(Document *doc, int line, int column, CompletionCallback callback);
    QStringList triggerCharacters(Document *doc) const;

    /** Texto curto para o rodapé ("clangd", "pylsp — starting"…). */
    QString statusFor(Document *doc) const;

Q_SIGNALS:
    void statusChanged();
    void message(const QString &text);

private:
    struct Tracked
    {
        QPointer<LspClient> client;
        QString uri;
        int version = 0;
        QTimer *timer = nullptr;
    };

    LspClient *clientFor(const QString &languageId, const QString &rootPath);
    void sendChange(Document *doc);
    QString commandFor(const QString &languageId) const;
    static QString lspLanguageId(const QString &languageId);
    static QString rootFor(const QString &filePath);
    void onDiagnostics(const QString &uri, const QJsonArray &list);

    AppSettings m_settings;
    QHash<QString, QPointer<LspClient>> m_clients;  //< id da linguagem → servidor
    QHash<QString, int> m_failures;                  //< comando → quedas seguidas
    QHash<Document *, Tracked> m_docs;
};
