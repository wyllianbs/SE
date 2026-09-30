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

    // Segurança
    bool blockDestructiveCommands = true;

    // Arquivos
    QString projectDir;          //< vazio = ~/SlayerEditor

    // Linguagens
    QMap<QString, LanguageConfig> languages; //< id da linguagem embutida → config
    QList<CustomLanguage> customLanguages;

    static AppSettings defaults();
    static LanguageConfig defaultLanguageConfig(const QString &languageId);

    /** Config efetiva de uma linguagem embutida (com os padrões preenchidos). */
    LanguageConfig languageConfig(const QString &languageId) const;
    QString effectiveProjectDir() const;

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
