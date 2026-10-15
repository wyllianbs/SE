#pragma once

#include "Icons.h"
#include "Language.h"
#include "Settings.h"
#include "Theme.h"

#include <QMainWindow>
#include <QPointer>

class CodeEditor;
class FindBar;
class QAction;
class QComboBox;
class QLabel;
class QPushButton;
class QStackedWidget;
class QSplitter;
class Document;
class ViewSpace;
class QToolBar;
class QToolButton;
class TerminalDock;
class LspManager;
class PreviewServer;
class QProcess;
class QTimer;
class DebugSession;
class DebugPanel;
struct DebugFrame;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    void openFiles(const QStringList &paths);

protected:
    void closeEvent(QCloseEvent *event) override;
    // Arrastar arquivos para áreas não cobertas pelos editores (tela de abertura,
    // barras) os abre em novas abas.
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void createActions();
    void restoreSession();
    void saveSession();

    CodeEditor *newTab();
    void newFileFromTemplate();
    QWidget *buildEmptyState();
    void updateEmptyState();
    CodeEditor *currentEditor() const;

    // Split view
    ViewSpace *createViewSpace();
    QList<ViewSpace *> viewSpaces() const;
    QList<CodeEditor *> allEditors() const;
    QList<CodeEditor *> editorsFor(Document *doc) const;
    void setActiveSpace(ViewSpace *space);
    void splitSpace(ViewSpace *space, Qt::Orientation orientation, bool moveDocument);
    bool closeSpace(ViewSpace *space);
    void removeSpace(ViewSpace *space);
    void cycleSpace(int step);
    void updateSpaceActions();

    Document *createDocument();
    CodeEditor *openView(Document *doc, ViewSpace *space);
    void releaseView(CodeEditor *editor);
    void updateDocumentTitles(Document *doc);
    void tabAction(ViewSpace *space, int index, const QString &action);
    void renameFile(CodeEditor *editor);
    void openFile(const QString &path);
    bool save(CodeEditor *editor);
    bool saveAs(CodeEditor *editor);
    bool closeTab(ViewSpace *space, int index);
    bool maybeSave(CodeEditor *editor);
    void runCurrent(bool interactive = false);
    void previewCurrent(CodeEditor *editor); //< HTML: abre no navegador (servidor de preview)
    void startDebugging();
    void showExecutionPoint(const DebugFrame &frame);
    void clearExecutionPoint();
    void updateDebugActions();
    QHash<QString, QList<int>> collectBreakpoints() const;
    void goToLine();
    void showAbout();
    void showHelp();
    void saveAll();
    // Barra de ferramentas configurável (como no Kate)
    static QStringList defaultToolbarActions();
    void rebuildToolbar();
    void showToolbarMenu(const QPoint &globalPos);
    void configureToolbars();
    QWidget *buildDiskBar();
    void updateDiskBar();
    void showDiskDifference(Document *doc);
    void restartApplication();

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
    void updateStatus();
    QString styleSheetFor(const Theme &theme) const;

    QSplitter *m_rootSplitter = nullptr;
    ViewSpace *m_activeSpace = nullptr;
    bool m_inactiveHidden = false;
    QStackedWidget *m_stack = nullptr;
    QList<QPair<QPushButton *, Icons::Glyph>> m_emptyButtons;
    FindBar *m_findBar;
    TerminalDock *m_terminal;
    QToolBar *m_toolbar = nullptr;
    QComboBox *m_languageCombo;
    QLabel *m_cursorLabel;
    QLabel *m_branchLabel = nullptr;
    QLabel *m_modeLabel = nullptr;
    QToolButton *m_tabsButton = nullptr;
    QToolButton *m_encodingButton = nullptr;
    QToolButton *m_lspLabel = nullptr;
    QLabel *m_diagLabel = nullptr;
    LspManager *m_lsp = nullptr;
    PreviewServer *m_preview = nullptr;
    QProcess *m_previewProcess = nullptr; //< servidor externo (ex.: php -S)
    QTimer *m_previewReloadTimer = nullptr;
    QTimer *m_autoSaveTimer = nullptr;
    QSet<CodeEditor *> m_autoSavePending;
    void autoSavePending();
    void changeEncoding(const QString &encoding, bool bom);
    void buildStatusBar();
    QString gitBranchFor(const QString &filePath) const;
    Theme m_theme;
    AppSettings m_settings;
    QString m_lastDir;
    QList<QPair<QAction *, Icons::Glyph>> m_iconActions;
    QAction *m_runAction = nullptr;
    QAction *m_runToolbarAction = nullptr;
    QAction *m_runInteractiveAction = nullptr;
    QAction *m_livePreviewAction = nullptr;
    void setLivePreview(bool on);
    DebugSession *m_debug = nullptr;
    DebugPanel *m_debugPanel = nullptr;
    QList<QAction *> m_stepActions; //< ativas só com o programa pausado
    QAction *m_debugAction = nullptr;
    QAction *m_stopDebugAction = nullptr;
    QPointer<Document> m_execDoc;
    bool m_focusProgramOnRun = true;
    // Arquivos alterados por outros programas (valor: apagado?).
    QHash<Document *, bool> m_diskChanged;
    QWidget *m_diskBar = nullptr;
    QLabel *m_diskIcon = nullptr;
    QLabel *m_diskLabel = nullptr;
    QPushButton *m_diskAutoReload = nullptr;
    QPushButton *m_diskReload = nullptr;
    QPushButton *m_diskDiff = nullptr;
    QPushButton *m_diskOverwrite = nullptr;
    QPushButton *m_diskIgnore = nullptr;
    QAction *m_lspAction = nullptr;
    QList<QPair<QAction *, bool AppSettings::*>> m_settingToggles;
    QList<QPair<QString, QAction *>> m_toolbarRegistry; //< id → ação que pode ir para a barra
};
