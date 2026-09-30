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

/**
 * Configurações, no mesmo espírito do painel do Slayer Editor para Android:
 * perfis, editor, aparência, terminal, linguagens, segurança e arquivos.
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
    QWidget *buildSecurityPage();
    QWidget *buildFilesPage();
    void addPage(const QString &title, char32_t glyph, QWidget *page);

    void setForm(const AppSettings &s);
    void refreshProfiles();
    void addLanguageRow(const QString &id, const QString &name, const LanguageConfig &cfg, bool custom);

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
    QCheckBox *m_editorBlink = nullptr;

    // Aparência
    QListWidget *m_themes = nullptr;
    QSlider *m_buttonScale = nullptr;
    QLabel *m_buttonScaleLabel = nullptr;
    QSlider *m_uiScale = nullptr;
    QLabel *m_uiScaleLabel = nullptr;

    // Terminal
    QFontComboBox *m_terminalFont = nullptr;
    QSpinBox *m_terminalFontSize = nullptr;
    QComboBox *m_shell = nullptr;
    QCheckBox *m_terminalBlink = nullptr;

    // Linguagens
    QTableWidget *m_languages = nullptr;

    // Segurança
    QCheckBox *m_guard = nullptr;

    // Arquivos
    QLineEdit *m_projectDir = nullptr;
};
