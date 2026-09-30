#include "MainWindow.h"
#include "CodeEditor.h"
#include "FindBar.h"
#include "Language.h"
#include "SettingsDialog.h"
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
#include <QListWidget>
#include <QMenuBar>
#include <QMessageBox>
#include <QSettings>
#include <QStatusBar>
#include <QTabBar>
#include <QTabWidget>
#include <QTextBlock>
#include <QTimer>
#include <QPushButton>
#include <QStackedWidget>
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
    m_stack = new QStackedWidget(central);
    m_stack->addWidget(m_tabs);
    m_stack->addWidget(buildEmptyState());
    layout->addWidget(m_stack, 1);
    layout->addWidget(m_findBar);
    setCentralWidget(central);

    addDockWidget(Qt::BottomDockWidgetArea, m_terminal);

    m_languageCombo->setFocusPolicy(Qt::NoFocus);
    statusBar()->addPermanentWidget(m_cursorLabel);
    statusBar()->addPermanentWidget(m_languageCombo);
    statusBar()->setSizeGripEnabled(false);

    connect(m_languageCombo, &QComboBox::activated, this, [this](int index) {
        if (CodeEditor *ed = currentEditor()) {
            ed->setLanguage(languageById(m_languageCombo->itemData(index).toString()));
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
    updateEmptyState();
    // Depuração: renderiza a janela (e opcionalmente uma página das configurações) em PNG e sai.
    if (qEnvironmentVariableIsSet("SLAYER_DEBUG_GRAB")) {
        QTimer::singleShot(800, this, [this]() {
            const QString out = qEnvironmentVariable("SLAYER_DEBUG_GRAB");
            if (qEnvironmentVariableIsSet("SLAYER_DEBUG_SETTINGS")) {
                showSettings();
                auto *dialog = findChild<SettingsDialog *>();
                if (auto *nav = dialog->findChild<QListWidget *>(QStringLiteral("SettingsNav")))
                    nav->setCurrentRow(qEnvironmentVariableIntValue("SLAYER_DEBUG_SETTINGS"));
                QTimer::singleShot(300, this, [dialog, out]() {
                    dialog->grab().save(out);
                    qApp->exit(0);
                });
                return;
            }
            grab().save(out);
            qApp->exit(0);
        });
    }
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
    edit->addSeparator();
    add(edit, tr("&Settings…"), QKeySequence(Qt::CTRL | Qt::Key_Comma),
                             [this]() { showSettings(); }, Icons::Settings);

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
    add(view, tr("Zoom &In"), QKeySequence::ZoomIn, [this]() { applyFontSize(m_settings.editorFontSize + 1); });
    add(view, tr("Zoom &Out"), QKeySequence::ZoomOut, [this]() { applyFontSize(m_settings.editorFontSize - 1); });
    add(view, tr("&Reset Zoom"), QKeySequence(Qt::CTRL | Qt::Key_0),
        [this]() { applyFontSize(AppSettings::defaults().editorFontSize); });

    QMenu *run = menuBar()->addMenu(tr("&Run"));
    m_runAction = add(run, tr("&Run File"), QKeySequence(Qt::Key_F5), [this]() { runCurrent(); }, Icons::Run);
    m_runAction->setToolTip(tr("Save and run the current file (F5)"));

    QMenu *help = menuBar()->addMenu(tr("&Help"));
    add(help, tr("&About Slayer Editor"), QKeySequence(), [this]() { showAbout(); }, Icons::Info);

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
    m_toolbar->addAction(m_runAction);

    if (auto *runButton = m_toolbar->widgetForAction(m_runAction)) {
        runButton->setObjectName(QStringLiteral("RunButton"));
    }
}

// --------------------------------------------------------------- abas

/** Tela exibida quando todas as abas estão fechadas. */
QWidget *MainWindow::buildEmptyState()
{
    auto *page = new QWidget;
    page->setObjectName(QStringLiteral("EmptyState"));
    page->setAttribute(Qt::WA_StyledBackground);
    auto *v = new QVBoxLayout(page);
    v->addStretch(1);

    auto *logo = new QLabel;
    logo->setPixmap(QPixmap(QStringLiteral(":/icons/slayer-editor.png"))
                        .scaled(128, 128, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    logo->setAlignment(Qt::AlignCenter);
    v->addWidget(logo);

    auto *title = new QLabel(QStringLiteral("Slayer Editor"));
    title->setObjectName(QStringLiteral("EmptyTitle"));
    title->setAlignment(Qt::AlignCenter);
    v->addWidget(title);

    auto *hint = new QLabel(tr("No open files"));
    hint->setObjectName(QStringLiteral("EmptyHint"));
    hint->setAlignment(Qt::AlignCenter);
    v->addWidget(hint);
    v->addSpacing(18);

    auto makeButton = [this, page](Icons::Glyph glyph, const QString &text, const QString &keys) {
        auto *b = new QPushButton(page);
        b->setObjectName(QStringLiteral("EmptyAction"));
        b->setText(text + QStringLiteral("    ") + keys);
        b->setCursor(Qt::PointingHandCursor);
        b->setMinimumWidth(260);
        m_emptyButtons.append({b, glyph});
        return b;
    };
    auto *newButton = makeButton(Icons::NewFile, tr("New File"), QStringLiteral("Ctrl+N"));
    auto *openButton = makeButton(Icons::Open, tr("Open File…"), QStringLiteral("Ctrl+O"));
    connect(newButton, &QPushButton::clicked, this, [this]() { newTab(); });
    connect(openButton, &QPushButton::clicked, this, [this]() {
        openFiles(QFileDialog::getOpenFileNames(this, tr("Open File"), m_lastDir));
    });
    for (QPushButton *b : {newButton, openButton}) {
        auto *row = new QHBoxLayout;
        row->addStretch(1);
        row->addWidget(b);
        row->addStretch(1);
        v->addLayout(row);
    }
    v->addStretch(2);
    return page;
}

void MainWindow::updateEmptyState()
{
    const bool empty = m_tabs->count() == 0;
    m_stack->setCurrentIndex(empty ? 1 : 0);
    if (empty) {
        m_findBar->hide();
        m_findBar->setEditor(nullptr);
    }
    updateStatus();
}

CodeEditor *MainWindow::newTab()
{
    auto *editor = new CodeEditor(m_tabs);
    configureEditor(editor);
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
    updateEmptyState();
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
    }
    editor->setLanguage(languageForPath(abs));
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
    editor->setLanguage(languageForPath(path));
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
    updateEmptyState();
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
    if (!editor) {
        m_cursorLabel->clear();
        m_languageCombo->setVisible(false);
        setWindowTitle(QStringLiteral("Slayer Editor"));
        return;
    }
    m_languageCombo->setVisible(true);
    const QTextCursor c = editor->textCursor();
    m_cursorLabel->setText(tr("Ln %1, Col %2").arg(c.blockNumber() + 1).arg(c.positionInBlock() + 1));
    const int idx = m_languageCombo->findData(editor->language().id);
    m_languageCombo->setCurrentIndex(idx);
    const QString title = editor->displayName() + (editor->document()->isModified() ? QStringLiteral(" ●") : QString());
    setWindowTitle(title + QStringLiteral(" — Slayer Editor"));
}

// ----------------------------------------------------------- executar

void MainWindow::runCurrent()
{
    CodeEditor *editor = currentEditor();
    if (!editor)
        return;
    // Sem título: salvar primeiro define o nome e, com ele, a linguagem.
    if (!save(editor))
        return;
    if (editor->language().runCommand.isEmpty()) {
        statusBar()->showMessage(tr("No run command for %1 files — set one in Settings → Languages.")
                                     .arg(editor->language().displayName), 5000);
        return;
    }
    const QFileInfo info(editor->filePath());
    const Language &lang = editor->language();
    QString tool, flags;
    if (lang.isCustom()) {
        for (const CustomLanguage &c : std::as_const(m_settings.customLanguages)) {
            if (QStringLiteral("custom:") + c.name == lang.id) {
                tool = c.tool;
                flags = c.flags;
            }
        }
    } else {
        const LanguageConfig cfg = m_settings.languageConfig(lang.id);
        tool = cfg.tool;
        flags = cfg.flags;
    }
    m_terminal->runCommand(lang.commandFor(info.absoluteFilePath(), tool, flags), info.absolutePath());
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
    AppSettings s = m_settings;
    s.theme = themeId;
    applySettings(s);
}

void MainWindow::applyFontSize(int pointSize)
{
    AppSettings s = m_settings;
    s.editorFontSize = qBound(6, pointSize, 48);
    // O terminal acompanha o zoom quando os tamanhos eram iguais.
    if (m_settings.terminalFontSize == m_settings.editorFontSize)
        s.terminalFontSize = s.editorFontSize;
    applySettings(s);
}

QFont MainWindow::editorFont() const
{
    QFont f(m_settings.editorFont.isEmpty() ? Icons::monoFamily() : m_settings.editorFont);
    f.setStyleHint(QFont::Monospace);
    f.setPointSize(m_settings.editorFontSize);
    return f;
}

QFont MainWindow::terminalFont() const
{
    QFont f(m_settings.terminalFont.isEmpty() ? Icons::monoFamily() : m_settings.terminalFont);
    f.setStyleHint(QFont::Monospace);
    f.setPointSize(m_settings.terminalFontSize);
    return f;
}

void MainWindow::configureEditor(CodeEditor *editor) const
{
    editor->setTheme(m_theme);
    editor->setEditorFont(editorFont());
    editor->setTabWidthSpaces(m_settings.tabWidth);
    editor->setLineNumbersVisible(m_settings.lineNumbers);
    editor->setWordWrap(m_settings.wordWrap);
    editor->setHighlightCurrentLine(m_settings.highlightCurrentLine);
    editor->setAutoClosePairs(m_settings.autoClosePairs);
    editor->setCodeCompletion(m_settings.codeCompletion);
}

void MainWindow::applySettings(const AppSettings &settings)
{
    m_settings = settings;
    m_settings.save();
    m_theme = Theme::byId(m_settings.theme);

    {
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
    }

    QApplication::setCursorFlashTime(m_settings.editorCursorBlink ? 1000 : 0);
    const int iconSize = qRound(18 * m_settings.buttonScale / 100.0);
    m_toolbar->setIconSize(QSize(iconSize, iconSize));

    for (int i = 0; i < m_tabs->count(); ++i) {
        CodeEditor *ed = editorAt(i);
        configureEditor(ed);
        // Extensões e linguagens personalizadas podem ter mudado.
        if (!ed->filePath().isEmpty())
            ed->setLanguage(languageForPath(ed->filePath()));
        updateTabTitle(ed);
    }
    rebuildLanguageCombo();

    m_terminal->setTheme(m_theme);
    m_terminal->setTerminalFont(terminalFont());
    m_terminal->configure(m_settings.shell, m_settings.terminalCursorBlink, m_settings.blockDestructiveCommands);
    refreshIcons();
    updateStatus();
}

void MainWindow::showSettings()
{
    auto *dialog = new SettingsDialog(m_settings, m_theme, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    connect(dialog, &SettingsDialog::applied, this, [this](const AppSettings &s) {
        if (s.uiScale != m_settings.uiScale)
            statusBar()->showMessage(tr("The interface scale applies after restarting Slayer Editor."), 5000);
        applySettings(s);
    });
    dialog->open();
}

void MainWindow::rebuildLanguageCombo()
{
    m_languageCombo->clear();
    for (const Language &lang : Language::all())
        m_languageCombo->addItem(Icons::icon(Icons::forLanguage(lang.id), Icons::colorForLanguage(lang.id), 14),
                                 lang.displayName, lang.id);
    for (const CustomLanguage &c : std::as_const(m_settings.customLanguages))
        m_languageCombo->addItem(Icons::icon(Icons::File, Icons::colorForLanguage(QString()), 14), c.name,
                                 QStringLiteral("custom:") + c.name);
    m_languageCombo->addItem(Icons::icon(Icons::File, Icons::colorForLanguage(QString()), 14),
                             Language::plainText().displayName, Language::plainText().id);
}

Language MainWindow::languageById(const QString &id) const
{
    if (id.startsWith(QLatin1String("custom:"))) {
        for (const CustomLanguage &c : m_settings.customLanguages) {
            if (QStringLiteral("custom:") + c.name == id)
                return Language::custom(c.name, Language::splitExtensions(c.extensions));
        }
        return Language::plainText();
    }
    return Language::byId(id);
}

/** Extensões configuradas em Settings → Languages; personalizadas têm prioridade. */
Language MainWindow::languageForPath(const QString &path) const
{
    const QString ext = QFileInfo(path).suffix().toLower();
    if (ext.isEmpty())
        return Language::plainText();
    for (const CustomLanguage &c : m_settings.customLanguages) {
        if (Language::splitExtensions(c.extensions).contains(ext))
            return Language::custom(c.name, Language::splitExtensions(c.extensions));
    }
    for (const Language &lang : Language::all()) {
        if (Language::splitExtensions(m_settings.languageConfig(lang.id).extensions).contains(ext))
            return lang;
    }
    return Language::plainText();
}

void MainWindow::refreshIcons()
{
    for (const auto &[button, glyph] : std::as_const(m_emptyButtons))
        button->setIcon(Icons::icon(glyph, m_theme.primary, 18));
    const int size = m_toolbar->iconSize().width();
    for (const auto &[action, glyph] : std::as_const(m_iconActions))
        action->setIcon(Icons::icon(glyph, action == m_runAction ? m_theme.window : m_theme.textSecondary, size));
    for (int i = 0; i < m_tabs->count(); ++i) {
        if (auto *b = qobject_cast<QToolButton *>(m_tabs->tabBar()->tabButton(i, QTabBar::RightSide)))
            b->setIcon(Icons::icon(Icons::Close, m_theme.textSecondary, 12));
    }
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
QToolButton#RunButton { background: %5; color: %1; padding: 6px 10px; }
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
QDialog#SettingsDialog { background: %2; }
#EmptyState { background: %2; }
QLabel#EmptyTitle { color: %3; font-size: 26px; font-weight: 700; margin-top: 10px; }
QLabel#EmptyHint { color: %7; font-size: 14px; }
QPushButton#EmptyAction { background: %1; color: %3; border: 1px solid %4; border-radius: 12px;
                          padding: 12px 20px; text-align: left; font-size: 14px; margin: 4px; }
QPushButton#EmptyAction:hover { border-color: %5; background: %4; }
QListWidget#SettingsNav { background: %1; border: none; border-right: 1px solid %4; padding: 12px 8px;
                          outline: 0; }
QListWidget#SettingsNav::item { color: %3; padding: 4px 10px; border-radius: 8px; margin: 1px 0; }
QListWidget#SettingsNav::item:hover { background: %4; }
QListWidget#SettingsNav::item:selected { background: %4; color: %5; font-weight: 600; }
QScrollArea, QScrollArea > QWidget > QWidget { background: %2; }
QLabel#PageTitle { color: %3; font-size: 20px; font-weight: 700; }
QLabel#PageSubtitle, QLabel#Hint { color: %7; }
#Card { background: %1; border: 1px solid %4; border-radius: 12px; }
#Card QLabel { background: transparent; }
QLabel#CardTitle { color: %5; font-weight: 600; letter-spacing: 0.4px; }
QPushButton#Danger { color: #f38ba8; }
QPushButton#Danger:hover { background: #f38ba8; color: %1; border-color: #f38ba8; }
QPushButton:disabled { color: %7; background: transparent; }
QSpinBox, QComboBox#Field, QFontComboBox { background: %2; color: %3; border: 1px solid %4; border-radius: 8px;
                                           padding: 4px 8px; min-height: 22px; }
QSpinBox:focus, QFontComboBox:focus { border: 1px solid %5; }
#Card QComboBox { background: %2; color: %3; border: 1px solid %4; border-radius: 8px; padding: 4px 8px; }
QListWidget#ProfileList, QListWidget#ThemeList, QTableWidget#LanguageTable {
    background: %2; color: %3; border: 1px solid %4; border-radius: 10px; padding: 4px; outline: 0; }
QListWidget#ProfileList::item { padding: 6px 8px; border-radius: 6px; }
QListWidget#ProfileList::item:selected, QListWidget#ThemeList::item:selected { background: %4; color: %3; }
QListWidget#ThemeList::item { border-radius: 10px; padding: 6px; }
QTableWidget#LanguageTable { alternate-background-color: %1; gridline-color: %4; }
QTableWidget#LanguageTable::item { padding: 4px 6px; }
QTableWidget#LanguageTable::item:selected { background: %4; color: %3; }
QHeaderView::section { background: %1; color: %7; border: none; border-bottom: 1px solid %4;
                       padding: 6px 8px; font-weight: 600; }
QSlider::groove:horizontal { height: 4px; background: %4; border-radius: 2px; }
QSlider::sub-page:horizontal { background: %5; border-radius: 2px; }
QSlider::handle:horizontal { background: %5; width: 16px; height: 16px; margin: -6px 0; border-radius: 8px; }
QCheckBox::indicator { width: 16px; height: 16px; border-radius: 5px; border: 1px solid %7; background: %2; }
QCheckBox::indicator:checked { background: %5; border-color: %5; image: url(%9); }
QSpinBox::up-button, QSpinBox::down-button { subcontrol-origin: border; width: 18px; border: none;
                                             background: transparent; }
QSpinBox::up-button { subcontrol-position: top right; }
QSpinBox::down-button { subcontrol-position: bottom right; }
QSpinBox::up-arrow { image: url(%10); width: 10px; height: 10px; }
QSpinBox::down-arrow, QComboBox::down-arrow { image: url(%11); width: 10px; height: 10px; }
QComboBox::drop-down { border: none; width: 22px; background: transparent; }
)")
        .arg(t.window.name(), t.surface.name(), t.text.name(), hover, t.primary.name(),
             t.primary.lighter(115).name(), t.textSecondary.name(), t.selection.name(),
             Icons::glyphFile(Icons::Check, t.window, 14),
             Icons::glyphFile(Icons::ChevronUp, t.textSecondary, 12),
             Icons::glyphFile(Icons::ChevronDown, t.textSecondary, 12));
}

// ------------------------------------------------------------ sessão

void MainWindow::restoreSession()
{
    QSettings s;
    applySettings(AppSettings::load());
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
    m_settings.save();
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
