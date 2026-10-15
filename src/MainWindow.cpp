#include "MainWindow.h"
#include "CodeEditor.h"
#include "FindBar.h"
#include "Language.h"
#include "DebugPanel.h"
#include "DebugSession.h"
#include "HelpDialog.h"
#include "LspManager.h"
#include "NewFileDialog.h"
#include "SettingsDialog.h"
#include "TerminalDock.h"
#include "ThemeEditor.h"
#include "ToolbarDialog.h"
#include "ViewSpace.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include "PreviewServer.h"
#include "Sandbox.h"

#include <QDesktopServices>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QKeyEvent>
#include <QMimeData>
#include <QRegularExpression>
#include <QUrl>
#include <QGuiApplication>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QPlainTextEdit>
#include <QTemporaryFile>
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
#include <QStyleHints>
#include <QTabBar>
#include <QTabWidget>
#include <QTextBlock>
#include <QTimer>
#include <QProcess>
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
    setAcceptDrops(true); // arquivos soltos fora dos editores (tela de abertura) abrem em abas
    connect(m_lsp, &LspManager::statusChanged, this, [this]() {
        // Gatilhos de completion chegam com o "initialize" do servidor.
        for (CodeEditor *e : allEditors())
            configureEditor(e);
        updateStatus();
    });
    connect(m_lsp, &LspManager::message, this, [this](const QString &text) { statusBar()->showMessage(text, 6000); });
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
    layout->addWidget(buildDiskBar());
    layout->addWidget(m_stack, 1);
    layout->addWidget(m_findBar);
    setCentralWidget(central);
    setActiveSpace(createViewSpace());
    m_rootSplitter->addWidget(m_activeSpace);

    addDockWidget(Qt::BottomDockWidgetArea, m_terminal);

    m_debug = new DebugSession(m_terminal, this);
    m_debugPanel = new DebugPanel(m_debug, this);
    addDockWidget(Qt::RightDockWidgetArea, m_debugPanel);
    m_debugPanel->hide();
    connect(m_debug, &DebugSession::stateChanged, this, [this](DebugSession::State st) {
        updateDebugActions();
        // Rodando livre (início ou Continuar): o foco vai para o programa, que
        // pode estar esperando entrada (scanf, input). Nos passos, fica no editor.
        if (st == DebugSession::State::Running && m_focusProgramOnRun)
            m_terminal->focusTerminal();
    });
    connect(m_debug, &DebugSession::status, this, [this](const QString &msg) { statusBar()->showMessage(msg, 4000); });
    connect(m_debug, &DebugSession::stopped, this, [this](const QList<DebugFrame> &frames, const QString &) {
        if (!frames.isEmpty())
            showExecutionPoint(frames.first());
        raise();
        activateWindow();
    });
    connect(m_debugPanel, &DebugPanel::frameActivated, this, &MainWindow::showExecutionPoint);
    connect(m_debug, &DebugSession::finished, this, [this](const QString &msg) {
        clearExecutionPoint();
        statusBar()->showMessage(msg, 5000);
    });
    connect(m_debug, &DebugSession::error, this, [this](const QString &msg) {
        clearExecutionPoint();
        QMessageBox::warning(this, tr("Debugger"), msg);
    });

    buildStatusBar();

    connect(m_languageCombo, &QComboBox::activated, this, [this](int index) {
        if (CodeEditor *ed = currentEditor())
            ed->setLanguage(languageById(m_languageCombo->itemData(index).toString()));
    });
    // O desktop trocou entre claro e escuro: reaplica se "seguir o sistema".
    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this, [this]() {
        if (m_settings.themeFollowSystem)
            applySettings(m_settings);
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
    updateDebugActions();
    restoreSession();
    updateEmptyState();
    // Depuração: renderiza a janela (e opcionalmente uma página das configurações) em PNG e sai.
    if (qEnvironmentVariableIsSet("SLAYER_DEBUG_GRAB")) {
        QTimer::singleShot(qEnvironmentVariableIntValue("SLAYER_DEBUG_DELAY") + 900, this, [this]() {
            const QString out = qEnvironmentVariable("SLAYER_DEBUG_GRAB");
            // SLAYER_DEBUG_MENU: abre o menu do topo com esse índice e o captura.
            if (qEnvironmentVariableIsSet("SLAYER_DEBUG_MENU")) {
                QMenu *m = menuBar()->actions().value(qEnvironmentVariableIntValue("SLAYER_DEBUG_MENU"))->menu();
                m->popup(mapToGlobal(QPoint(40, 40)));
                QTimer::singleShot(300, this, [m, out]() {
                    m->grab().save(out);
                    qApp->exit(0);
                });
                return;
            }
            if (qEnvironmentVariableIsSet("SLAYER_DEBUG_NEWFILE")) {
                auto *dialog = new NewFileDialog(m_settings, qEnvironmentVariable("SLAYER_DEBUG_NEWFILE"), m_theme, this);
                dialog->show();
                QTimer::singleShot(300, this, [dialog, out]() {
                    dialog->grab().save(out);
                    qApp->exit(0);
                });
                return;
            }
            if (qEnvironmentVariableIsSet("SLAYER_DEBUG_THEMEEDITOR")) {
                auto *editor = new ThemeEditor({}, m_theme.id, m_theme, this);
                editor->show();
                QTimer::singleShot(300, this, [editor, out]() {
                    editor->grab().save(out);
                    qApp->exit(0);
                });
                return;
            }
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
                // SLAYER_DEBUG_LANGROW: linha selecionada na lista de linguagens.
                if (qEnvironmentVariableIsSet("SLAYER_DEBUG_LANGROW"))
                    for (auto *list : dialog->findChildren<QListWidget *>(QStringLiteral("ProfileList")))
                        if (list->count() > 5)
                            list->setCurrentRow(qEnvironmentVariableIntValue("SLAYER_DEBUG_LANGROW"));
                QTimer::singleShot(300, this, [dialog, out]() {
                    dialog->grab().save(out);
                    qApp->exit(0);
                });
                return;
            }
            // SLAYER_DEBUG_BREAK: ponto de parada na linha dada (base 1) e inicia o depurador.
            if (qEnvironmentVariableIsSet("SLAYER_DEBUG_BREAK") && currentEditor()) {
                currentEditor()->doc()->toggleBreakpoint(qEnvironmentVariableIntValue("SLAYER_DEBUG_BREAK") - 1);
                startDebugging();
            }
            // SLAYER_DEBUG_TERMRIGHT: move o terminal para a direita (layout do usuário).
            if (qEnvironmentVariableIsSet("SLAYER_DEBUG_TERMRIGHT"))
                m_terminal->moveToArea(Qt::RightDockWidgetArea);
            // SLAYER_DEBUG_TOUCH: outro "programa" altera o arquivo atual.
            if (qEnvironmentVariableIsSet("SLAYER_DEBUG_TOUCH") && currentEditor()) {
                QFile f(currentEditor()->filePath());
                if (f.open(QIODevice::Append))
                    f.write(qgetenv("SLAYER_DEBUG_TOUCH") + "\n");
            }
            // SLAYER_DEBUG_TERMTABS: abre abas extras de terminal.
            for (int i = 1; i < qEnvironmentVariableIntValue("SLAYER_DEBUG_TERMTABS"); ++i)
                m_terminal->newTerminal();
            // SLAYER_DEBUG_FOLD: dobra o bloco que começa na linha dada (base 1).
            if (qEnvironmentVariableIsSet("SLAYER_DEBUG_FOLD") && currentEditor())
                currentEditor()->doc()->setFolded(qEnvironmentVariableIntValue("SLAYER_DEBUG_FOLD") - 1, true);
            // SLAYER_DEBUG_CURSOR: posiciona o cursor na posição absoluta dada.
            if (qEnvironmentVariableIsSet("SLAYER_DEBUG_CURSOR") && currentEditor()) {
                QTextCursor c = currentEditor()->textCursor();
                c.setPosition(qEnvironmentVariableIntValue("SLAYER_DEBUG_CURSOR"));
                currentEditor()->setTextCursor(c);
            }
            // SLAYER_DEBUG_PREVIEW: liga o Live Preview (via o botão).
            if (qEnvironmentVariableIsSet("SLAYER_DEBUG_PREVIEW") && m_livePreviewAction
                && m_livePreviewAction->isEnabled())
                m_livePreviewAction->trigger();
            // SLAYER_DEBUG_TYPE: "digita" o texto no editor (dispara a completion).
            if (qEnvironmentVariableIsSet("SLAYER_DEBUG_TYPE") && currentEditor()) {
                currentEditor()->setFocus();
                for (const QChar &ch : qEnvironmentVariable("SLAYER_DEBUG_TYPE")) {
                    int key = 0;
                    QString text(ch);
                    if (ch == QLatin1Char('\t')) { key = Qt::Key_Tab; }
                    else if (ch.isLetter()) key = Qt::Key_A + (ch.toUpper().unicode() - 'A');
                    else if (ch.isDigit()) key = Qt::Key_0 + (ch.unicode() - '0');
                    QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier, text);
                    QApplication::sendEvent(currentEditor(), &press);
                }
            }
            // SLAYER_DEBUG_SPLIT: sequência de divisões, "h" lado a lado e "v" empilhado.
            for (QChar c : qEnvironmentVariable("SLAYER_DEBUG_SPLIT"))
                splitSpace(m_activeSpace, c == QLatin1Char('h') ? Qt::Horizontal : Qt::Vertical, false);
            // SLAYER_DEBUG_RUN: executa o arquivo atual (para screenshots).
            if (qEnvironmentVariableIsSet("SLAYER_DEBUG_RUN"))
                runCurrent();
            // SLAYER_DEBUG_AFTER: espera extra antes da captura (ex.: o depurador parar).
            QTimer::singleShot(300 + qEnvironmentVariableIntValue("SLAYER_DEBUG_AFTER"), this, [this, out]() {
                grab().save(out);
                qApp->exit(0);
            });
        });
    }
}

MainWindow::~MainWindow()
{
    // Os documentos são filhos da janela e morrem depois dos membros dela:
    // nada de sinais deles para cá (m_diskChanged etc. já não existiriam).
    for (Document *doc : findChildren<Document *>(Qt::FindDirectChildrenOnly))
        doc->disconnect(this);
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

    // Ações que podem ir para a barra (Configurar barras de ferramentas).
    auto reg = [this](const QString &id, QAction *action) {
        m_toolbarRegistry.append({id, action});
        return action;
    };

    QMenu *file = menuBar()->addMenu(tr("&File"));
    QAction *newA = add(file, tr("&New…"), QKeySequence::New, [this]() { newFileFromTemplate(); }, Icons::NewFile);
    newA->setToolTip(tr("New file with a Hello, World! template (Ctrl+N)"));
    reg(QStringLiteral("newEmpty"), add(file, tr("New &Empty File"), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_N), [this]() { newTab(); }));
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
    reg(QStringLiteral("closeTab"), add(file, tr("&Close Tab"), QKeySequence(Qt::CTRL | Qt::Key_W), [this]() {
        closeTab(m_activeSpace, m_activeSpace->currentIndex());
    }, Icons::Close));
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
    reg(QStringLiteral("replace"), add(edit, tr("&Replace…"), QKeySequence(Qt::CTRL | Qt::Key_H), [this]() { m_findBar->activate(true); }));
    add(edit, tr("Find &Next"), QKeySequence::FindNext, [this]() { m_findBar->findNext(); });
    add(edit, tr("Find &Previous"), QKeySequence::FindPrevious, [this]() { m_findBar->findPrevious(); });
    edit->addSeparator();
    reg(QStringLiteral("toggleComment"), add(edit, tr("Toggle &Comment"), QKeySequence(Qt::CTRL | Qt::Key_Slash), [this]() {
        if (auto *e = currentEditor()) e->toggleComment();
    }));
    reg(QStringLiteral("goToLine"), add(edit, tr("&Go to Line…"), QKeySequence(Qt::CTRL | Qt::Key_G), [this]() { goToLine(); }));
    edit->addSeparator();
    // Chaves rápidas das opções de Settings → Editor → Features.
    auto toggle = [this](QMenu *menu, const QString &text, bool AppSettings::*field, const QString &on,
                         const QString &off) {
        QAction *a = menu->addAction(text);
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
        return a;
    };
    edit->addSeparator();
    reg(QStringLiteral("settings"), add(edit, tr("&Settings…"), QKeySequence(Qt::CTRL | Qt::Key_Comma),
                             [this]() { showSettings(); }, Icons::Settings));

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
    reg(QStringLiteral("newTerminalTab"), add(view, tr("New Terminal Ta&b"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_QuoteLeft), [this]() {
        m_terminal->show();
        m_terminal->newTerminal();
    }, Icons::Add));
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
    reg(QStringLiteral("splitVertical"), add(split, tr("Split &Vertical"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_L),
        [this]() { splitSpace(m_activeSpace, Qt::Horizontal, false); }, Icons::SplitSideBySide));
    reg(QStringLiteral("splitHorizontal"), add(split, tr("Split &Horizontal"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_T),
        [this]() { splitSpace(m_activeSpace, Qt::Vertical, false); }, Icons::SplitStacked));
    add(split, tr("&Close Current View"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_R),
        [this]() { closeSpace(m_activeSpace); }, Icons::Close);
    // F8 é "Continuar" do depurador (como no Thonny).
    add(split, tr("Next View"), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_Right), [this]() { cycleSpace(1); });
    add(split, tr("Previous View"), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_Left), [this]() { cycleSpace(-1); });
    view->addSeparator();
    // Montado ao abrir: inclui temas criados/editados em Settings → Aparência.
    QMenu *themeMenu = view->addMenu(tr("T&heme"));
    connect(themeMenu, &QMenu::aboutToShow, this, [this, themeMenu]() {
        themeMenu->clear();
        auto *group = new QActionGroup(themeMenu);
        for (const Theme &t : Theme::all()) {
            QAction *a = themeMenu->addAction(t.name);
            a->setCheckable(true);
            a->setChecked(t.id == m_theme.id);
            group->addAction(a);
            const QString id = t.id;
            connect(a, &QAction::triggered, this, [this, id]() { applyTheme(id); });
        }
        themeMenu->addSeparator();
        connect(themeMenu->addAction(tr("Customize Themes…")), &QAction::triggered, this, [this]() {
            showSettings();
            if (auto *dialog = findChild<SettingsDialog *>())
                if (auto *nav = dialog->findChild<QListWidget *>(QStringLiteral("SettingsNav")))
                    nav->setCurrentRow(2);
        });
    });
    view->addSeparator();
    // Dobramento de código, como no Kate.
    QMenu *folding = view->addMenu(tr("Code &Folding"));
    toggle(folding, tr("Show &Folding Markers"), &AppSettings::codeFolding, tr("Code folding enabled"),
           tr("Code folding disabled"));
    folding->addSeparator();
    reg(QStringLiteral("toggleNode"),
        add(folding, tr("&Toggle Current Node"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_BracketLeft), [this]() {
            if (auto *e = currentEditor()) e->doc()->toggleCurrentNode(e->textCursor().blockNumber());
        }));
    add(folding, tr("Toggle &Contained Nodes"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_BracketRight), [this]() {
        if (auto *e = currentEditor()) e->doc()->toggleContainedNodes(e->textCursor().blockNumber());
    });
    folding->addSeparator();
    reg(QStringLiteral("foldTop"),
        add(folding, tr("F&old Toplevel Nodes"), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_BracketLeft), [this]() {
            if (auto *e = currentEditor()) e->doc()->setToplevelFolded(true);
        }));
    reg(QStringLiteral("unfoldTop"),
        add(folding, tr("&Unfold Toplevel Nodes"), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_BracketRight), [this]() {
            if (auto *e = currentEditor()) e->doc()->setToplevelFolded(false);
        }));
    toggle(view, tr("Code Com&pletion"), &AppSettings::codeCompletion, tr("Code completion enabled"),
           tr("Code completion disabled"));
    toggle(view, tr("Auto-Close &Brackets and Quotes"), &AppSettings::autoClosePairs, tr("Auto-closing pairs enabled"),
           tr("Auto-closing pairs disabled"));
    // Como no Kate: arquivo alterado por outro programa é relido sozinho.
    QAction *autoReloadA = toggle(view, tr("Auto &Reload Document"), &AppSettings::autoReload,
                                  tr("Files changed by other programs will reload automatically"),
                                  tr("Auto reload disabled: you will be asked when a file changes on disk"));
    autoReloadA->setToolTip(tr("Reload files changed by other programs (unless they have unsaved changes)"));
    reg(QStringLiteral("reload"), add(view, tr("Re&load Document"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_F5), [this]() {
        if (CodeEditor *e = currentEditor(); e && !e->filePath().isEmpty()) {
            if (e->doc()->isModified()
                && QMessageBox::question(this, tr("Reload Document"),
                                         tr("Discard the unsaved changes in “%1” and reload it from disk?")
                                             .arg(e->displayName()))
                       != QMessageBox::Yes)
                return;
            e->doc()->reload();
            m_diskChanged.remove(e->doc());
            updateDiskBar();
        }
    }));
    view->addSeparator();
    reg(QStringLiteral("zoomIn"), add(view, tr("Zoom &In"), QKeySequence::ZoomIn, [this]() { applyFontSize(m_settings.editorFontSize + 1); }));
    reg(QStringLiteral("zoomOut"), add(view, tr("Zoom &Out"), QKeySequence::ZoomOut, [this]() { applyFontSize(m_settings.editorFontSize - 1); }));
    add(view, tr("&Reset Zoom"), QKeySequence(Qt::CTRL | Qt::Key_0),
        [this]() { applyFontSize(AppSettings::defaults().editorFontSize); });

    QMenu *run = menuBar()->addMenu(tr("&Run"));
    m_runAction = add(run, tr("&Run File"), QKeySequence(Qt::Key_F5), [this]() { runCurrent(); }, Icons::Run);
    m_runAction->setToolTip(tr("Save and run the current file (F5)"));
    m_runInteractiveAction = add(run, tr("Run &Interactive"), QKeySequence(Qt::ALT | Qt::Key_F5),
                                 [this]() { runCurrent(true); }, Icons::RunInteractive);
    m_runInteractiveAction->setToolTip(tr("Run and stay in the interpreter — Python, JavaScript, Shell (Alt+F5); "
                                          "compiled languages run as usual"));
    m_livePreviewAction = add(run, tr("&Live Preview in Browser"), QKeySequence(Qt::SHIFT | Qt::Key_F5),
                              [this](bool on) { setLivePreview(on); }, Icons::LivePreview);
    m_livePreviewAction->setCheckable(true);
    m_livePreviewAction->setEnabled(false);
    m_livePreviewAction->setToolTip(tr("Preview HTML in the browser with live reload (Shift+F5)"));
    connect(m_livePreviewAction, &QAction::toggled, this, [this]() { refreshIcons(); });
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

    QMenu *debug = menuBar()->addMenu(tr("&Debug"));
    m_debugAction = add(debug, tr("&Debug Current File"), QKeySequence(Qt::CTRL | Qt::Key_F5),
                        [this]() { startDebugging(); }, Icons::Bug);
    m_debugAction->setToolTip(tr("Debug the current file (Ctrl+F5) — Python, C, C++"));
    debug->addSeparator();
    QAction *overA = add(debug, tr("Step &Over"), QKeySequence(Qt::Key_F6), [this]() {
        m_focusProgramOnRun = false;
        m_debug->stepOver();
    },
                         Icons::StepOver);
    QAction *intoA = add(debug, tr("Step &Into"), QKeySequence(Qt::Key_F7), [this]() {
        m_focusProgramOnRun = false;
        m_debug->stepInto();
    },
                         Icons::StepInto);
    QAction *outA = add(debug, tr("Step O&ut"), QKeySequence(Qt::SHIFT | Qt::Key_F7), [this]() {
        m_focusProgramOnRun = false;
        m_debug->stepOut();
    },
                        Icons::StepOut);
    QAction *contA = add(debug, tr("&Continue"), QKeySequence(Qt::Key_F8), [this]() {
        m_focusProgramOnRun = true;
        m_debug->resume();
    },
                         Icons::Continue);
    m_stopDebugAction = add(debug, tr("&Stop"), QKeySequence(Qt::SHIFT | Qt::Key_F5), [this]() { m_debug->stop(); },
                            Icons::Stop);
    overA->setToolTip(tr("Step over (F6)"));
    intoA->setToolTip(tr("Step into (F7)"));
    outA->setToolTip(tr("Step out (Shift+F7)"));
    contA->setToolTip(tr("Continue (F8)"));
    m_stopDebugAction->setToolTip(tr("Stop debugging (Shift+F5)"));
    m_stepActions = {overA, intoA, outA, contA};
    debug->addSeparator();
    add(debug, tr("Toggle &Breakpoint"), QKeySequence(Qt::Key_F9), [this]() {
        if (CodeEditor *e = currentEditor())
            e->doc()->toggleBreakpoint(e->textCursor().blockNumber());
    });
    add(debug, tr("&Remove All Breakpoints"), QKeySequence(), [this]() {
        QSet<Document *> docs;
        for (CodeEditor *e : allEditors())
            docs.insert(e->doc());
        for (Document *d : std::as_const(docs))
            d->clearBreakpoints();
    });
    debug->addSeparator();
    QAction *panelA = m_debugPanel->toggleViewAction();
    panelA->setText(tr("Debugger &Panel"));
    debug->addAction(panelA);

    QMenu *help = menuBar()->addMenu(tr("&Help"));
    reg(QStringLiteral("help"), add(help, tr("Slayer Editor &Help"), QKeySequence(Qt::Key_F1), [this]() { showHelp(); }, Icons::Help));

    QMenu *about = menuBar()->addMenu(tr("&About"));
    reg(QStringLiteral("about"), add(about, tr("&About Slayer Editor"), QKeySequence(), [this]() { showAbout(); }, Icons::Info));
    add(about, tr("About &Qt"), QKeySequence(), [this]() { QMessageBox::aboutQt(this); });

    // Ação própria para o botão da barra: ícone escuro sobre o fundo azul; no
    // menu, o ícone de Run usa a cor dos demais.
    m_runToolbarAction = new QAction(tr("Run"), this);
    m_runToolbarAction->setToolTip(m_runAction->toolTip());
    connect(m_runToolbarAction, &QAction::triggered, this, [this]() { runCurrent(); });

    // Na ordem em que aparecem em "Ações disponíveis".
    const QList<QPair<QString, QAction *>> main{
        {"new", newA}, {"open", openA}, {"save", saveA}, {"saveAs", saveAsA}, {"saveAll", saveAllA},
        {"undo", undoA}, {"redo", redoA}, {"find", findA}, {"terminal", termA}, {"run", m_runToolbarAction},
        {"runInteractive", m_runInteractiveAction}, {"livePreview", m_livePreviewAction}, {"debug", m_debugAction}, {"stepOver", m_stepActions[0]},
        {"stepInto", m_stepActions[1]}, {"stepOut", m_stepActions[2]}, {"continue", m_stepActions[3]},
        {"stopDebug", m_stopDebugAction}};
    m_toolbarRegistry = main + m_toolbarRegistry;

    m_toolbar = addToolBar(tr("Main Toolbar"));
    m_toolbar->setObjectName(QStringLiteral("MainToolbar"));
    m_toolbar->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_toolbar, &QToolBar::customContextMenuRequested, this,
            [this](const QPoint &pos) { showToolbarMenu(m_toolbar->mapToGlobal(pos)); });
}

QStringList MainWindow::defaultToolbarActions()
{
    return {"new", "open", "save", "saveAs", "saveAll", "separator", "undo", "redo", "find", "separator",
            "terminal", "run", "runInteractive", "livePreview", "separator", "debug", "stepOver", "stepInto", "stepOut",
            "continue", "stopDebug"};
}

/** Monta a barra conforme as configurações (ações, texto, ícones, trava). */
void MainWindow::rebuildToolbar()
{
    m_toolbar->clear();
    for (const QString &id : m_settings.toolbarActions.isEmpty() ? defaultToolbarActions() : m_settings.toolbarActions) {
        if (id == QLatin1String("separator")) {
            m_toolbar->addSeparator();
            continue;
        }
        for (const auto &[aid, action] : std::as_const(m_toolbarRegistry)) {
            if (aid == id)
                m_toolbar->addAction(action);
        }
    }
    if (auto *runButton = m_toolbar->widgetForAction(m_runToolbarAction))
        runButton->setObjectName(QStringLiteral("RunButton"));
    m_toolbar->setToolButtonStyle(Qt::ToolButtonStyle(m_settings.toolbarStyle));
    const int size = m_settings.toolbarIconSize;
    m_toolbar->setIconSize(QSize(size, size));
    m_toolbar->setMovable(!m_settings.toolbarLocked);
}

/** Botão direito na barra, como no Kate. */
void MainWindow::showToolbarMenu(const QPoint &globalPos)
{
    QMenu menu(this);
    auto change = [this](auto apply) {
        AppSettings s = m_settings;
        apply(s);
        applySettings(s);
    };

    QMenu *text = menu.addMenu(tr("Text Position"));
    auto *textGroup = new QActionGroup(text);
    const QList<QPair<QString, Qt::ToolButtonStyle>> styles{
        {tr("Icons Only"), Qt::ToolButtonIconOnly}, {tr("Text Only"), Qt::ToolButtonTextOnly},
        {tr("Text Alongside Icons"), Qt::ToolButtonTextBesideIcon}, {tr("Text Under Icons"), Qt::ToolButtonTextUnderIcon}};
    for (const auto &[label, style] : styles) {
        QAction *a = text->addAction(label);
        a->setCheckable(true);
        a->setChecked(m_settings.toolbarStyle == int(style));
        textGroup->addAction(a);
        const int value = int(style);
        connect(a, &QAction::triggered, this, [change, value]() { change([value](AppSettings &s) { s.toolbarStyle = value; }); });
    }

    QMenu *size = menu.addMenu(tr("Icon Size"));
    auto *sizeGroup = new QActionGroup(size);
    const QList<QPair<QString, int>> sizes{{tr("Small (16x16)"), 16}, {tr("Medium (22x22)"), 22},
                                           {tr("Large (32x32)"), 32}, {tr("Huge (48x48)"), 48}};
    for (const auto &[label, px] : sizes) {
        QAction *a = size->addAction(label);
        a->setCheckable(true);
        a->setChecked(m_settings.toolbarIconSize == px);
        sizeGroup->addAction(a);
        const int value = px;
        connect(a, &QAction::triggered, this, [change, value]() { change([value](AppSettings &s) { s.toolbarIconSize = value; }); });
    }

    menu.addSeparator();
    QAction *lock = menu.addAction(tr("Lock Toolbar Positions"));
    lock->setCheckable(true);
    lock->setChecked(m_settings.toolbarLocked);
    connect(lock, &QAction::toggled, this, [change](bool on) { change([on](AppSettings &s) { s.toolbarLocked = on; }); });
    menu.addSeparator();
    connect(menu.addAction(Icons::icon(Icons::Settings, m_theme.textSecondary, 16), tr("Configure Toolbars…")),
            &QAction::triggered, this, &MainWindow::configureToolbars);
    menu.exec(globalPos);
}

void MainWindow::configureToolbars()
{
    ToolbarDialog dialog(m_toolbarRegistry,
                         m_settings.toolbarActions.isEmpty() ? defaultToolbarActions() : m_settings.toolbarActions,
                         defaultToolbarActions(), this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    AppSettings s = m_settings;
    s.toolbarActions = dialog.currentIds() == defaultToolbarActions() ? QStringList() : dialog.currentIds();
    applySettings(s);
}

// --------------------------------------------------------------- abas

/** Tela exibida quando todas as abas estão fechadas. */
QWidget *MainWindow::buildEmptyState()
{
    auto *page = new QWidget;
    page->setObjectName(QStringLiteral("EmptyState"));
    page->setAttribute(Qt::WA_StyledBackground);
    page->setAcceptDrops(true); // arrastar arquivos para a tela de abertura os abre
    page->installEventFilter(this);
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
    connect(newButton, &QPushButton::clicked, this, [this]() { newFileFromTemplate(); });
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
    connect(space, &ViewSpace::filesDropped, this, [this, space](const QStringList &paths) {
        setActiveSpace(space);
        openFiles(paths);
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
    doc->setAutoReload(m_settings.autoReload);
    connect(doc, &Document::changedOnDisk, this, [this, doc](bool deleted) {
        m_diskChanged.insert(doc, deleted);
        updateDiskBar();
    });
    connect(doc, &Document::reloaded, this, [this, doc]() {
        if (m_diskChanged.remove(doc))
            updateDiskBar();
        if (m_settings.autoReload)
            statusBar()->showMessage(tr("Reloaded “%1” (changed on disk)").arg(doc->displayName()), 3000);
        m_lsp->documentSaved(doc); // o servidor vê o texto novo
    });
    connect(doc, &QObject::destroyed, this, [this, doc]() {
        m_diskChanged.remove(doc);
        updateDiskBar();
    });
    connect(doc, &Document::changed, this, [this, doc]() {
        updateDocumentTitles(doc);
        m_lsp->attach(doc);
    });
    connect(doc, &Document::breakpointsChanged, this, [this, doc]() {
        if (m_debug->isActive() && !doc->filePath().isEmpty()) {
            QList<int> lines = doc->breakpoints().values();
            std::sort(lines.begin(), lines.end());
            m_debug->setBreakpoints(doc->filePath(), lines);
        }
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
    // Arrastar arquivos para o editor abre-os em novas abas (no espaço do editor).
    connect(editor, &CodeEditor::filesDropped, this, [this, space](const QStringList &paths) {
        setActiveSpace(space);
        openFiles(paths);
    });
    // Auto-save: salva o arquivo editado automaticamente (debounce), se ligado.
    connect(editor, &QPlainTextEdit::textChanged, this, [this, editor]() {
        if (!m_settings.autoSave || editor->filePath().isEmpty())
            return;
        m_autoSavePending.insert(editor);
        if (!m_autoSaveTimer) {
            m_autoSaveTimer = new QTimer(this);
            m_autoSaveTimer->setSingleShot(true);
            m_autoSaveTimer->setInterval(1500);
            connect(m_autoSaveTimer, &QTimer::timeout, this, &MainWindow::autoSavePending);
        }
        m_autoSaveTimer->start();
    });
    // Live-reload do preview ao editar (modo "a cada mudança"), com antirrebote.
    connect(editor, &QPlainTextEdit::textChanged, this, [this]() {
        if (!m_preview || !m_preview->isRunning() || m_settings.previewReload != 1)
            return;
        if (!m_previewReloadTimer) {
            m_previewReloadTimer = new QTimer(this);
            m_previewReloadTimer->setSingleShot(true);
            m_previewReloadTimer->setInterval(250);
            connect(m_previewReloadTimer, &QTimer::timeout, this, [this]() {
                if (m_preview)
                    m_preview->triggerReload();
            });
        }
        m_previewReloadTimer->start();
    });
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

/** Arquivo novo com o Hello, World! da linguagem escolhida (Ctrl+N). */
void MainWindow::newFileFromTemplate()
{
    QSettings qs;
    NewFileDialog dialog(m_settings, qs.value(QStringLiteral("ui/lastNewLanguage"), QStringLiteral("c")).toString(),
                         m_theme, this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    const QString id = dialog.languageId();
    qs.setValue(QStringLiteral("ui/lastNewLanguage"), id);

    CodeEditor *editor = newTab();
    Document *doc = editor->doc();
    const QString name = dialog.fileName();
    // A extensão digitada manda; sem ela, vale a linguagem escolhida.
    Language lang = !name.isEmpty() && !QFileInfo(name).suffix().isEmpty() ? languageForPath(name) : languageById(id);
    if (lang.id == Language::plainText().id && !id.isEmpty())
        lang = languageById(id);
    doc->setLanguage(lang);
    doc->setSuggestedName(name);
    doc->text()->setPlainText(dialog.content());
    doc->markClean(); // fechar sem editar não pergunta nada
    // Cursor no fim do corpo do programa, pronto para editar.
    QTextCursor c(doc->text());
    c.movePosition(QTextCursor::End);
    editor->setTextCursor(c);
    editor->setFocus();
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
    QString start = editor->filePath();
    if (start.isEmpty())
        start = editor->doc()->suggestedName().isEmpty() ? m_lastDir
                                                          : QDir(m_lastDir).filePath(editor->doc()->suggestedName());
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
    // Servidor de linguagem: clicar abre o menu (ligar/desligar, configurações).
    m_lspLabel = new QToolButton(this);
    m_lspLabel->setObjectName(QStringLiteral("StatusButton"));
    m_lspLabel->setPopupMode(QToolButton::InstantPopup);
    m_lspLabel->setAutoRaise(true);
    m_lspLabel->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_lspLabel->setIconSize(QSize(14, 14));
    m_lspLabel->setToolTip(tr("Language server — click to turn on/off"));
    auto *lspMenu = new QMenu(m_lspLabel);
    m_lspLabel->setMenu(lspMenu);
    connect(lspMenu, &QMenu::aboutToShow, this, [this, lspMenu]() {
        lspMenu->clear();
        lspMenu->addAction(m_lspAction); // a mesma ação do menu Run: sempre sincronizada
        lspMenu->addSeparator();
        connect(lspMenu->addAction(tr("Language Server Settings…")), &QAction::triggered, this, [this]() {
            showSettings();
            if (auto *dialog = findChild<SettingsDialog *>())
                if (auto *nav = dialog->findChild<QListWidget *>(QStringLiteral("SettingsNav")))
                    nav->setCurrentRow(4); // Linguagens (com os servidores de linguagem)
        });
    });

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

/**
 * Faixa "modificado por outro programa" (como no Kate), para o arquivo
 * atual: Recarregar, Ver diferença, Sobrescrever (com alterações locais) e
 * Ignorar.
 */
QWidget *MainWindow::buildDiskBar()
{
    m_diskBar = new QWidget(this);
    m_diskBar->setObjectName(QStringLiteral("DiskBar"));
    m_diskBar->setAttribute(Qt::WA_StyledBackground);
    auto *row = new QHBoxLayout(m_diskBar);
    row->setContentsMargins(12, 6, 8, 6);
    m_diskIcon = new QLabel(m_diskBar);
    m_diskLabel = new QLabel(m_diskBar);
    m_diskLabel->setWordWrap(true);
    row->addWidget(m_diskIcon);
    row->addWidget(m_diskLabel, 1);
    auto makeButton = [this, row](const QString &text) {
        auto *b = new QPushButton(text, m_diskBar);
        b->setAutoDefault(false);
        row->addWidget(b);
        return b;
    };
    m_diskAutoReload = makeButton(tr("Enable Auto Reload"));
    m_diskReload = makeButton(tr("Reload"));
    m_diskDiff = makeButton(tr("View Difference"));
    m_diskOverwrite = makeButton(tr("Overwrite"));
    m_diskIgnore = makeButton(tr("Ignore"));
    m_diskOverwrite->setToolTip(tr("Keep your version and save it over the file on disk"));
    m_diskAutoReload->setToolTip(tr("From now on, reload files changed by other programs automatically "
                                    "(unless they have unsaved changes)"));

    auto current = [this]() -> Document * {
        CodeEditor *e = currentEditor();
        return e && m_diskChanged.contains(e->doc()) ? e->doc() : nullptr;
    };
    connect(m_diskAutoReload, &QPushButton::clicked, this, [this, current]() {
        Document *doc = current();
        AppSettings s = m_settings;
        s.autoReload = true;
        applySettings(s); // salva e aplica setAutoReload a todos os documentos
        // Aplica agora ao arquivo atual, como o recarregamento automático faria.
        if (doc && !doc->isModified()) {
            QString error;
            if (!doc->reload(&error) && !error.isEmpty())
                QMessageBox::warning(this, tr("Reload Document"), error);
            m_diskChanged.remove(doc);
        }
        updateDiskBar();
    });
    connect(m_diskReload, &QPushButton::clicked, this, [this, current]() {
        if (Document *doc = current()) {
            QString error;
            if (!doc->reload(&error))
                QMessageBox::warning(this, tr("Reload Document"), error);
            m_diskChanged.remove(doc);
            updateDiskBar();
        }
    });
    connect(m_diskDiff, &QPushButton::clicked, this, [this, current]() {
        if (Document *doc = current())
            showDiskDifference(doc);
    });
    connect(m_diskOverwrite, &QPushButton::clicked, this, [this, current]() {
        if (Document *doc = current()) {
            if (CodeEditor *e = currentEditor(); e && save(e)) {
                m_diskChanged.remove(doc);
                updateDiskBar();
            }
        }
    });
    connect(m_diskIgnore, &QPushButton::clicked, this, [this, current]() {
        if (Document *doc = current()) {
            doc->acknowledgeDiskChange();
            m_diskChanged.remove(doc);
            updateDiskBar();
        }
    });
    m_diskBar->hide();
    return m_diskBar;
}

void MainWindow::updateDiskBar()
{
    if (!m_diskBar)
        return;
    CodeEditor *e = currentEditor();
    if (!e || !m_diskChanged.contains(e->doc())) {
        m_diskBar->hide();
        return;
    }
    Document *doc = e->doc();
    const bool deleted = m_diskChanged.value(doc);
    m_diskIcon->setPixmap(Icons::icon(Icons::Info, QColor("#f9e2af"), 16).pixmap(16, 16));
    m_diskLabel->setText(deleted ? tr("“%1” was deleted by another program.").arg(doc->displayName())
                         : doc->isModified()
                             ? tr("“%1” was modified by another program, and you have unsaved changes. "
                                  "Reload it (losing your changes) or overwrite the file with your version?")
                                   .arg(doc->displayName())
                             : tr("“%1” was modified by another program. Show the new version?").arg(doc->displayName()));
    m_diskAutoReload->setVisible(!deleted && !m_settings.autoReload);
    m_diskReload->setVisible(!deleted);
    m_diskDiff->setVisible(!deleted);
    m_diskOverwrite->setVisible(deleted || doc->isModified());
    m_diskOverwrite->setText(deleted ? tr("Save Again") : tr("Overwrite"));
    m_diskBar->show();
}

/** Diferença entre a versão do disco e a do editor (diff -u). */
void MainWindow::showDiskDifference(Document *doc)
{
    QTemporaryFile mine(QDir::tempPath() + QStringLiteral("/slayer-XXXXXX"));
    if (!mine.open())
        return;
    mine.write(doc->text()->toPlainText().toUtf8());
    mine.flush();
    QProcess diff;
    diff.start(QStringLiteral("diff"), {"-u", "--label", tr("in the editor"), "--label", tr("on disk"),
                                        mine.fileName(), doc->filePath()});
    if (!diff.waitForFinished(10000)) {
        QMessageBox::warning(this, tr("View Difference"), tr("Could not run “diff”."));
        return;
    }
    const QString text = QString::fromUtf8(diff.readAllStandardOutput());

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Changes in “%1”").arg(doc->displayName()));
    dialog.resize(900, 600);
    auto *view = new QPlainTextEdit(&dialog);
    view->setReadOnly(true);
    view->setLineWrapMode(QPlainTextEdit::NoWrap);
    view->setFont(editorFont());
    // Linhas +/- coloridas como num diff.
    QString html;
    for (const QString &line : text.split(QLatin1Char('\n'))) {
        QString color = m_theme.text.name();
        if (line.startsWith(QLatin1String("+")) && !line.startsWith(QLatin1String("+++")))
            color = QStringLiteral("#a6e3a1");
        else if (line.startsWith(QLatin1String("-")) && !line.startsWith(QLatin1String("---")))
            color = QStringLiteral("#f38ba8");
        else if (line.startsWith(QLatin1String("@@")))
            color = m_theme.primary.name();
        html += QStringLiteral("<span style=\"color:%1; white-space:pre\">%2</span><br>").arg(color, line.toHtmlEscaped());
    }
    view->document()->setHtml(QStringLiteral("<div style=\"font-family:'%1'\">%2</div>")
                                  .arg(editorFont().family(), text.isEmpty() ? tr("No differences.") : html));
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    auto *layout = new QVBoxLayout(&dialog);
    layout->addWidget(view);
    layout->addWidget(buttons);
    dialog.exec();
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
    updateDiskBar();
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
    // Visível para linguagens com servidor conhecido: mostra o servidor ativo,
    // ou "LSP off" para dar como religar dali mesmo.
    const QString server = m_lsp->statusFor(editor->doc());
    const bool lspLanguage = LspManager::languages().contains(editor->language().id);
    m_lspLabel->setVisible(!server.isEmpty() || (lspLanguage && !m_settings.lspEnabled));
    m_lspLabel->setText(server.isEmpty() ? tr("LSP off") : server);
    m_lspLabel->setIcon(Icons::icon(Icons::Server, server.isEmpty() ? m_theme.textSecondary : m_theme.primary, 14));
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
    int idx = m_languageCombo->findData(editor->language().id);
    if (idx < 0) {
        // Arquivo de uma linguagem não instalada: aparece só enquanto for o atual.
        const Language &lang = editor->language();
        m_languageCombo->addItem(Icons::icon(Icons::forLanguage(lang.id), Icons::colorForLanguage(lang.id), 14),
                                 tr("%1 (not installed)").arg(lang.displayName), lang.id);
        idx = m_languageCombo->count() - 1;
    }
    m_languageCombo->setCurrentIndex(idx);
    if (m_livePreviewAction)
        m_livePreviewAction->setEnabled(editor->language().id == QLatin1String("html"));
    const QString title = editor->displayName() + (editor->doc()->isModified() ? QStringLiteral(" ●") : QString());
    setWindowTitle(title + QStringLiteral(" — Slayer Editor"));
}

// ----------------------------------------------------------- executar

void MainWindow::runCurrent(bool interactive)
{
    CodeEditor *editor = currentEditor();
    if (!editor)
        return;
    // Sem título: salvar primeiro define o nome e, com ele, a linguagem.
    if (!save(editor))
        return;
    // HTML não "roda": a pré-visualização é pelo botão Live Preview.
    if (editor->language().id == QLatin1String("html")) {
        statusBar()->showMessage(tr("Use the Live Preview button to open HTML in the browser."), 4000);
        return;
    }
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
    m_terminal->runCommand(lang.commandFor(info.absoluteFilePath(), tool, flags, interactive), info.absolutePath());
}

void MainWindow::previewCurrent(CodeEditor *editor)
{
    const QFileInfo info(editor->filePath());
    QString root = QDir(info.absolutePath()).absolutePath();
    // Detecta a raiz do site: se o HTML usa caminhos absolutos ("/dist/…"), sobe
    // até o ancestral que mais contém esses caminhos de topo, para que CSS/JS
    // absolutos resolvam.
    {
        static const QRegularExpression re(QStringLiteral("(?:href|src)\\s*=\\s*[\"']/([^/\"'?#]+)"));
        QSet<QString> tops;
        auto it = re.globalMatch(editor->toPlainText());
        while (it.hasNext())
            tops.insert(it.next().captured(1));
        if (!tops.isEmpty()) {
            QDir d(root);
            QString best = root;
            int bestCount = 0;
            for (int up = 0; up <= 8; ++up) {
                int count = 0;
                for (const QString &seg : std::as_const(tops))
                    if (d.exists(seg))
                        ++count;
                if (count > bestCount) {
                    bestCount = count;
                    best = d.absolutePath();
                }
                if (!d.cdUp())
                    break;
            }
            if (bestCount > 0)
                root = best;
        }
    }
    const QString relPath = QString::fromUtf8(QUrl::toPercentEncoding(QDir(root).relativeFilePath(info.absoluteFilePath()), "/"));
    const QString host = m_settings.previewHost.isEmpty() ? QStringLiteral("127.0.0.1") : m_settings.previewHost;
    const int port = m_settings.previewPort;
    QUrl url;
    bool delay = false;

    if (m_settings.previewCommand.trimmed().isEmpty()) {
        // Servidor embutido (com live-reload).
        if (!m_preview)
            m_preview = new PreviewServer(this);
        if (!m_preview->isRunning() || m_preview->rootDir() != root || m_preview->host() != host
            || m_preview->port() != port) {
            if (!m_preview->start(root, host, port) && !m_preview->start(root, host, 0)) {
                statusBar()->showMessage(tr("Could not start the preview server."), 5000);
                return;
            }
        }
        m_preview->setContentProvider([this](const QString &abs, QByteArray &out) -> bool {
            for (CodeEditor *e : allEditors()) {
                if (!e->filePath().isEmpty() && e->doc()->isModified()
                    && QFileInfo(e->filePath()).absoluteFilePath() == abs) {
                    out = e->toPlainText().toUtf8();
                    return true;
                }
            }
            return false;
        });
        const PreviewServer::ReloadMode mode = m_settings.previewReload == 0 ? PreviewServer::Never
                                             : m_settings.previewReload == 2 ? PreviewServer::Interval
                                                                             : PreviewServer::OnChange;
        m_preview->setReload(mode, m_settings.previewReloadInterval * 1000);
        url = m_preview->urlForFile(info.absoluteFilePath());
    } else {
        // Servidor externo (ex.: "php -S {host}:{port}"); sem live-reload injetado.
        QString cmd = m_settings.previewCommand;
        cmd.replace(QStringLiteral("{host}"), host);
        cmd.replace(QStringLiteral("{port}"), QString::number(port));
        const QStringList parts = QProcess::splitCommand(cmd);
        if (parts.isEmpty())
            return;
        if (!Sandbox::hasExecutable(parts.first())) {
            statusBar()->showMessage(tr("“%1” is not installed — set the preview tool in Settings.").arg(parts.first()), 6000);
            return;
        }
        if (!m_previewProcess) {
            m_previewProcess = new QProcess(this);
            m_previewProcess->setProcessChannelMode(QProcess::ForwardedChannels);
        }
        if (m_previewProcess->state() == QProcess::NotRunning || m_previewProcess->workingDirectory() != root) {
            m_previewProcess->kill();
            m_previewProcess->waitForFinished(500);
            m_previewProcess->setWorkingDirectory(root);
            const auto [program, args] = Sandbox::command(parts.first(), parts.mid(1));
            m_previewProcess->start(program, args);
            delay = true; // dá um tempo para o servidor subir antes de abrir o navegador
        }
        // Se o comando traz o próprio endereço (sem {host}/{port}), usa-o na URL.
        QString urlHost = host;
        int urlPort = port;
        if (!m_settings.previewCommand.contains(QLatin1String("{host}"))
            && !m_settings.previewCommand.contains(QLatin1String("{port}"))) {
            static const QRegularExpression addr(QStringLiteral("(\\[[^\\]]+\\]|[A-Za-z0-9_.-]+)?:(\\d{2,5})(?!\\d)"));
            const auto mm = addr.match(m_settings.previewCommand);
            if (mm.hasMatch()) {
                if (!mm.captured(1).isEmpty())
                    urlHost = mm.captured(1);
                urlPort = mm.captured(2).toInt();
            }
        }
        if (urlHost.isEmpty() || urlHost == QLatin1String("0.0.0.0") || urlHost == QLatin1String("[::]"))
            urlHost = QStringLiteral("127.0.0.1");
        url = QUrl(QStringLiteral("http://%1:%2/%3").arg(urlHost).arg(urlPort).arg(relPath));
    }

    if (url.isEmpty()) {
        statusBar()->showMessage(tr("Could not build the preview URL."), 5000);
        return;
    }
    auto openInBrowser = [this, url]() {
        if (m_settings.previewBrowser.trimmed().isEmpty()) {
            QDesktopServices::openUrl(url);
        } else {
            const QStringList b = QProcess::splitCommand(m_settings.previewBrowser);
            QStringList args = b.mid(1);
            args << url.toString();
            const auto [program, a] = Sandbox::command(b.first(), args);
            QProcess::startDetached(program, a);
        }
    };
    if (delay)
        QTimer::singleShot(500, this, openInBrowser);
    else
        openInBrowser();
    statusBar()->showMessage(tr("Previewing %1 at %2").arg(info.fileName(), url.toString()), 4000);
}

void MainWindow::setLivePreview(bool on)
{
    if (on) {
        CodeEditor *editor = currentEditor();
        if (!editor || editor->language().id != QLatin1String("html") || !save(editor)) {
            if (m_livePreviewAction)
                m_livePreviewAction->setChecked(false);
            return;
        }
        previewCurrent(editor);
    } else {
        if (m_preview)
            m_preview->stop();
        if (m_previewProcess && m_previewProcess->state() != QProcess::NotRunning) {
            m_previewProcess->kill();
            m_previewProcess->waitForFinished(500);
        }
        statusBar()->showMessage(tr("Live preview stopped."), 3000);
    }
}

void MainWindow::autoSavePending()
{
    const QSet<CodeEditor *> pending = m_autoSavePending;
    m_autoSavePending.clear();
    const QList<CodeEditor *> alive = allEditors();
    for (CodeEditor *e : pending) {
        if (alive.contains(e) && !e->filePath().isEmpty() && e->doc()->isModified())
            save(e);
    }
}

QHash<QString, QList<int>> MainWindow::collectBreakpoints() const
{
    QHash<QString, QList<int>> out;
    for (CodeEditor *e : allEditors()) {
        Document *d = e->doc();
        if (d->filePath().isEmpty() || d->breakpoints().isEmpty() || out.contains(d->filePath()))
            continue;
        QList<int> lines = d->breakpoints().values();
        std::sort(lines.begin(), lines.end());
        out.insert(d->filePath(), lines);
    }
    return out;
}

void MainWindow::startDebugging()
{
    CodeEditor *editor = currentEditor();
    if (!editor)
        return;
    if (m_debug->isActive()) {
        // Já depurando: Ctrl+F5 continua, como no Thonny.
        m_debug->resume();
        return;
    }
    if (!save(editor))
        return;
    const QString id = editor->language().id;
    if (!DebugSession::supports(id)) {
        statusBar()->showMessage(tr("The debugger supports Python, C and C++."), 5000);
        return;
    }
    const QHash<QString, QList<int>> bps = collectBreakpoints();
    if (bps.isEmpty())
        statusBar()->showMessage(tr("No breakpoints: click left of a line number (or F9) to add one."), 6000);
    m_debugPanel->show();
    m_focusProgramOnRun = true;
    m_debug->start(editor->filePath(), id, m_settings, bps);
}

void MainWindow::showExecutionPoint(const DebugFrame &frame)
{
    if (frame.path.isEmpty() || !QFileInfo::exists(frame.path))
        return;
    openFile(frame.path);
    CodeEditor *editor = currentEditor();
    if (!editor || editor->filePath() != QFileInfo(frame.path).absoluteFilePath())
        return;
    if (m_execDoc && m_execDoc != editor->doc())
        m_execDoc->setExecutionLine(-1);
    m_execDoc = editor->doc();
    m_execDoc->setExecutionLine(frame.line);
    editor->setFocus();
}

void MainWindow::clearExecutionPoint()
{
    if (m_execDoc)
        m_execDoc->setExecutionLine(-1);
    m_execDoc = nullptr;
}

void MainWindow::updateDebugActions()
{
    const DebugSession::State st = m_debug->state();
    for (QAction *a : std::as_const(m_stepActions))
        a->setEnabled(st == DebugSession::State::Stopped);
    m_stopDebugAction->setEnabled(st != DebugSession::State::Idle);
    if (st == DebugSession::State::Running)
        clearExecutionPoint();
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

/**
 * Fecha (pedindo para salvar o que for preciso e gravando a sessão) e abre de
 * novo: os arquivos, o layout e as divisões voltam como estavam.
 */
void MainWindow::restartApplication()
{
    if (!close())
        return; // o usuário cancelou em "alterações não salvas"
    QProcess::startDetached(QCoreApplication::applicationFilePath(), {});
    qApp->quit();
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
                              "© Wyllian Bezerra da Silva"));
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
    return Icons::editorFont(m_settings.editorFont.isEmpty() ? Icons::monoFamily() : m_settings.editorFont,
                             m_settings.editorFontSize);
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
    QList<QPair<QString, QString>> symbolRules;
    for (const SymbolRule &r : m_settings.effectiveSymbols())
        symbolRules.append({r.sequence, r.symbol});
    editor->setPrettySymbols(m_settings.prettySymbols, symbolRules);
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
    QList<Theme> userThemes;
    for (const QJsonValue &v : std::as_const(m_settings.userThemes))
        userThemes << Theme::fromJson(v.toObject());
    Theme::setUserThemes(userThemes);
    m_theme = Theme::byId(m_settings.theme);
    // Seguir o sistema: variante clara/escura conforme o desktop.
    if (m_settings.themeFollowSystem) {
        const Qt::ColorScheme scheme = QGuiApplication::styleHints()->colorScheme();
        if (scheme != Qt::ColorScheme::Unknown)
            m_theme = Theme::variant(m_settings.theme, scheme == Qt::ColorScheme::Dark);
    }

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
    rebuildToolbar();

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
        doc->setAutoReload(m_settings.autoReload);
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
        const bool restart = s.uiScale != m_settings.uiScale || s.uiLanguage != m_settings.uiLanguage;
        applySettings(s);
        // Idioma e escala valem desde a criação da janela: reabre o app na hora.
        if (restart)
            QTimer::singleShot(0, this, &MainWindow::restartApplication);
    });
    dialog->open();
}

void MainWindow::rebuildLanguageCombo()
{
    // Linguagens instaladas no sistema (o realce das outras continua valendo
    // para arquivos abertos; updateStatus acrescenta a do arquivo atual).
    m_languageCombo->clear();
    for (const Language &lang : Language::all()) {
        if (m_settings.languageInstalled(lang.id))
            m_languageCombo->addItem(Icons::icon(Icons::forLanguage(lang.id), Icons::colorForLanguage(lang.id), 14),
                                     lang.displayName, lang.id);
    }
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
    for (const auto &[action, glyph] : std::as_const(m_iconActions)) {
        // Live Preview ligado: ícone na cor de destaque (indica ativo).
        const QColor color = (action == m_livePreviewAction && action->isChecked()) ? m_theme.primary
                                                                                     : m_theme.textSecondary;
        action->setIcon(Icons::icon(glyph, color, size));
    }
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
QMainWindow { background: %1; }
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
/* Opções marcáveis com caixa visível (como no Kate): vazia = dá para marcar. */
QMenu::indicator { width: 14px; height: 14px; margin-left: 8px; border: 1px solid %7; border-radius: 4px;
                   background: %2; }
QMenu::indicator:checked { background: %5; border-color: %5; image: url("%9"); }
QMenu::indicator:exclusive { border-radius: 8px; }
QMenu::indicator:selected { border-color: %3; }
QMenu::indicator:checked:selected { border-color: %5; }
QTabWidget::pane { border: none; }
QTabBar { background: %1; }
QTabBar::tab { background: transparent; color: %7; padding: 8px 14px; margin: 4px 2px 0 2px;
               border: 1px solid transparent; border-bottom: none;
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
QTabWidget#TerminalTabs QTabBar { background: %2; }
QTabWidget#TerminalTabs QTabBar::tab { padding: 6px 12px; margin: 4px 1px 0 1px; color: %7; background: transparent;
    border: 1px solid transparent; border-bottom: none;
    border-top-left-radius: 8px; border-top-right-radius: 8px; }
QTabWidget#TerminalTabs QTabBar::tab:selected { background: %12; color: %3; font-weight: 600;
    border: 1px solid %4; border-bottom: none; }
QTabWidget#TerminalTabs QTabBar::tab:hover:!selected { background: %4; color: %3; }
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
#DiskBar { background: %1; border-bottom: 2px solid #f9e2af; }
#DiskBar QLabel { color: %3; }
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
QPushButton#SegmentLeft, QPushButton#SegmentRight { padding: 6px 16px; min-width: 70px; border-radius: 0; }
QPushButton#SegmentLeft { border-top-left-radius: 8px; border-bottom-left-radius: 8px; }
QPushButton#SegmentRight { border-top-right-radius: 8px; border-bottom-right-radius: 8px; border-left: none; }
QPushButton#SegmentLeft:checked, QPushButton#SegmentRight:checked { background: %5; color: %1; border-color: %5;
    font-weight: 600; }
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
QTreeWidget#DebugVariables, QListWidget#DebugStack { background: %2; color: %3; border: 1px solid %4;
    border-radius: 8px; alternate-background-color: %1; outline: 0; }
QTreeWidget#DebugVariables::item, QListWidget#DebugStack::item { padding: 3px 4px; }
QTreeWidget#DebugVariables::item:selected, QListWidget#DebugStack::item:selected { background: %4; color: %3; }
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
             Icons::glyphFile(Icons::ChevronDown, t.textSecondary, 12), t.terminalBackground.name());
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

static QStringList droppedLocalFiles(const QMimeData *mime)
{
    QStringList files;
    if (mime && mime->hasUrls()) {
        for (const QUrl &url : mime->urls()) {
            if (url.isLocalFile())
                files << url.toLocalFile();
        }
    }
    return files;
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    if (!droppedLocalFiles(event->mimeData()).isEmpty())
        event->acceptProposedAction();
    else
        QMainWindow::dragEnterEvent(event);
}

void MainWindow::dropEvent(QDropEvent *event)
{
    const QStringList files = droppedLocalFiles(event->mimeData());
    if (!files.isEmpty()) {
        event->acceptProposedAction();
        openFiles(files);
        return;
    }
    QMainWindow::dropEvent(event);
}

// A tela de abertura (buildEmptyState) instala este filtro para também abrir
// arquivos soltos sobre ela.
bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    switch (event->type()) {
    case QEvent::DragEnter:
    case QEvent::DragMove:
    case QEvent::Drop: {
        auto *de = static_cast<QDropEvent *>(event);
        const QStringList files = droppedLocalFiles(de->mimeData());
        if (!files.isEmpty()) {
            de->acceptProposedAction();
            if (event->type() == QEvent::Drop)
                openFiles(files);
            return true;
        }
        break;
    }
    default:
        break;
    }
    return QMainWindow::eventFilter(watched, event);
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
    // Encerra o preview (servidor embutido e servidor externo).
    if (m_preview)
        m_preview->stop();
    if (m_previewProcess && m_previewProcess->state() != QProcess::NotRunning) {
        m_previewProcess->kill();
        m_previewProcess->waitForFinished(500);
    }
    saveSession();
    event->accept();
}
