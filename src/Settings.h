#pragma once

#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

#include <optional>

/** Ferramenta, flags e extensões de uma linguagem embutida. */
struct LanguageConfig
{
    QString tool;
    QString flags;
    QString extensions; //< separadas por espaço ou vírgula
};

/** Servidor LSP de uma linguagem. */
struct LspConfig
{
    bool enabled = true;
    QString command; //< vazio = primeiro servidor conhecido
};

/** Linguagem definida pelo usuário: "<tool> <flags> arquivo". */
struct CustomLanguage
{
    QString name;
    QString tool;
    QString flags;
    QString extensions;
};

/**
 * Todas as configurações do usuário. Gravadas como JSON no QSettings;
 * os perfis guardam o mesmo JSON sob um nome.
 */
struct AppSettings
{
    // Editor
    QString editorFont;          //< vazio = JetBrainsMono Nerd Font embutida
    int editorFontSize = 11;
    int tabWidth = 4;
    bool lineNumbers = true;
    bool wordWrap = false;
    bool highlightCurrentLine = true;
    bool autoClosePairs = true;
    bool codeCompletion = true;
    bool codeFolding = true;
    bool lspEnabled = true;      //< chave geral dos servidores de linguagem
    bool insertSpaces = true;    //< Tab insere espaços (tabulações emuladas)
    bool editorCursorBlink = true;

    // Aparência
    QString theme = QStringLiteral("catppuccin-mocha");
    int buttonScale = 100;       //< % do tamanho dos ícones da barra
    int uiScale = 100;           //< % — aplicado ao reiniciar

    // Terminal
    QString terminalFont;
    int terminalFontSize = 11;
    QString shell;               //< vazio = $SHELL
    bool terminalCursorBlink = true;

    // Linguagens
    QMap<QString, LanguageConfig> languages; //< id da linguagem embutida → config
    QList<CustomLanguage> customLanguages;
    QMap<QString, LspConfig> lsp;           //< id da linguagem → servidor

    static AppSettings defaults();
    static LanguageConfig defaultLanguageConfig(const QString &languageId);

    /** Config efetiva de uma linguagem embutida (com os padrões preenchidos). */
    LanguageConfig languageConfig(const QString &languageId) const;
    /** Config efetiva do LSP (comando padrão preenchido). */
    LspConfig lspConfig(const QString &languageId) const;

    QJsonObject toJson() const;
    static AppSettings fromJson(const QJsonObject &json);

    static AppSettings load();
    void save() const;

    // Perfis
    static QStringList profileNames();
    static void saveProfile(const QString &name, const AppSettings &settings);
    static std::optional<AppSettings> loadProfile(const QString &name);
    static void deleteProfile(const QString &name);
    static QString activeProfile();
    static void setActiveProfile(const QString &name);
};
