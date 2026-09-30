#include "MainWindow.h"
#include "CodeEditor.h"
#include "FindBar.h"
#include "Language.h"
#include "TerminalDock.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QSettings>
#include <QStatusBar>
#include <QTabBar>
#include <QTabWidget>
#include <QTextBlock>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_tabs(new QTabWidget(this))
    , m_findBar(new FindBar(this))
    , m_terminal(new TerminalDock(this))
    , m_languageCombo(new QComboBox(this))
    , m_cursorLabel(new QLabel(this))
    , m_theme(Theme::all().first())
    , m_lastDir(QDir::homePath())
{
    setWindowTitle(QStringLiteral("Slayer Editor"));
    setWindowIcon(QIcon(QStringLiteral(":/icons/slayer-editor.png")));
    setDockOptions(QMainWindow::AnimatedDocks | QMainWindow::AllowTabbedDocks);
    setCorner(Qt::BottomLeftCorner, Qt::LeftDockWidgetArea);
    setCorner(Qt::BottomRightCorner, Qt::RightDockWidgetArea);

    m_tabs->setDocumentMode(true);
    m_tabs->setTabsClosable(true);
    m_tabs->setMovable(true);
    m_tabs->tabBar()->setExpanding(false);
    m_tabs->tabBar()->setElideMode(Qt::ElideMiddle);

    auto *central = new QWidget(this);
    central->setObjectName(QStringLiteral("Central"));
    auto *layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_tabs, 1);
    layout->addWidget(m_findBar);
    setCentralWidget(central);

    addDockWidget(Qt::BottomDockWidgetArea, m_terminal);

    for (const Language &lang : Language::all())
        m_languageCombo->addItem(lang.displayName, lang.id);
    m_languageCombo->addItem(Language::plainText().displayName, Language::plainText().id);
    m_languageCombo->setFocusPolicy(Qt::NoFocus);
    statusBar()->addPermanentWidget(m_cursorLabel);
    statusBar()->addPermanentWidget(m_languageCombo);
    statusBar()->setSizeGripEnabled(false);

    connect(m_languageCombo, &QComboBox::activated, this, [this](int index) {
        if (CodeEditor *ed = currentEditor()) {
            ed->setLanguage(Language::byId(m_languageCombo->itemData(index).toString()));
            updateTabTitle(ed);
            updateStatus();
        }
    });
    connect(m_tabs, &QTabWidget::tabCloseRequested, this, &MainWindow::closeTab);
    connect(m_tabs, &QTabWidget::currentChanged, this, [this]() {
        m_findBar->setEditor(currentEditor());
        updateStatus();
    });

    createActions();
    restoreSession();
    if (m_tabs->count() == 0)
        newTab();
}

// ---------------------------------------------------------------- ações

void MainWindow::createActions()
{
    auto add = [this](QMenu *menu, const QString &text, const QKeySequence &key, auto slot,
                      std::optional<Icons::Glyph> glyph = std::nullopt) {
        QAction *a = menu->addAction(text);
        if (!key.isEmpty())
            a->setShortcut(key);
        connect(a, &QAction::triggered, this, slot);
        if (glyph)
            m_iconActions.append({a, *glyph});
        return a;
    };

    QMenu *file = menuBar()->addMenu(tr("&File"));
    QAction *newA = add(file, tr("&New"), QKeySequence::New, [this]() { newTab(); }, Icons::NewFile);
    QAction *openA = add(file, tr("&Open…"), QKeySequence::Open, [this]() {
        const QStringList paths = QFileDialog::getOpenFileNames(this, tr("Open File"), m_lastDir);
        openFiles(paths);
    }, Icons::Open);
    file->addSeparator();
    QAction *saveA = add(file, tr("&Save"), QKeySequence::Save, [this]() { save(currentEditor()); }, Icons::Save);
    add(file, tr("Save &As…"), QKeySequence::SaveAs, [this]() { saveAs(currentEditor()); }, Icons::SaveAs);
    file->addSeparator();
    add(file, tr("&Close Tab"), QKeySequence(Qt::CTRL | Qt::Key_W), [this]() {
        closeTab(m_tabs->currentIndex());
    }, Icons::Close);
    add(file, tr("&Quit"), QKeySequence::Quit, [this]() { close(); });

    QMenu *edit = menuBar()->addMenu(tr("&Edit"));
    QAction *undoA = add(edit, tr("&Undo"), QKeySequence::Undo, [this]() {
        if (auto *e = currentEditor()) e->undo();
    }, Icons::Undo);
    QAction *redoA = add(edit, tr("&Redo"), QKeySequence::Redo, [this]() {
        if (auto *e = currentEditor()) e->redo();
    }, Icons::Redo);
    edit->addSeparator();
    QAction *findA = add(edit, tr("&Find…"), QKeySequence::Find, [this]() { m_findBar->activate(false); }, Icons::Search);
    add(edit, tr("&Replace…"), QKeySequence(Qt::CTRL | Qt::Key_H), [this]() { m_findBar->activate(true); });
    add(edit, tr("Find &Next"), QKeySequence::FindNext, [this]() { m_findBar->findNext(); });
    add(edit, tr("Find &Previous"), QKeySequence::FindPrevious, [this]() { m_findBar->findPrevious(); });
    edit->addSeparator();
    add(edit, tr("Toggle &Comment"), QKeySequence(Qt::CTRL | Qt::Key_Slash), [this]() {
        if (auto *e = currentEditor()) e->toggleComment();
    });
    add(edit, tr("&Go to Line…"), QKeySequence(Qt::CTRL | Qt::Key_G), [this]() { goToLine(); });

    QMenu *view = menuBar()->addMenu(tr("&View"));
    QAction *termA = add(view, tr("Show/Hide &Terminal"), QKeySequence(Qt::CTRL | Qt::Key_QuoteLeft), [this]() {
        if (m_terminal->isVisible() && m_terminal->isAncestorOf(QApplication::focusWidget())) {
            m_terminal->hide();
            if (auto *e = currentEditor()) e->setFocus();
        } else {
            m_terminal->show();
            m_terminal->focusTerminal();
        }
    }, Icons::Terminal);
    QMenu *pos = view->addMenu(tr("Terminal &Position"));
    const QList<QPair<QString, QString>> positions{
        {tr("Left"), "left"}, {tr("Top"), "top"}, {tr("Right"), "right"}, {tr("Bottom"), "bottom"}};
    for (const auto &[label, key] : positions) {
        const QString k = key;
        connect(pos->addAction(label), &QAction::triggered, this,
                [this, k]() { m_terminal->moveToArea(TerminalDock::areaFromKey(k)); });
    }
    view->addSeparator();
    QMenu *themeMenu = view->addMenu(tr("T&heme"));
    auto *themeGroup = new QActionGroup(themeMenu);
    for (const Theme &t : Theme::all()) {
        QAction *a = themeMenu->addAction(t.name);
        a->setCheckable(true);
        a->setData(t.id);
        themeGroup->addAction(a);
        const QString id = t.id;
        connect(a, &QAction::triggered, this, [this, id]() { applyTheme(id); });
    }
    connect(themeMenu, &QMenu::aboutToShow, this, [this, themeGroup]() {
        for (QAction *a : themeGroup->actions())
            a->setChecked(a->data().toString() == m_theme.id);
    });
    view->addSeparator();
    add(view, tr("Zoom &In"), QKeySequence::ZoomIn, [this]() { applyFontSize(m_editorFont.pointSize() + 1); });
    add(view, tr("Zoom &Out"), QKeySequence::ZoomOut, [this]() { applyFontSize(m_editorFont.pointSize() - 1); });
    add(view, tr("&Reset Zoom"), QKeySequence(Qt::CTRL | Qt::Key_0), [this]() { applyFontSize(11); });

    QMenu *run = menuBar()->addMenu(tr("&Run"));
    m_runAction = add(run, tr("&Run File"), QKeySequence(Qt::Key_F5), [this]() { runCurrent(); }, Icons::Run);
    m_runAction->setToolTip(tr("Save and run the current file (F5)"));

    QMenu *help = menuBar()->addMenu(tr("&Help"));
    QAction *aboutA = add(help, tr("&About Slayer Editor"), QKeySequence(), [this]() { showAbout(); }, Icons::Info);

    // Barra de ferramentas plana, só ícones.
    m_toolbar = addToolBar(tr("Main Toolbar"));
    m_toolbar->setObjectName(QStringLiteral("MainToolbar"));
    m_toolbar->setMovable(false);
    m_toolbar->setIconSize(QSize(18, 18));
    m_toolbar->addActions({newA, openA, saveA});
    m_toolbar->addSeparator();
    m_toolbar->addActions({undoA, redoA, findA});
    m_toolbar->addSeparator();
    m_toolbar->addAction(termA);
    auto *spacer = new QWidget(m_toolbar);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_toolbar->addWidget(spacer);
    m_toolbar->addAction(aboutA);
    m_toolbar->addAction(m_runAction);
    if (auto *runButton = m_toolbar->widgetForAction(m_runAction)) {
        runButton->setObjectName(QStringLiteral("RunButton"));
        static_cast<QToolButton *>(runButton)->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    }
    m_runAction->setIconText(tr("Run"));
}

// --------------------------------------------------------------- abas

CodeEditor *MainWindow::newTab()
{
    auto *editor = new CodeEditor(m_tabs);
    editor->setTheme(m_theme);
    editor->setEditorFont(m_editorFont);
    const int index = m_tabs->addTab(editor, QString());
    auto *closeButton = new QToolButton(m_tabs->tabBar());
    closeButton->setObjectName(QStringLiteral("TabClose"));
    closeButton->setAutoRaise(true);
    closeButton->setIconSize(QSize(12, 12));
    closeButton->setIcon(Icons::icon(Icons::Close, m_theme.textSecondary, 12));
    closeButton->setToolTip(tr("Close tab"));
    connect(closeButton, &QToolButton::clicked, this, [this, editor]() { closeTab(m_tabs->indexOf(editor)); });
    m_tabs->tabBar()->setTabButton(index, QTabBar::RightSide, closeButton);
    m_tabs->setCurrentIndex(index);
    connect(editor->document(), &QTextDocument::modificationChanged, this,
            [this, editor]() { updateTabTitle(editor); });
    connect(editor, &QPlainTextEdit::cursorPositionChanged, this, [this, editor]() {
        if (editor == currentEditor())
            updateStatus();
    });
    updateTabTitle(editor);
    editor->setFocus();
    return editor;
}

CodeEditor *MainWindow::currentEditor() const
{
    return qobject_cast<CodeEditor *>(m_tabs->currentWidget());
}

CodeEditor *MainWindow::editorAt(int index) const
{
    return qobject_cast<CodeEditor *>(m_tabs->widget(index));
}

void MainWindow::openFiles(const QStringList &paths)
{
    for (const QString &p : paths)
        openFile(p);
}

void MainWindow::openFile(const QString &path)
{
    const QString abs = QFileInfo(path).absoluteFilePath();
    for (int i = 0; i < m_tabs->count(); ++i) {
        if (editorAt(i)->filePath() == abs) {
            m_tabs->setCurrentIndex(i);
            return;
        }
    }

    // Reaproveita a aba "Sem título" vazia.
    CodeEditor *editor = currentEditor();
    if (!editor || !editor->filePath().isEmpty() || editor->document()->isModified()
        || !editor->toPlainText().isEmpty())
        editor = newTab();

    if (QFileInfo::exists(abs)) {
        QString error;
        if (!editor->loadFile(abs, &error)) {
            QMessageBox::warning(this, tr("Open File"), tr("Could not open %1:\n%2").arg(abs, error));
            return;
        }
    } else {
        editor->setFilePath(abs);
        editor->setLanguage(Language::forFileName(abs));
    }
    m_lastDir = QFileInfo(abs).absolutePath();
    updateTabTitle(editor);
    updateStatus();
}

bool MainWindow::save(CodeEditor *editor)
{
    if (!editor)
        return false;
    if (editor->filePath().isEmpty())
        return saveAs(editor);
    QString error;
    if (!editor->saveFile(editor->filePath(), &error)) {
        QMessageBox::warning(this, tr("Save"), tr("Could not save %1:\n%2").arg(editor->filePath(), error));
        return false;
    }
    statusBar()->showMessage(tr("Saved %1").arg(editor->displayName()), 2500);
    return true;
}

bool MainWindow::saveAs(CodeEditor *editor)
{
    if (!editor)
        return false;
    const QString start = editor->filePath().isEmpty() ? m_lastDir : editor->filePath();
    const QString path = QFileDialog::getSaveFileName(this, tr("Save As"), start);
    if (path.isEmpty())
        return false;
    QString error;
    if (!editor->saveFile(path, &error)) {
        QMessageBox::warning(this, tr("Save"), tr("Could not save %1:\n%2").arg(path, error));
        return false;
    }
    m_lastDir = QFileInfo(path).absolutePath();
    updateTabTitle(editor);
    updateStatus();
    return true;
}

bool MainWindow::maybeSave(CodeEditor *editor)
{
    if (!editor || !editor->document()->isModified())
        return true;
    m_tabs->setCurrentWidget(editor);
    const auto answer = QMessageBox::question(
        this, tr("Unsaved Changes"),
        tr("“%1” has unsaved changes. Save them?").arg(editor->displayName()),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (answer == QMessageBox::Save)
        return save(editor);
    return answer == QMessageBox::Discard;
}

bool MainWindow::closeTab(int index)
{
    CodeEditor *editor = editorAt(index);
    if (!editor || !maybeSave(editor))
        return false;
    m_tabs->removeTab(index);
    editor->deleteLater();
    if (m_tabs->count() == 0)
        newTab();
    return true;
}

void MainWindow::updateTabTitle(CodeEditor *editor)
{
    const int index = m_tabs->indexOf(editor);
    if (index < 0)
        return;
    const QString modified = editor->document()->isModified() ? QStringLiteral("  ●") : QString();
    m_tabs->setTabText(index, editor->displayName() + modified);
    m_tabs->setTabToolTip(index, editor->filePath());
    const QString lang = editor->language().id;
    m_tabs->setTabIcon(index, Icons::icon(Icons::forLanguage(lang), Icons::colorForLanguage(lang), 16));
    if (editor == currentEditor())
        updateStatus();
}

void MainWindow::updateStatus()
{
    CodeEditor *editor = currentEditor();
    if (!editor)
        return;
    const QTextCursor c = editor->textCursor();
    m_cursorLabel->setText(tr("Ln %1, Col %2").arg(c.blockNumber() + 1).arg(c.positionInBlock() + 1));
    const int idx = m_languageCombo->findData(editor->language().id);
    m_languageCombo->setCurrentIndex(idx);
    m_runAction->setEnabled(!editor->language().runCommand.isEmpty());
    const QString title = editor->displayName() + (editor->document()->isModified() ? QStringLiteral(" ●") : QString());
    setWindowTitle(title + QStringLiteral(" — Slayer Editor"));
}

// ----------------------------------------------------------- executar

void MainWindow::runCurrent()
{
    CodeEditor *editor = currentEditor();
    if (!editor)
        return;
    if (editor->language().runCommand.isEmpty()) {
        statusBar()->showMessage(tr("No run command for %1").arg(editor->language().displayName), 3000);
        return;
    }
    if (!save(editor))
        return;
    const QFileInfo info(editor->filePath());
    m_terminal->runCommand(editor->language().commandFor(info.absoluteFilePath()), info.absolutePath());
}

void MainWindow::goToLine()
{
    CodeEditor *editor = currentEditor();
    if (!editor)
        return;
    bool ok = false;
    const int line = QInputDialog::getInt(this, tr("Go to Line"), tr("Line:"),
                                          editor->textCursor().blockNumber() + 1, 1,
                                          editor->blockCount(), 1, &ok);
    if (ok)
        editor->goToLine(line);
}

void MainWindow::showAbout()
{
    QMessageBox box(this);
    box.setWindowTitle(tr("About Slayer Editor"));
    box.setIconPixmap(QPixmap(QStringLiteral(":/icons/slayer-editor.png"))
                          .scaled(96, 96, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    box.setText(QStringLiteral("<h2>Slayer Editor</h2><p>%1 %2</p>")
                    .arg(tr("Version"), QStringLiteral(SLAYER_VERSION)));
    box.setInformativeText(tr("A lightweight code editor with an embedded terminal.<br>"
                              "© Prof. Wyllian"));
    box.exec();
}

// ------------------------------------------------------- tema e fonte

void MainWindow::applyTheme(const QString &themeId)
{
    m_theme = Theme::byId(themeId);

    QPalette p;
    p.setColor(QPalette::Window, m_theme.window);
    p.setColor(QPalette::WindowText, m_theme.text);
    p.setColor(QPalette::Base, m_theme.surface);
    p.setColor(QPalette::AlternateBase, m_theme.window);
    p.setColor(QPalette::Text, m_theme.text);
    p.setColor(QPalette::Button, m_theme.surface);
    p.setColor(QPalette::ButtonText, m_theme.text);
    p.setColor(QPalette::Highlight, m_theme.primary);
    p.setColor(QPalette::HighlightedText, m_theme.dark ? m_theme.window : QColor(Qt::white));
    p.setColor(QPalette::ToolTipBase, m_theme.surface);
    p.setColor(QPalette::ToolTipText, m_theme.text);
    p.setColor(QPalette::PlaceholderText, m_theme.textSecondary);
    p.setColor(QPalette::Link, m_theme.primary);
    p.setColor(QPalette::Disabled, QPalette::Text, m_theme.textSecondary);
    p.setColor(QPalette::Disabled, QPalette::ButtonText, m_theme.textSecondary);
    p.setColor(QPalette::Disabled, QPalette::WindowText, m_theme.textSecondary);
    qApp->setPalette(p);
    qApp->setStyleSheet(styleSheetFor(m_theme));

    for (int i = 0; i < m_tabs->count(); ++i)
        editorAt(i)->setTheme(m_theme);
    m_terminal->setTheme(m_theme);
    refreshIcons();
}

void MainWindow::refreshIcons()
{
    for (const auto &[action, glyph] : std::as_const(m_iconActions))
        action->setIcon(Icons::icon(glyph, action == m_runAction ? m_theme.window : m_theme.textSecondary));
    for (int i = 0; i < m_tabs->count(); ++i) {
        if (auto *b = qobject_cast<QToolButton *>(m_tabs->tabBar()->tabButton(i, QTabBar::RightSide)))
            b->setIcon(Icons::icon(Icons::Close, m_theme.textSecondary, 12));
    }
}

void MainWindow::applyFontSize(int pointSize)
{
    pointSize = qBound(6, pointSize, 48);
    m_editorFont = QFont(Icons::monoFamily());
    m_editorFont.setStyleHint(QFont::Monospace);
    m_editorFont.setPointSize(pointSize);
    for (int i = 0; i < m_tabs->count(); ++i)
        editorAt(i)->setEditorFont(m_editorFont);
    m_terminal->setTerminalFont(m_editorFont);
}

QString MainWindow::styleSheetFor(const Theme &t) const
{
    const QString hover = t.divider.name();
    return QStringLiteral(R"(
QMainWindow, #Central { background: %1; }
QToolBar#MainToolbar { background: %1; border: none; padding: 6px 8px; spacing: 4px; }
QToolBar::separator { background: %4; width: 1px; margin: 6px 6px; }
QToolButton { border: none; border-radius: 8px; padding: 6px; color: %3; }
QToolButton:hover { background: %4; }
QToolButton:pressed { background: %5; }
QToolButton#RunButton { background: %5; color: %1; font-weight: 600; padding: 6px 14px; }
QToolButton#RunButton:hover { background: %6; }
QToolButton#RunButton:disabled { background: %4; color: %7; }
QMenuBar { background: %1; color: %3; padding: 2px 4px; }
QMenuBar::item { padding: 5px 10px; border-radius: 6px; background: transparent; }
QMenuBar::item:selected { background: %4; }
QMenu { background: %2; color: %3; border: 1px solid %4; border-radius: 10px; padding: 6px; }
QMenu::item { padding: 6px 28px 6px 14px; border-radius: 6px; }
QMenu::item:selected { background: %4; }
QMenu::separator { height: 1px; background: %4; margin: 5px 8px; }
QTabWidget::pane { border: none; }
QTabBar { background: %1; }
QTabBar::tab { background: transparent; color: %7; padding: 8px 14px; margin: 4px 2px 0 2px;
               border-top-left-radius: 10px; border-top-right-radius: 10px; min-width: 60px; }
QTabBar::tab:selected { background: %2; color: %3; }
QTabBar::tab:hover:!selected { background: %4; color: %3; }
QToolButton#TabClose { padding: 2px; border-radius: 5px; margin-left: 4px; }
QPlainTextEdit { background: %2; color: %3; border: none; selection-background-color: %8; }
QStatusBar { background: %1; color: %7; border-top: 1px solid %4; }
QStatusBar QLabel { color: %7; padding: 0 10px; }
QStatusBar::item { border: none; }
QComboBox { background: transparent; color: %7; border: none; padding: 2px 10px; border-radius: 6px; }
QComboBox:hover { background: %4; color: %3; }
QComboBox QAbstractItemView { background: %2; color: %3; border: 1px solid %4; border-radius: 8px;
                              selection-background-color: %4; padding: 4px; }
QDockWidget { color: %3; }
#DockTitleBar { background: %1; border-top: 1px solid %4; }
#DockTitle { color: %7; font-weight: 600; letter-spacing: 0.5px; }
#FindBar { background: %1; border-top: 1px solid %4; }
QLineEdit { background: %2; color: %3; border: 1px solid %4; border-radius: 8px; padding: 5px 10px;
            selection-background-color: %8; }
QLineEdit:focus { border: 1px solid %5; }
QPushButton { background: %2; color: %3; border: 1px solid %4; border-radius: 8px; padding: 5px 12px; }
QPushButton:hover { background: %4; }
QPushButton:default { border: 1px solid %5; }
QCheckBox { color: %7; spacing: 6px; }
QToolTip { background: %2; color: %3; border: 1px solid %4; border-radius: 6px; padding: 4px 8px; }
QScrollBar:vertical { background: transparent; width: 12px; margin: 0; }
QScrollBar:horizontal { background: transparent; height: 12px; margin: 0; }
QScrollBar::handle { background: %4; border-radius: 4px; min-height: 30px; min-width: 30px; margin: 3px; }
QScrollBar::handle:hover { background: %7; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }
QMainWindow::separator { background: %4; width: 1px; height: 1px; }
QMainWindow::separator:hover { background: %5; }
QMessageBox, QInputDialog, QFileDialog { background: %1; }
)")
        .arg(t.window.name(), t.surface.name(), t.text.name(), hover, t.primary.name(),
             t.primary.lighter(115).name(), t.textSecondary.name(), t.selection.name());
}

// ------------------------------------------------------------ sessão

void MainWindow::restoreSession()
{
    QSettings s;
    applyFontSize(s.value(QStringLiteral("editor/fontSize"), 11).toInt());
    applyTheme(s.value(QStringLiteral("ui/theme"), QStringLiteral("catppuccin-mocha")).toString());
    m_lastDir = s.value(QStringLiteral("ui/lastDir"), QDir::homePath()).toString();

    if (!restoreGeometry(s.value(QStringLiteral("ui/geometry")).toByteArray()))
        resize(1200, 800);
    if (!restoreState(s.value(QStringLiteral("ui/state")).toByteArray())) {
        resizeDocks({m_terminal}, {260}, Qt::Vertical);
    }

    const QStringList files = s.value(QStringLiteral("session/files")).toStringList();
    for (const QString &f : files) {
        if (QFileInfo::exists(f))
            openFile(f);
    }
    const int current = s.value(QStringLiteral("session/current"), 0).toInt();
    if (current >= 0 && current < m_tabs->count())
        m_tabs->setCurrentIndex(current);
}

void MainWindow::saveSession()
{
    QSettings s;
    s.setValue(QStringLiteral("editor/fontSize"), m_editorFont.pointSize());
    s.setValue(QStringLiteral("ui/theme"), m_theme.id);
    s.setValue(QStringLiteral("ui/lastDir"), m_lastDir);
    s.setValue(QStringLiteral("ui/geometry"), saveGeometry());
    s.setValue(QStringLiteral("ui/state"), saveState());
    QStringList files;
    for (int i = 0; i < m_tabs->count(); ++i) {
        if (!editorAt(i)->filePath().isEmpty())
            files << editorAt(i)->filePath();
    }
    s.setValue(QStringLiteral("session/files"), files);
    s.setValue(QStringLiteral("session/current"), m_tabs->currentIndex());
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    for (int i = 0; i < m_tabs->count(); ++i) {
        if (!maybeSave(editorAt(i))) {
            event->ignore();
            return;
        }
    }
    saveSession();
    event->accept();
}
