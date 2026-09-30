#pragma once

#include "Icons.h"
#include "Language.h"
#include "Settings.h"
#include "Theme.h"

#include <QMainWindow>

class CodeEditor;
class FindBar;
class QAction;
class QComboBox;
class QLabel;
class QPushButton;
class QStackedWidget;
class QTabWidget;
class QToolBar;
class TerminalDock;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);

    void openFiles(const QStringList &paths);

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void createActions();
    void restoreSession();
    void saveSession();

    CodeEditor *newTab();
    QWidget *buildEmptyState();
    void updateEmptyState();
    CodeEditor *currentEditor() const;
    CodeEditor *editorAt(int index) const;
    void openFile(const QString &path);
    bool save(CodeEditor *editor);
    bool saveAs(CodeEditor *editor);
    bool closeTab(int index);
    bool maybeSave(CodeEditor *editor);
    void runCurrent();
    void goToLine();
    void showAbout();

    void applySettings(const AppSettings &settings);
    void applyTheme(const QString &themeId);
    void applyFontSize(int pointSize);
    void configureEditor(CodeEditor *editor) const;
    void showSettings();
    void rebuildLanguageCombo();
    Language languageForPath(const QString &path) const;
    Language languageById(const QString &id) const;
    QFont editorFont() const;
    QFont terminalFont() const;
    void refreshIcons();
    void updateTabTitle(CodeEditor *editor);
    void updateStatus();
    QString styleSheetFor(const Theme &theme) const;

    QTabWidget *m_tabs;
    QStackedWidget *m_stack = nullptr;
    QList<QPair<QPushButton *, Icons::Glyph>> m_emptyButtons;
    FindBar *m_findBar;
    TerminalDock *m_terminal;
    QToolBar *m_toolbar = nullptr;
    QComboBox *m_languageCombo;
    QLabel *m_cursorLabel;
    Theme m_theme;
    AppSettings m_settings;
    QString m_lastDir;
    QList<QPair<QAction *, Icons::Glyph>> m_iconActions;
    QAction *m_runAction = nullptr;
};
