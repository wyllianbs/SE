#include "MainWindow.h"
#include "CodeEditor.h"
#include "FindBar.h"
#include "Language.h"
#include "HelpDialog.h"
#include "LspManager.h"
#include "SettingsDialog.h"
#include "TerminalDock.h"
#include "ViewSpace.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QGuiApplication>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QSettings>
#include <QStatusBar>
#include <QTabBar>
#include <QTabWidget>
#include <QTextBlock>
#include <QTimer>
#include <QPushButton>
#include <QSet>
#include <QStringConverter>
#include <QTextDocument>
#include <QSplitter>
#include <QStackedWidget>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_findBar(new FindBar(this))
    , m_terminal(new TerminalDock(this))
    , m_languageCombo(new QComboBox(this))
    , m_cursorLabel(new QLabel(this))
    , m_theme(Theme::all().first())
    , m_lastDir(QDir::homePath())
    , m_lsp(new LspManager(this))
{
    connect(m_lsp, &LspManager::statusChanged, this, [this]() {
        // Gatilhos de completion chegam com o "initialize" do servidor.
        for (CodeEditor *e : allEditors())
            configureEditor(e);
        updateStatus();
    });
    connect(m_lsp, &LspManager::message, this, [this](const QString &text) { statusBar()->showMessage(text, 6000); });
    // Terminal transparente: a janela precisa nascer com fundo translúcido (o
    // atributo só vale antes de ela ser criada). Todas as outras áreas pintam
    // o próprio fundo pela folha de estilo, então só o terminal fica vazado.
    if (AppSettings::load().terminalTransparency > 0) {
        setAttribute(Qt::WA_TranslucentBackground);
        m_translucent = true;
    }
    setWindowTitle(QStringLiteral("Slayer Editor"));
    setWindowIcon(QIcon(QStringLiteral(":/icons/slayer-editor.png")));
    setDockOptions(QMainWindow::AnimatedDocks | QMainWindow::AllowTabbedDocks);
    setCorner(Qt::BottomLeftCorner, Qt::LeftDockWidgetArea);
    setCorner(Qt::BottomRightCorner, Qt::RightDockWidgetArea);

    auto *central = new QWidget(this);
    central->setObjectName(QStringLiteral("Central"));
    auto *layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    m_rootSplitter = new QSplitter(Qt::Horizontal, central);
    m_rootSplitter->setChildrenCollapsible(false);
    m_stack = new QStackedWidget(central);
    m_stack->addWidget(m_rootSplitter);
    m_stack->addWidget(buildEmptyState());
    layout->addWidget(m_stack, 1);
    layout->addWidget(m_findBar);
    setCentralWidget(central);
    setActiveSpace(createViewSpace());
    m_rootSplitter->addWidget(m_activeSpace);

    addDockWidget(Qt::BottomDockWidgetArea, m_terminal);

    buildStatusBar();

    connect(m_languageCombo, &QComboBox::activated, this, [this](int index) {
        if (CodeEditor *ed = currentEditor())
            ed->setLanguage(languageById(m_languageCombo->itemData(index).toString()));
    });
    // O espaço de visão ativo é o do último widget focado.
    connect(qApp, &QApplication::focusChanged, this, [this](QWidget *, QWidget *now) {
        for (QWidget *w = now; w; w = w->parentWidget()) {
            if (auto *space = qobject_cast<ViewSpace *>(w)) {
                setActiveSpace(space);
                return;
            }
        }
    });

    createActions();
    restoreSession();
    updateEmptyState();
    // Depuração: renderiza a janela (e opcionalmente uma página das configurações) em PNG e sai.
    if (qEnvironmentVariableIsSet("SLAYER_DEBUG_GRAB")) {
        QTimer::singleShot(qEnvironmentVariableIntValue("SLAYER_DEBUG_DELAY") + 900, this, [this]() {
            const QString out = qEnvironmentVariable("SLAYER_DEBUG_GRAB");
            if (qEnvironmentVariableIsSet("SLAYER_DEBUG_HELP")) {
                showHelp();
                auto *help = findChild<HelpDialog *>();
                help->findChild<QLineEdit *>()->setText(qEnvironmentVariable("SLAYER_DEBUG_HELP"));
                QTimer::singleShot(300, this, [help, out]() {
                    help->grab().save(out);
                    qApp->exit(0);
                });
                return;
            }
            if (qEnvironmentVariableIsSet("SLAYER_DEBUG_SETTINGS")) {
                showSettings();
                auto *dialog = findChild<SettingsDialog *>();
                if (qEnvironmentVariableIsSet("SLAYER_DEBUG_SIZE")) {
                    const QStringList wh = qEnvironmentVariable("SLAYER_DEBUG_SIZE").split(QLatin1Char('x'));
                    dialog->resize(wh.value(0).toInt(), wh.value(1).toInt());
                }
                if (auto *nav = dialog->findChild<QListWidget *>(QStringLiteral("SettingsNav")))
                    nav->setCurrentRow(qEnvironmentVariableIntValue("SLAYER_DEBUG_SETTINGS"));
                QTimer::singleShot(300, this, [dialog, out]() {
                    dialog->grab().save(out);
                    qApp->exit(0);
                });
                return;
            }
            // SLAYER_DEBUG_TERMTABS: abre abas extras de terminal.
            for (int i = 1; i < qEnvironmentVariableIntValue("SLAYER_DEBUG_TERMTABS"); ++i)
                m_terminal->newTerminal();
            // SLAYER_DEBUG_FOLD: dobra o bloco que começa na linha dada (base 1).
            if (qEnvironmentVariableIsSet("SLAYER_DEBUG_FOLD") && currentEditor())
                currentEditor()->doc()->setFolded(qEnvironmentVariableIntValue("SLAYER_DEBUG_FOLD") - 1, true);
            // SLAYER_DEBUG_SPLIT: sequência de divisões, "h" lado a lado e "v" empilhado.
            for (QChar c : qEnvironmentVariable("SLAYER_DEBUG_SPLIT"))
                splitSpace(m_activeSpace, c == QLatin1Char('h') ? Qt::Horizontal : Qt::Vertical, false);
            QTimer::singleShot(300, this, [this, out]() {
                grab().save(out);
                qApp->exit(0);
            });
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
    QAction *saveAsA = add(file, tr("Save &As…"), QKeySequence::SaveAs, [this]() { saveAs(currentEditor()); }, Icons::SaveAs);
    QAction *saveAllA = add(file, tr("Save A&ll"), QKeySequence(Qt::CTRL | Qt::Key_L), [this]() { saveAll(); }, Icons::SaveAll);
    saveAsA->setToolTip(tr("Save As (Ctrl+Shift+S)"));
    saveAllA->setToolTip(tr("Save All (Ctrl+L)"));
    file->addSeparator();
    add(file, tr("&Close Tab"), QKeySequence(Qt::CTRL | Qt::Key_W), [this]() {
        closeTab(m_activeSpace, m_activeSpace->currentIndex());
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
    QMenu *folding = edit->addMenu(tr("Code &Folding"));
    add(folding, tr("&Fold Block"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_BracketLeft), [this]() {
        if (auto *e = currentEditor()) e->foldAtCursor(true);
    });
    add(folding, tr("&Unfold Block"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_BracketRight), [this]() {
        if (auto *e = currentEditor()) e->foldAtCursor(false);
    });
    add(folding, tr("Fold &All"), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_BracketLeft), [this]() {
        if (auto *e = currentEditor()) e->doc()->foldAll();
    });
    add(folding, tr("Unfold A&ll"), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_BracketRight), [this]() {
        if (auto *e = currentEditor()) e->doc()->unfoldAll();
    });
    edit->addSeparator();
    // Chaves rápidas das opções de Settings → Editor → Features.
    auto toggle = [this, edit](const QString &text, bool AppSettings::*field, const QString &on, const QString &off) {
        QAction *a = edit->addAction(text);
        a->setCheckable(true);
        connect(a, &QAction::toggled, this, [this, field, on, off](bool checked) {
            if (m_settings.*field == checked)
                return;
            AppSettings s = m_settings;
            s.*field = checked;
            applySettings(s);
            statusBar()->showMessage(checked ? on : off, 3000);
        });
        m_settingToggles.append({a, field});
    };
    toggle(tr("Code Com&pletion"), &AppSettings::codeCompletion, tr("Code completion enabled"),
           tr("Code completion disabled"));
    toggle(tr("Auto-Close &Brackets and Quotes"), &AppSettings::autoClosePairs, tr("Auto-closing pairs enabled"),
           tr("Auto-closing pairs disabled"));
    toggle(tr("Code F&olding Markers"), &AppSettings::codeFolding, tr("Code folding enabled"),
           tr("Code folding disabled"));
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
    add(view, tr("New Terminal Ta&b"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_QuoteLeft), [this]() {
        m_terminal->show();
        m_terminal->newTerminal();
    }, Icons::Add);
    QMenu *pos = view->addMenu(tr("Terminal &Position"));
    const QList<QPair<QString, QString>> positions{
        {tr("Left"), "left"}, {tr("Top"), "top"}, {tr("Right"), "right"}, {tr("Bottom"), "bottom"}};
    for (const auto &[label, key] : positions) {
        const QString k = key;
        connect(pos->addAction(label), &QAction::triggered, this,
                [this, k]() { m_terminal->moveToArea(TerminalDock::areaFromKey(k)); });
    }
    view->addSeparator();
    QMenu *split = view->addMenu(tr("&Split View"));
    add(split, tr("Split &Vertical"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_L),
        [this]() { splitSpace(m_activeSpace, Qt::Horizontal, false); }, Icons::SplitSideBySide);
    add(split, tr("Split &Horizontal"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_T),
        [this]() { splitSpace(m_activeSpace, Qt::Vertical, false); }, Icons::SplitStacked);
    add(split, tr("&Close Current View"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_R),
        [this]() { closeSpace(m_activeSpace); }, Icons::Close);
    add(split, tr("Next View"), QKeySequence(Qt::Key_F8), [this]() { cycleSpace(1); });
    add(split, tr("Previous View"), QKeySequence(Qt::SHIFT | Qt::Key_F8), [this]() { cycleSpace(-1); });
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
    run->addSeparator();
    // Atalho para a chave geral de Settings → Editor → Features.
    m_lspAction = run->addAction(tr("Language Servers (&LSP)"));
    m_lspAction->setCheckable(true);
    connect(m_lspAction, &QAction::toggled, this, [this](bool on) {
        if (on == m_settings.lspEnabled)
            return;
        AppSettings s = m_settings;
        s.lspEnabled = on;
        applySettings(s);
        statusBar()->showMessage(on ? tr("Language servers enabled") : tr("Language servers disabled"), 3000);
    });

    QMenu *help = menuBar()->addMenu(tr("&Help"));
    add(help, tr("Slayer Editor &Help"), QKeySequence(Qt::Key_F1), [this]() { showHelp(); }, Icons::Help);

    QMenu *about = menuBar()->addMenu(tr("&About"));
    add(about, tr("&About Slayer Editor"), QKeySequence(), [this]() { showAbout(); }, Icons::Info);
    add(about, tr("About &Qt"), QKeySequence(), [this]() { QMessageBox::aboutQt(this); });

    // Barra de ferramentas plana, só ícones.
    m_toolbar = addToolBar(tr("Main Toolbar"));
    m_toolbar->setObjectName(QStringLiteral("MainToolbar"));
    m_toolbar->setMovable(false);
    m_toolbar->setIconSize(QSize(18, 18));
    m_toolbar->addActions({newA, openA, saveA, saveAsA, saveAllA});
    m_toolbar->addSeparator();
    m_toolbar->addActions({undoA, redoA, findA});
    m_toolbar->addSeparator();
    m_toolbar->addAction(termA);
    // Ação própria para o botão da barra: ícone escuro sobre o fundo azul; no
    // menu, o ícone de Run usa a cor dos demais.
    m_runToolbarAction = new QAction(tr("Run"), this);
    m_runToolbarAction->setToolTip(m_runAction->toolTip());
    connect(m_runToolbarAction, &QAction::triggered, this, [this]() { runCurrent(); });
    m_toolbar->addAction(m_runToolbarAction);

    if (auto *runButton = m_toolbar->widgetForAction(m_runToolbarAction)) {
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
    const bool empty = allEditors().isEmpty();
    m_stack->setCurrentIndex(empty ? 1 : 0);
    if (empty) {
        m_findBar->hide();
        m_findBar->setEditor(nullptr);
    }
    updateStatus();
}

// ------------------------------------------------------- espaços de visão

ViewSpace *MainWindow::createViewSpace()
{
    auto *space = new ViewSpace;
    space->setTheme(m_theme);
    connect(space, &QTabWidget::tabCloseRequested, this, [this, space](int i) { closeTab(space, i); });
    connect(space, &QTabWidget::currentChanged, this, [this, space]() {
        if (space == m_activeSpace) {
            m_findBar->setEditor(currentEditor());
            updateStatus();
        }
    });
    connect(space, &ViewSpace::splitRequested, this, &MainWindow::splitSpace);
    connect(space, &ViewSpace::newTabRequested, this, [this](ViewSpace *s) {
        setActiveSpace(s);
        newTab();
    });
    connect(space, &ViewSpace::tabActionRequested, this, &MainWindow::tabAction);
    connect(space, &ViewSpace::closeViewRequested, this, &MainWindow::closeSpace);
    connect(space, &ViewSpace::closeInactiveViewsRequested, this, [this](ViewSpace *keep) {
        for (ViewSpace *s : viewSpaces()) {
            if (s != keep && !closeSpace(s))
                return;
        }
    });
    connect(space, &ViewSpace::hideInactiveViewsToggled, this, [this](ViewSpace *keep, bool hide) {
        m_inactiveHidden = hide;
        for (ViewSpace *s : viewSpaces())
            s->setVisible(!hide || s == keep);
        updateSpaceActions();
    });
    return space;
}

QList<ViewSpace *> MainWindow::viewSpaces() const
{
    return m_rootSplitter->findChildren<ViewSpace *>();
}

QList<CodeEditor *> MainWindow::allEditors() const
{
    QList<CodeEditor *> list;
    for (ViewSpace *s : viewSpaces())
        list += s->editors();
    return list;
}

QList<CodeEditor *> MainWindow::editorsFor(Document *doc) const
{
    QList<CodeEditor *> list;
    for (CodeEditor *e : allEditors()) {
        if (e->doc() == doc)
            list << e;
    }
    return list;
}

void MainWindow::setActiveSpace(ViewSpace *space)
{
    if (!space || space == m_activeSpace)
        return;
    if (m_activeSpace)
        m_activeSpace->setActive(false);
    m_activeSpace = space;
    space->setActive(true);
    m_findBar->setEditor(currentEditor());
    updateStatus();
}

void MainWindow::updateSpaceActions()
{
    const QList<ViewSpace *> spaces = viewSpaces();
    for (ViewSpace *s : spaces)
        s->setMultipleViews(spaces.size() > 1, m_inactiveHidden);
}

/**
 * Divide o espaço de visão. Qt::Horizontal põe as visões lado a lado ("Split
 * Vertical" no Kate); Qt::Vertical as empilha. Com moveDocument, a aba atual
 * sai do espaço original em vez de ser duplicada.
 */
void MainWindow::splitSpace(ViewSpace *space, Qt::Orientation orientation, bool moveDocument)
{
    CodeEditor *source = space ? space->currentEditor() : nullptr;
    if (!source)
        return;
    if (m_inactiveHidden) {
        m_inactiveHidden = false;
        for (ViewSpace *s : viewSpaces())
            s->show();
    }

    auto *parent = qobject_cast<QSplitter *>(space->parentWidget());
    ViewSpace *fresh = createViewSpace();
    const int index = parent->indexOf(space);
    if (parent->count() == 1 || parent->orientation() == orientation) {
        parent->setOrientation(orientation);
        parent->insertWidget(index + 1, fresh);
        QList<int> sizes;
        const int each = (orientation == Qt::Horizontal ? parent->width() : parent->height()) / parent->count();
        for (int i = 0; i < parent->count(); ++i)
            sizes << each;
        parent->setSizes(sizes);
    } else {
        // Direção diferente: um novo divisor ocupa o lugar do espaço original.
        const QList<int> sizes = parent->sizes();
        auto *child = new QSplitter(orientation);
        child->setChildrenCollapsible(false);
        parent->replaceWidget(index, child);
        child->addWidget(space);
        child->addWidget(fresh);
        parent->setSizes(sizes);
        const int half = (orientation == Qt::Horizontal ? space->width() : space->height()) / 2;
        child->setSizes({half, half});
    }

    CodeEditor *view = openView(source->doc(), fresh);
    view->setTextCursor(source->textCursor());
    view->centerCursor();
    if (moveDocument)
        closeTab(space, space->indexOf(source));
    setActiveSpace(fresh);
    view->setFocus();
    updateSpaceActions();
}

/** Fecha um espaço de visão; documentos sem outra visão pedem para salvar. */
bool MainWindow::closeSpace(ViewSpace *space)
{
    if (!space || viewSpaces().size() < 2)
        return false;
    for (CodeEditor *e : space->editors()) {
        if (editorsFor(e->doc()).size() == 1 && !maybeSave(e))
            return false;
    }
    for (CodeEditor *e : space->editors())
        releaseView(e);
    removeSpace(space);
    return true;
}

/** Retira o espaço da árvore de divisores e desfaz divisores com um só filho. */
void MainWindow::removeSpace(ViewSpace *space)
{
    auto *parent = qobject_cast<QSplitter *>(space->parentWidget());
    const bool wasActive = (space == m_activeSpace);
    space->hide();
    space->setParent(nullptr);
    space->deleteLater();
    if (wasActive)
        m_activeSpace = nullptr;

    while (parent && parent != m_rootSplitter && parent->count() == 1) {
        auto *grand = qobject_cast<QSplitter *>(parent->parentWidget());
        QWidget *only = parent->widget(0);
        const int idx = grand->indexOf(parent);
        grand->replaceWidget(idx, only);
        parent->deleteLater();
        parent = grand;
    }
    if (m_rootSplitter->count() == 1) {
        if (auto *inner = qobject_cast<QSplitter *>(m_rootSplitter->widget(0))) {
            // A raiz herda a orientação e os filhos do único divisor restante.
            m_rootSplitter->setOrientation(inner->orientation());
            const QList<int> sizes = inner->sizes();
            while (inner->count() > 0)
                m_rootSplitter->addWidget(inner->widget(0));
            inner->deleteLater();
            m_rootSplitter->setSizes(sizes);
        }
    }
    if (!m_activeSpace) {
        const QList<ViewSpace *> spaces = viewSpaces();
        setActiveSpace(spaces.isEmpty() ? nullptr : spaces.first());
        if (CodeEditor *e = currentEditor())
            e->setFocus();
    }
    m_inactiveHidden = false;
    for (ViewSpace *s : viewSpaces())
        s->show();
    updateSpaceActions();
    updateEmptyState();
}

void MainWindow::cycleSpace(int step)
{
    const QList<ViewSpace *> spaces = viewSpaces();
    if (spaces.size() < 2)
        return;
    const int i = (spaces.indexOf(m_activeSpace) + step + spaces.size()) % spaces.size();
    setActiveSpace(spaces[i]);
    if (CodeEditor *e = currentEditor())
        e->setFocus();
}

// --------------------------------------------------------------- abas

Document *MainWindow::createDocument()
{
    auto *doc = new Document(this);
    doc->setTheme(m_theme);
    doc->setFoldingEnabled(m_settings.codeFolding);
    connect(doc, &Document::changed, this, [this, doc]() {
        updateDocumentTitles(doc);
        m_lsp->attach(doc);
    });
    connect(doc, &Document::diagnosticsChanged, this, [this, doc]() {
        if (CodeEditor *e = currentEditor(); e && e->doc() == doc)
            updateStatus();
    });
    return doc;
}

CodeEditor *MainWindow::openView(Document *doc, ViewSpace *space)
{
    auto *editor = new CodeEditor(doc, space);
    configureEditor(editor);
    const int index = space->addTab(editor, QString());
    auto *closeButton = new QToolButton(space->tabBar());
    closeButton->setObjectName(QStringLiteral("TabClose"));
    closeButton->setAutoRaise(true);
    closeButton->setIconSize(QSize(12, 12));
    closeButton->setIcon(Icons::icon(Icons::Close, m_theme.textSecondary, 12));
    closeButton->setToolTip(tr("Close tab"));
    connect(closeButton, &QToolButton::clicked, this,
            [this, space, editor]() { closeTab(space, space->indexOf(editor)); });
    space->tabBar()->setTabButton(index, QTabBar::RightSide, closeButton);
    space->setCurrentIndex(index);
    connect(editor, &QPlainTextEdit::cursorPositionChanged, this, [this, editor]() {
        if (editor == currentEditor())
            updateStatus();
    });
    connect(editor, &CodeEditor::overwriteModeToggled, this, [this]() { updateStatus(); });
    updateDocumentTitles(doc);
    updateEmptyState();
    return editor;
}

CodeEditor *MainWindow::newTab()
{
    if (!m_activeSpace)
        setActiveSpace(viewSpaces().first());
    CodeEditor *editor = openView(createDocument(), m_activeSpace);
    editor->setFocus();
    return editor;
}

CodeEditor *MainWindow::currentEditor() const
{
    return m_activeSpace ? m_activeSpace->currentEditor() : nullptr;
}

void MainWindow::openFiles(const QStringList &paths)
{
    for (const QString &p : paths)
        openFile(p);
}

void MainWindow::openFile(const QString &path)
{
    const QString abs = QFileInfo(path).absoluteFilePath();

    // Já aberto: mostra no espaço ativo (criando uma visão, se preciso).
    for (CodeEditor *e : allEditors()) {
        if (e->filePath() != abs)
            continue;
        for (CodeEditor *mine : m_activeSpace->editors()) {
            if (mine->doc() == e->doc()) {
                m_activeSpace->setCurrentWidget(mine);
                mine->setFocus();
                return;
            }
        }
        openView(e->doc(), m_activeSpace)->setFocus();
        return;
    }

    // Reaproveita a aba "Sem título" vazia.
    CodeEditor *editor = currentEditor();
    if (!editor || !editor->doc()->isUntitledAndEmpty() || editorsFor(editor->doc()).size() > 1)
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
    m_lsp->documentSaved(editor->doc());
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
    m_lsp->documentSaved(editor->doc());
    updateStatus();
    return true;
}

bool MainWindow::maybeSave(CodeEditor *editor)
{
    if (!editor || !editor->doc()->isModified())
        return true;
    if (auto *space = qobject_cast<ViewSpace *>(editor->parentWidget()->parentWidget())) {
        space->setCurrentWidget(editor);
        setActiveSpace(space);
    }
    const auto answer = QMessageBox::question(
        this, tr("Unsaved Changes"),
        tr("“%1” has unsaved changes. Save them?").arg(editor->displayName()),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (answer == QMessageBox::Save)
        return save(editor);
    return answer == QMessageBox::Discard;
}

/** Tira a visão da aba; o documento é destruído quando não sobra visão dele. */
void MainWindow::releaseView(CodeEditor *editor)
{
    Document *doc = editor->doc();
    const bool last = editorsFor(doc).size() == 1;
    for (ViewSpace *s : viewSpaces()) {
        const int i = s->indexOf(editor);
        if (i >= 0)
            s->removeTab(i);
    }
    editor->deleteLater();
    if (last)
        doc->deleteLater();
}

bool MainWindow::closeTab(ViewSpace *space, int index)
{
    CodeEditor *editor = space ? space->editorAt(index) : nullptr;
    if (!editor)
        return false;
    if (editorsFor(editor->doc()).size() == 1 && !maybeSave(editor))
        return false;
    releaseView(editor);
    // Espaço dividido que ficou vazio é fechado, como no Kate.
    if (space->count() == 0 && viewSpaces().size() > 1)
        removeSpace(space);
    else
        updateEmptyState();
    return true;
}

void MainWindow::tabAction(ViewSpace *space, int index, const QString &action)
{
    CodeEditor *editor = space->editorAt(index);
    if (!editor)
        return;
    setActiveSpace(space);
    if (action == QLatin1String("save")) {
        save(editor);
    } else if (action == QLatin1String("saveAs")) {
        saveAs(editor);
    } else if (action == QLatin1String("rename")) {
        renameFile(editor);
    } else if (action == QLatin1String("copyPath")) {
        QGuiApplication::clipboard()->setText(editor->filePath());
        statusBar()->showMessage(tr("Copied %1").arg(editor->filePath()), 2500);
    } else if (action == QLatin1String("close")) {
        closeTab(space, index);
    } else if (action == QLatin1String("closeOthers")) {
        const QList<CodeEditor *> others = space->editors();
        for (CodeEditor *e : others) {
            if (e != editor && !closeTab(space, space->indexOf(e)))
                break;
        }
    }
}

/** Renomeia o arquivo no disco; aba sem título vira "Salvar como". */
void MainWindow::renameFile(CodeEditor *editor)
{
    Document *doc = editor->doc();
    if (doc->filePath().isEmpty() || !QFileInfo::exists(doc->filePath())) {
        saveAs(editor);
        return;
    }
    const QFileInfo info(doc->filePath());
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Rename File"), tr("New name:"), QLineEdit::Normal,
                                               info.fileName(), &ok)
                             .trimmed();
    if (!ok || name.isEmpty() || name == info.fileName())
        return;
    if (name.contains(QLatin1Char('/'))) {
        QMessageBox::warning(this, tr("Rename File"), tr("The name cannot contain “/”."));
        return;
    }
    const QString target = info.dir().filePath(name);
    if (QFileInfo::exists(target)) {
        QMessageBox::warning(this, tr("Rename File"), tr("“%1” already exists.").arg(name));
        return;
    }
    if (!QFile::rename(doc->filePath(), target)) {
        QMessageBox::warning(this, tr("Rename File"), tr("Could not rename “%1”.").arg(info.fileName()));
        return;
    }
    doc->setFilePath(target);
    doc->setLanguage(languageForPath(target));
    m_lsp->documentSaved(doc);
    statusBar()->showMessage(tr("Renamed to %1").arg(name), 2500);
}

void MainWindow::updateDocumentTitles(Document *doc)
{
    const QString modified = doc->isModified() ? QStringLiteral("  ●") : QString();
    const QString lang = doc->language().id;
    const QIcon icon = Icons::icon(Icons::forLanguage(lang), Icons::colorForLanguage(lang), 16);
    for (ViewSpace *s : viewSpaces()) {
        for (int i = 0; i < s->count(); ++i) {
            if (s->editorAt(i)->doc() != doc)
                continue;
            s->setTabText(i, doc->displayName() + modified);
            s->setTabToolTip(i, doc->filePath());
            s->setTabIcon(i, icon);
        }
    }
    if (CodeEditor *e = currentEditor(); e && e->doc() == doc)
        updateStatus();
}

/**
 * Rodapé no estilo do Kate: branch do git à esquerda; à direita linha:coluna,
 * modo de inserção, tabulação (menu), codificação e linguagem (menu).
 */
void MainWindow::buildStatusBar()
{
    statusBar()->setSizeGripEnabled(false);
    m_branchLabel = new QLabel(this);
    m_branchLabel->setObjectName(QStringLiteral("BranchLabel"));
    m_branchLabel->setTextFormat(Qt::RichText);
    statusBar()->addWidget(m_branchLabel);
    m_diagLabel = new QLabel(this);
    m_diagLabel->setObjectName(QStringLiteral("DiagLabel"));
    m_diagLabel->setTextFormat(Qt::RichText);
    m_diagLabel->setMinimumWidth(0);
    m_diagLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    statusBar()->addWidget(m_diagLabel, 1);
    m_lspLabel = new QLabel(this);
    m_lspLabel->setToolTip(tr("Language server"));

    m_modeLabel = new QLabel(this);
    m_modeLabel->setToolTip(tr("Insert key toggles insert/overwrite"));
    m_encodingButton = new QToolButton(this);
    m_encodingButton->setObjectName(QStringLiteral("StatusButton"));
    m_encodingButton->setPopupMode(QToolButton::InstantPopup);
    m_encodingButton->setAutoRaise(true);
    m_encodingButton->setToolTip(tr("Text encoding"));
    auto *encodingMenu = new QMenu(m_encodingButton);
    m_encodingButton->setMenu(encodingMenu);
    connect(encodingMenu, &QMenu::aboutToShow, this, [this, encodingMenu]() {
        encodingMenu->clear();
        CodeEditor *editor = currentEditor();
        if (!editor)
            return;
        Document *doc = editor->doc();
        auto addEncoding = [this, doc](QMenu *menu, const QString &label, const QString &name, bool bom) {
            QAction *a = menu->addAction(label);
            a->setCheckable(true);
            a->setChecked(doc->encoding().compare(name, Qt::CaseInsensitive) == 0 && doc->hasBom() == bom);
            connect(a, &QAction::triggered, this, [this, name, bom]() { changeEncoding(name, bom); });
        };
        const QList<std::tuple<QString, QString, bool>> common{
            {QStringLiteral("UTF-8"), QStringLiteral("UTF-8"), false},
            {tr("UTF-8 with BOM"), QStringLiteral("UTF-8"), true},
            {QStringLiteral("UTF-16 LE"), QStringLiteral("UTF-16LE"), true},
            {QStringLiteral("UTF-16 BE"), QStringLiteral("UTF-16BE"), true},
            {tr("ISO-8859-1 (Western European)"), QStringLiteral("ISO-8859-1"), false},
            {tr("ISO-8859-15 (Western European, €)"), QStringLiteral("ISO-8859-15"), false},
            {tr("Windows-1252 (Western European)"), QStringLiteral("windows-1252"), false},
            {QStringLiteral("US-ASCII"), QStringLiteral("US-ASCII"), false}};
        encodingMenu->addSection(doc->isModified() || doc->filePath().isEmpty()
                                     ? tr("Save with encoding") : tr("Reload with encoding"));
        for (const auto &[label, name, bom] : common)
            addEncoding(encodingMenu, label, name, bom);
        QMenu *more = encodingMenu->addMenu(tr("Other Encodings"));
        QStringList names = QStringConverter::availableCodecs();
        names.removeIf([](const QString &n) { return n.contains(QLatin1Char(',')) || n.contains(QLatin1Char('_')); });
        names.sort(Qt::CaseInsensitive);
        names.removeDuplicates();
        for (const QString &n : std::as_const(names))
            addEncoding(more, n, n, false);
    });

    m_tabsButton = new QToolButton(this);
    m_tabsButton->setObjectName(QStringLiteral("StatusButton"));
    m_tabsButton->setPopupMode(QToolButton::InstantPopup);
    m_tabsButton->setAutoRaise(true);
    auto *tabsMenu = new QMenu(m_tabsButton);
    m_tabsButton->setMenu(tabsMenu);
    connect(tabsMenu, &QMenu::aboutToShow, this, [this, tabsMenu]() {
        tabsMenu->clear();
        CodeEditor *editor = currentEditor();
        if (!editor)
            return;
        // Vale para o documento atual (todas as suas visões), como no Kate.
        const QList<CodeEditor *> views = editorsFor(editor->doc());
        QAction *soft = tabsMenu->addAction(tr("Insert Spaces (Soft Tabs)"));
        soft->setCheckable(true);
        soft->setChecked(editor->insertSpaces());
        connect(soft, &QAction::toggled, this, [this, views](bool on) {
            for (CodeEditor *v : views)
                v->setInsertSpaces(on);
            updateStatus();
        });
        tabsMenu->addSection(tr("Tab Width"));
        for (int w : {2, 3, 4, 8}) {
            QAction *a = tabsMenu->addAction(QString::number(w));
            a->setCheckable(true);
            a->setChecked(editor->tabWidth() == w);
            connect(a, &QAction::triggered, this, [this, views, w]() {
                for (CodeEditor *v : views)
                    v->setTabWidthSpaces(w);
                updateStatus();
            });
        }
    });

    m_languageCombo->setFocusPolicy(Qt::NoFocus);
    m_languageCombo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    for (QWidget *w : std::initializer_list<QWidget *>{m_lspLabel, m_cursorLabel, m_modeLabel, m_tabsButton,
                                                        m_encodingButton, m_languageCombo})
        statusBar()->addPermanentWidget(w);
}

/**
 * Como no Kate: arquivo salvo e sem alterações é relido na nova codificação;
 * caso contrário, ela passa a valer para o próximo salvamento.
 */
void MainWindow::changeEncoding(const QString &encoding, bool bom)
{
    CodeEditor *editor = currentEditor();
    if (!editor)
        return;
    Document *doc = editor->doc();
    if (!doc->filePath().isEmpty() && !doc->isModified() && QFileInfo::exists(doc->filePath())) {
        const int line = editor->textCursor().blockNumber();
        QString error;
        if (!doc->load(doc->filePath(), &error, encoding)) {
            QMessageBox::warning(this, tr("Encoding"), error);
            return;
        }
        doc->setEncoding(encoding, bom);
        editor->goToLine(line + 1);
        statusBar()->showMessage(tr("Reloaded as %1").arg(doc->encodingLabel()), 3000);
    } else {
        doc->setEncoding(encoding, bom);
        doc->text()->setModified(true);
        statusBar()->showMessage(tr("%1 will be saved as %2").arg(doc->displayName(), doc->encodingLabel()), 4000);
    }
    updateStatus();
}

/** Branch atual lendo .git/HEAD (sem executar o git). Vazio fora de repositório. */
QString MainWindow::gitBranchFor(const QString &filePath) const
{
    if (filePath.isEmpty())
        return QString();
    QDir dir = QFileInfo(filePath).absoluteDir();
    for (int depth = 0; depth < 64; ++depth) {
        const QFileInfo git(dir.filePath(QStringLiteral(".git")));
        QString gitDir;
        if (git.isDir()) {
            gitDir = git.absoluteFilePath();
        } else if (git.isFile()) {
            // Worktree/submódulo: ".git" é um arquivo "gitdir: <caminho>".
            QFile f(git.absoluteFilePath());
            if (f.open(QIODevice::ReadOnly)) {
                const QString line = QString::fromUtf8(f.readLine()).trimmed();
                if (line.startsWith(QLatin1String("gitdir:")))
                    gitDir = QDir(dir).absoluteFilePath(line.mid(7).trimmed());
            }
        }
        if (!gitDir.isEmpty()) {
            QFile head(gitDir + QStringLiteral("/HEAD"));
            if (!head.open(QIODevice::ReadOnly))
                return QString();
            const QString ref = QString::fromUtf8(head.readAll()).trimmed();
            if (ref.startsWith(QLatin1String("ref: refs/heads/")))
                return ref.mid(16);
            return ref.left(7); // HEAD destacado: hash curto
        }
        if (!dir.cdUp())
            break;
    }
    return QString();
}

void MainWindow::updateStatus()
{
    if (!m_branchLabel) // chamado durante a construção, antes do rodapé existir
        return;
    CodeEditor *editor = currentEditor();
    const bool has = editor != nullptr;
    for (QWidget *w : std::initializer_list<QWidget *>{m_cursorLabel, m_modeLabel, m_tabsButton, m_encodingButton,
                                                        m_languageCombo})
        w->setVisible(has);
    if (!has) {
        m_branchLabel->clear();
        m_diagLabel->clear();
        m_lspLabel->hide();
        setWindowTitle(QStringLiteral("Slayer Editor"));
        return;
    }
    const QTextCursor c = editor->textCursor();
    m_cursorLabel->setText(QStringLiteral("%1:%2").arg(c.blockNumber() + 1).arg(c.positionInBlock() + 1));
    m_modeLabel->setText(editor->overwriteMode() ? tr("OVERWRITE") : tr("INSERT"));
    m_encodingButton->setText(editor->doc()->encodingLabel());
    const QString server = m_lsp->statusFor(editor->doc());
    m_lspLabel->setVisible(!server.isEmpty());
    m_lspLabel->setText(QStringLiteral("<span style=\"font-family:'%1'\">%2</span>&nbsp;%3")
                            .arg(Icons::symbolFamily(), Icons::glyphText(Icons::Server), server.toHtmlEscaped()));
    // Diagnóstico da linha do cursor, como no Slayer Editor Android.
    const QList<Diagnostic> here = editor->doc()->diagnosticsAt(c.blockNumber());
    if (here.isEmpty()) {
        m_diagLabel->clear();
    } else {
        static const char *colors[] = {"#f38ba8", "#f38ba8", "#f9e2af", "#89b4fa", "#94e2d5"};
        const Diagnostic &d = here.first();
        const QString msg = d.message.section(QLatin1Char('\n'), 0, 0);
        m_diagLabel->setText(QStringLiteral("<span style=\"color:%1\">●</span>&nbsp;%2")
                                 .arg(QLatin1String(colors[qBound(0, d.severity, 4)]), msg.toHtmlEscaped()));
        m_diagLabel->setToolTip(d.message);
    }
    m_tabsButton->setText(editor->insertSpaces() ? tr("Soft Tabs: %1").arg(editor->tabWidth())
                                                 : tr("Tab Size: %1").arg(editor->tabWidth()));
    const QString branch = gitBranchFor(editor->filePath());
    m_branchLabel->setText(branch.isEmpty() ? QString()
                                            : QStringLiteral("<span style=\"font-family:'%1'\">%2</span>&nbsp;&nbsp;%3")
                                                  .arg(Icons::symbolFamily(), Icons::glyphText(Icons::GitBranch),
                                                       branch.toHtmlEscaped()));
    const int idx = m_languageCombo->findData(editor->language().id);
    m_languageCombo->setCurrentIndex(idx);
    const QString title = editor->displayName() + (editor->doc()->isModified() ? QStringLiteral(" ●") : QString());
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

void MainWindow::saveAll()
{
    QSet<Document *> done;
    int saved = 0;
    for (CodeEditor *e : allEditors()) {
        Document *doc = e->doc();
        if (done.contains(doc) || !doc->isModified())
            continue;
        done.insert(doc);
        if (!save(e))
            return; // cancelou o "Salvar como" ou falhou: para aqui
        ++saved;
    }
    statusBar()->showMessage(saved ? tr("Saved %n file(s)", nullptr, saved) : tr("Nothing to save"), 2500);
}

void MainWindow::showHelp()
{
    auto *dialog = new HelpDialog(m_theme, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->show();
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
    editor->setInsertSpaces(m_settings.insertSpaces);
    Document *doc = editor->doc();
    LspManager *lsp = m_lsp;
    const bool useLsp = m_settings.lspEnabled;
    editor->setCompletionProvider(
        useLsp ? CodeEditor::CompletionProvider([lsp, doc](int line, int column, std::function<void(QList<CodeEditor::ServerItem>)> done) {
                     lsp->complete(doc, line, column, [done](const QList<LspCompletion> &items) {
                         QList<CodeEditor::ServerItem> out;
                         for (const LspCompletion &i : items)
                             out.append({i.label, i.insertText});
                         done(out);
                     });
                 })
               : CodeEditor::CompletionProvider(),
        lsp->triggerCharacters(doc));
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

    m_lsp->setSettings(m_settings);
    if (m_lspAction) {
        const QSignalBlocker blocker(m_lspAction);
        m_lspAction->setChecked(m_settings.lspEnabled);
    }
    for (const auto &[action, field] : std::as_const(m_settingToggles)) {
        const QSignalBlocker blocker(action);
        action->setChecked(m_settings.*field);
    }
    QSet<Document *> docs;
    for (CodeEditor *ed : allEditors()) {
        configureEditor(ed);
        docs.insert(ed->doc());
    }
    for (Document *doc : std::as_const(docs)) {
        doc->setTheme(m_theme);
        doc->setFoldingEnabled(m_settings.codeFolding);
        // Extensões e linguagens personalizadas podem ter mudado.
        if (!doc->filePath().isEmpty())
            doc->setLanguage(languageForPath(doc->filePath()));
    }
    for (ViewSpace *s : viewSpaces())
        s->setTheme(m_theme);
    rebuildLanguageCombo();

    m_terminal->setTheme(m_theme);
    m_terminal->setTerminalFont(terminalFont());
    m_terminal->configure(m_settings.shell, m_settings.terminalCursorBlink);
    m_terminal->setTransparency(m_settings.terminalTransparency);
    refreshIcons();
    updateStatus();
}

void MainWindow::showSettings()
{
    auto *dialog = new SettingsDialog(m_settings, m_theme, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    connect(dialog, &SettingsDialog::applied, this, [this](const AppSettings &s) {
        if (s.terminalTransparency > 0 && !m_translucent)
            statusBar()->showMessage(tr("Restart Slayer Editor to see the desktop through the terminal."), 6000);
        else if (s.uiScale != m_settings.uiScale || s.uiLanguage != m_settings.uiLanguage)
            statusBar()->showMessage(tr("Interface scale and language apply after restarting Slayer Editor."), 5000);
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
        action->setIcon(Icons::icon(glyph, m_theme.textSecondary, size));
    m_runToolbarAction->setIcon(Icons::icon(Icons::Run, m_theme.window, size));
    for (ViewSpace *s : viewSpaces()) {
        for (int i = 0; i < s->count(); ++i) {
            if (auto *b = qobject_cast<QToolButton *>(s->tabBar()->tabButton(i, QTabBar::RightSide)))
                b->setIcon(Icons::icon(Icons::Close, m_theme.textSecondary, 12));
        }
    }
}

QString MainWindow::styleSheetFor(const Theme &t) const
{
    const QString hover = t.divider.name();
    return QStringLiteral(R"(
QMainWindow { background: %12; }
#Central { background: %1; }
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
QTabBar[activeSpace="true"]::tab:selected { border-top: 2px solid %5; }
QSplitter::handle { background: %4; }
QSplitter::handle:horizontal { width: 1px; }
QSplitter::handle:vertical { height: 1px; }
QSplitter::handle:hover { background: %5; }
#SplitCorner { background: %1; }
QToolButton#SplitButton { padding: 4px; margin: 2px 6px; border-radius: 6px; }
QToolButton#TabClose { padding: 2px; border-radius: 5px; margin-left: 4px; }
QTabWidget#TerminalTabs QTabBar::tab { padding: 5px 12px; margin: 2px 2px 0 2px; }
QPlainTextEdit { background: %2; color: %3; border: none; selection-background-color: %8; }
QStatusBar { background: %1; color: %7; border-top: 1px solid %4; }
QStatusBar QLabel { color: %7; padding: 0 8px; }
QStatusBar QLabel#BranchLabel { color: %3; padding-left: 10px; }
QStatusBar QLabel#DiagLabel { color: %3; }
QToolButton#StatusButton { color: %7; padding: 2px 8px; border-radius: 6px; }
QToolButton#StatusButton:hover { background: %4; color: %3; }
QToolButton#StatusButton::menu-indicator { image: none; width: 0; }
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
QCheckBox::indicator:checked { background: %5; border-color: %5; image: url("%9"); }
QSpinBox::up-button, QSpinBox::down-button { subcontrol-origin: border; width: 18px; border: none;
                                             background: transparent; }
QSpinBox::up-button { subcontrol-position: top right; }
QSpinBox::down-button { subcontrol-position: bottom right; }
QSpinBox::up-arrow { image: url("%10"); width: 10px; height: 10px; }
QSpinBox::down-arrow, QComboBox::down-arrow { image: url("%11"); width: 10px; height: 10px; }
QComboBox::drop-down { border: none; width: 22px; background: transparent; }
)")
        .arg(t.window.name(), t.surface.name(), t.text.name(), hover, t.primary.name(),
             t.primary.lighter(115).name(), t.textSecondary.name(), t.selection.name(),
             Icons::glyphFile(Icons::Check, t.window, 14),
             Icons::glyphFile(Icons::ChevronUp, t.textSecondary, 12),
             Icons::glyphFile(Icons::ChevronDown, t.textSecondary, 12),
             m_translucent ? QStringLiteral("transparent") : t.window.name());
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
    if (current >= 0 && current < m_activeSpace->count())
        m_activeSpace->setCurrentIndex(current);
}

void MainWindow::saveSession()
{
    QSettings s;
    m_settings.save();
    s.setValue(QStringLiteral("ui/lastDir"), m_lastDir);
    s.setValue(QStringLiteral("ui/geometry"), saveGeometry());
    s.setValue(QStringLiteral("ui/state"), saveState());
    QStringList files;
    for (CodeEditor *e : allEditors()) {
        if (!e->filePath().isEmpty() && !files.contains(e->filePath()))
            files << e->filePath();
    }
    s.setValue(QStringLiteral("session/files"), files);
    s.setValue(QStringLiteral("session/current"), m_activeSpace ? m_activeSpace->currentIndex() : 0);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    QSet<Document *> asked;
    for (CodeEditor *e : allEditors()) {
        if (asked.contains(e->doc()))
            continue;
        asked.insert(e->doc());
        if (!maybeSave(e)) {
            event->ignore();
            return;
        }
    }
    saveSession();
    event->accept();
}
