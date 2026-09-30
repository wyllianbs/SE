#pragma once

#include "Icons.h"
#include "Theme.h"

#include <QMainWindow>

class CodeEditor;
class FindBar;
class QAction;
class QComboBox;
class QLabel;
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

    void applyTheme(const QString &themeId);
    void applyFontSize(int pointSize);
    void refreshIcons();
    void updateTabTitle(CodeEditor *editor);
    void updateStatus();
    QString styleSheetFor(const Theme &theme) const;

    QTabWidget *m_tabs;
    FindBar *m_findBar;
    TerminalDock *m_terminal;
    QToolBar *m_toolbar = nullptr;
    QComboBox *m_languageCombo;
    QLabel *m_cursorLabel;
    Theme m_theme;
    QFont m_editorFont;
    QString m_lastDir;
    QList<QPair<QAction *, Icons::Glyph>> m_iconActions;
    QAction *m_runAction = nullptr;
};
