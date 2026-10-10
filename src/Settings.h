#pragma once

#include <QJsonArray>
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
/** Substituição de exibição: a sequência é mostrada como símbolo (texto intacto). */
struct SymbolRule
{
    QString sequence;
    QString symbol;
    bool enabled = true;
    bool operator==(const SymbolRule &o) const
    { return sequence == o.sequence && symbol == o.symbol && enabled == o.enabled; }
};

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
    QString editorFont;          //< vazio = Hack (fonts-hack)
    int editorFontSize = 11;
    int tabWidth = 4;
    bool lineNumbers = true;
    bool wordWrap = false;
    bool highlightCurrentLine = true;
    bool autoClosePairs = true;
    bool codeCompletion = true;
    bool codeFolding = true;
    bool autoReload = false;     //< relê sozinho arquivos alterados por outros programas
    bool lspEnabled = true;      //< chave geral dos servidores de linguagem
    bool insertSpaces = true;    //< Tab insere espaços (tabulações emuladas)
    bool editorCursorBlink = true;
    bool prettySymbols = false;  //< exibir sequências como símbolos (<= vira ≤); texto não muda
    QList<SymbolRule> symbols;   //< regras de exibição; vazio = conjunto padrão

    // Aparência
    QString theme = QStringLiteral("catppuccin-mocha");
    bool themeFollowSystem = true;  //< usa a variante clara/escura conforme o desktop (padrão)
    int buttonScale = 100;       //< (obsoleto; substituído por toolbarIconSize)
    // Barra de ferramentas (botão direito nela, como no Kate)
    int toolbarIconSize = 22;    //< 16, 22, 32 ou 48
    int toolbarStyle = 0;        //< Qt::ToolButtonStyle (0 = só ícones)
    bool toolbarLocked = false;
    QStringList toolbarActions;  //< ids na ordem; vazio = padrão
    int uiScale = 100;           //< % — aplicado ao reiniciar
    QString uiLanguage = QStringLiteral("auto"); //< "auto" (pt_BR do sistema, senão en), "en", "pt_BR"

    // Terminal
    QString terminalFont;
    int terminalFontSize = 11;
    QString shell;               //< vazio = $SHELL
    bool terminalCursorBlink = true;
    int terminalTransparency = 0; //< % (0 = opaco, máx. 90)

    // Linguagens
    QMap<QString, LanguageConfig> languages; //< id da linguagem embutida → config
    QList<CustomLanguage> customLanguages;
    QMap<QString, LspConfig> lsp;           //< id da linguagem → servidor
    /** Temas do usuário (JSON de Theme): mesmo id de um tema do app = personalização. */
    QJsonArray userThemes;

    static AppSettings defaults();
    static LanguageConfig defaultLanguageConfig(const QString &languageId);
    /** Conjunto padrão de substituições de símbolos. */
    static QList<SymbolRule> defaultSymbols();
    /** Regras ativas, ordenadas da sequência mais longa para a mais curta. */
    QList<SymbolRule> effectiveSymbols() const;

    /** Config efetiva de uma linguagem embutida (com os padrões preenchidos). */
    LanguageConfig languageConfig(const QString &languageId) const;
    /** A ferramenta da linguagem (e o que mais ela exigir) existe no sistema? */
    bool languageInstalled(const QString &languageId) const;
    /** Linguagens embutidas disponíveis neste sistema, na ordem do catálogo. */
    QStringList installedLanguages() const;
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
