#pragma once

#include "Settings.h"
#include "Theme.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QFontComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QSlider;
class QSpinBox;
class QStackedWidget;
class QTableWidget;
class QPushButton;

/**
 * Configurações, no mesmo espírito do painel do Slayer Editor para Android:
 * perfis, editor, aparência, terminal e linguagens.
 * "Aplicar" emite applied() com os valores do formulário.
 */
class SettingsDialog : public QDialog
{
    Q_OBJECT
public:
    SettingsDialog(const AppSettings &settings, const Theme &theme, QWidget *parent = nullptr);

    AppSettings settings() const;

Q_SIGNALS:
    void applied(const AppSettings &settings);

private:
    QWidget *buildProfilesPage();
    QWidget *buildEditorPage();
    QWidget *buildAppearancePage();
    QWidget *buildTerminalPage();
    QWidget *buildLanguagesPage();
    struct LangEntry
    {
        QString id;      //< vazio = linguagem personalizada
        QString name;
        bool custom = false;
        LanguageConfig cfg;
        LspConfig lsp;
    };
    void refreshLanguageList(int select);
    void loadLanguageForm();
    void storeLanguageForm();
    void updateLanguageStatus();
    QString effectiveLspCommand(const LangEntry &e) const;
    void rebuildThemeList(const QString &selectId);
    QString currentThemeId() const;
    void addPage(const QString &title, char32_t glyph, QWidget *page);

    void setForm(const AppSettings &s);
    void refreshProfiles();

    AppSettings m_detect; //< configurações iniciais, para detectar o que está instalado
    Theme m_theme;
    QListWidget *m_nav;
    QStackedWidget *m_pages;

    // Perfis
    QListWidget *m_profiles = nullptr;
    QLineEdit *m_profileName = nullptr;
    QLabel *m_activeProfile = nullptr;

    // Editor
    QFontComboBox *m_editorFont = nullptr;
    QSpinBox *m_editorFontSize = nullptr;
    QSpinBox *m_tabWidth = nullptr;
    QCheckBox *m_lineNumbers = nullptr;
    QCheckBox *m_wordWrap = nullptr;
    QCheckBox *m_highlightLine = nullptr;
    QCheckBox *m_autoPairs = nullptr;
    QCheckBox *m_completion = nullptr;
    QCheckBox *m_folding = nullptr;
    QCheckBox *m_lspEnabled = nullptr;
    QCheckBox *m_insertSpaces = nullptr;
    QCheckBox *m_editorBlink = nullptr;

    // Aparência
    QListWidget *m_themes = nullptr;
    QList<Theme> m_userThemes; //< em edição; vão para AppSettings::userThemes
    QPushButton *m_modeDark = nullptr;
    QPushButton *m_modeLight = nullptr;
    QCheckBox *m_followSystem = nullptr;
    bool m_showDark = true;
    void setMode(bool dark);
    QSlider *m_buttonScale = nullptr;
    QLabel *m_buttonScaleLabel = nullptr;
    QSlider *m_uiScale = nullptr;
    QComboBox *m_uiLanguage = nullptr;
    QLabel *m_uiScaleLabel = nullptr;

    // Terminal
    QFontComboBox *m_terminalFont = nullptr;
    QSpinBox *m_terminalFontSize = nullptr;
    QComboBox *m_shell = nullptr;
    QCheckBox *m_terminalBlink = nullptr;
    QSlider *m_terminalTransparency = nullptr;
    QLabel *m_terminalTransparencyLabel = nullptr;

    // Linguagens (com o servidor de linguagem de cada uma)
    QListWidget *m_langList = nullptr;
    QList<LangEntry> m_entries;
    int m_currentEntry = -1;
    bool m_loadingLanguage = false;
    QLineEdit *m_langTool = nullptr;
    QLineEdit *m_langFlags = nullptr;
    QLineEdit *m_langExtensions = nullptr;
    QLabel *m_langRunHint = nullptr;
    QPushButton *m_langReset = nullptr;
    QPushButton *m_langRemove = nullptr;
    QWidget *m_lspCard = nullptr;
    QCheckBox *m_lspOn = nullptr;
    QComboBox *m_lspCommand = nullptr;
    QLabel *m_lspStatus = nullptr;
    QLabel *m_missingLanguages = nullptr;
    QMap<QString, LanguageConfig> m_hiddenLanguages; //< configs de linguagens não instaladas, preservadas
    QMap<QString, LspConfig> m_hiddenLsp;
};
