#include "TerminalDock.h"
#include <QAction>
#include <QActionGroup>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QPalette>
#include <QScrollBar>
#include <QHBoxLayout>
#include <QLabel>
#include <QMainWindow>
#include <QKeyEvent>
#include <QMenu>
#include <QLineEdit>
#include <QUrl>
#include <QInputDialog>
#include <QFileDialog>
#include <QDesktopServices>
#include <QMouseEvent>
#include <QPointer>
#include <QTimer>
#include <QWindow>
#include <QTabBar>
#include <QTabWidget>
#include <QStandardPaths>
#include <QToolButton>
#include <QVBoxLayout>

#include <qtermwidget.h>

#include <memory>
#include <sys/syscall.h>
#include <unistd.h>

#include "Icons.h"

TerminalDock::TerminalDock(QMainWindow *mainWindow)
    : QDockWidget(tr("Terminal"), mainWindow)
    , m_mainWindow(mainWindow)
    , m_container(new QWidget(this))
{
    m_container->setObjectName(QStringLiteral("TerminalContainer"));
    m_container->setAttribute(Qt::WA_StyledBackground);
    setObjectName(QStringLiteral("TerminalDock"));
    setAllowedAreas(Qt::AllDockWidgetAreas);
    setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable
                | QDockWidget::DockWidgetClosable);
    auto *layout = new QVBoxLayout(m_container);
    layout->setContentsMargins(6, 0, 6, 6);
    m_tabs = new QTabWidget(m_container);
    // Aba vista: tira a marca ● de atividade/silêncio.
    connect(m_tabs, &QTabWidget::currentChanged, this, [this](int i) {
        const QString text = m_tabs->tabText(i);
        if (text.startsWith(QStringLiteral("● ")))
            m_tabs->setTabText(i, text.mid(2));
    });
    m_tabs->setObjectName(QStringLiteral("TerminalTabs"));
    m_tabs->setDocumentMode(true);
    m_tabs->setMovable(true);
    m_tabs->tabBar()->setExpanding(false);
    m_tabs->tabBar()->setDrawBase(false);
    m_tabs->tabBar()->setElideMode(Qt::ElideMiddle);
    // Botão do meio fecha a aba (como no Konsole); duplo clique no vazio abre uma.
    m_tabs->tabBar()->installEventFilter(this);
    layout->addWidget(m_tabs);
    setWidget(m_container);
    setTitleBarWidget(buildTitleBar());
    registerColorSchemes();

    // Transparência só no painel destacado: a janela dele nasce translúcida
    // (atributo inócuo enquanto acoplado). A janela principal fica opaca —
    // translúcida inteira, repinturas parciais piscavam.
    setAttribute(Qt::WA_TranslucentBackground);

    // Destacado, o painel vira janela sem moldura (barra de título própria):
    // bordas e cantos são tratados aqui e repassados ao compositor.
    for (QWidget *w : {static_cast<QWidget *>(m_container), titleBarWidget()}) {
        w->setMouseTracking(true);
        w->installEventFilter(this);
    }
    connect(this, &QDockWidget::topLevelChanged, this, [this](bool floating) {
        m_container->layout()->setContentsMargins(floating ? kGrip : 6, 0, floating ? kGrip : 6,
                                                  floating ? kGrip : 6);
        if (!floating)
            m_container->unsetCursor();
        applyOpacity();
    });
}

namespace {

QString schemeDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + QStringLiteral("/color-schemes");
}
} // namespace

/**
 * O QTermWidget lê a lista de diretórios de esquemas uma única vez (ao criar
 * o primeiro terminal) e guarda em cache cada esquema já carregado, inclusive
 * todos os arquivos presentes na primeira consulta. Por isso o diretório é
 * registrado cedo e vazio, e cada conjunto de cores novo (tema criado ou
 * editado) vai para um arquivo de nome novo, lido do disco quando usado.
 */
void TerminalDock::registerColorSchemes()
{
    static bool done = false;
    if (done)
        return;
    done = true;
    // Limpa só os esquemas desta instância que sobraram de antes (PID reciclado).
    const QString dir = schemeDir();
    for (const QString &f : QDir(dir).entryList({QStringLiteral("SlayerEditor-%1-*").arg(QCoreApplication::applicationPid())}))
        QFile::remove(QDir(dir).filePath(f));
    QDir().mkpath(dir);
    QTermWidget::addCustomColorSchemeDir(dir);
    // Esquemas de instâncias encerradas: apaga os que não são de processos vivos.
    for (const QString &f : QDir(dir).entryList({QStringLiteral("SlayerEditor-*.colorscheme")})) {
        const QString pid = f.section(QLatin1Char('-'), 1, 1);
        if (!QFileInfo::exists(QStringLiteral("/proc/") + pid))
            QFile::remove(QDir(dir).filePath(f));
    }
}

QString TerminalDock::areaKey(Qt::DockWidgetArea area)
{
    switch (area) {
    case Qt::LeftDockWidgetArea: return QStringLiteral("left");
    case Qt::RightDockWidgetArea: return QStringLiteral("right");
    case Qt::TopDockWidgetArea: return QStringLiteral("top");
    default: return QStringLiteral("bottom");
    }
}

Qt::DockWidgetArea TerminalDock::areaFromKey(const QString &key)
{
    if (key == QLatin1String("left")) return Qt::LeftDockWidgetArea;
    if (key == QLatin1String("right")) return Qt::RightDockWidgetArea;
    if (key == QLatin1String("top")) return Qt::TopDockWidgetArea;
    return Qt::BottomDockWidgetArea;
}

QWidget *TerminalDock::buildTitleBar()
{
    auto *bar = new QWidget(this);
    bar->setObjectName(QStringLiteral("DockTitleBar"));
    auto *layout = new QHBoxLayout(bar);
    layout->setContentsMargins(10, 4, 4, 4);
    layout->setSpacing(2);

    m_titleIcon = new QLabel(bar);
    layout->addWidget(m_titleIcon);
    auto *title = new QLabel(tr("TERMINAL"), bar);
    title->setObjectName(QStringLiteral("DockTitle"));
    layout->addWidget(title);
    layout->addStretch(1);

    auto makeButton = [this, bar, layout](Icons::Glyph glyph, const QString &tip) {
        auto *b = new QToolButton(bar);
        b->setAutoRaise(true);
        b->setIconSize(QSize(16, 16));
        b->setToolTip(tip);
        layout->addWidget(b);
        m_buttons.append({b, glyph});
        return b;
    };

    connect(makeButton(Icons::Add, tr("New terminal tab (Ctrl+Shift+`)")), &QToolButton::clicked, this,
            [this]() { newTerminal(); });

    // Menu de posição: esquerda, topo, direita, base.
    auto *posButton = makeButton(Icons::Layout, tr("Panel position"));
    posButton->setPopupMode(QToolButton::InstantPopup);
    auto *menu = new QMenu(posButton);
    auto *group = new QActionGroup(menu);
    const QList<QPair<QString, Qt::DockWidgetArea>> areas{
        {tr("Left"), Qt::LeftDockWidgetArea},
        {tr("Top"), Qt::TopDockWidgetArea},
        {tr("Right"), Qt::RightDockWidgetArea},
        {tr("Bottom"), Qt::BottomDockWidgetArea}};
    for (const auto &[label, area] : areas) {
        QAction *a = menu->addAction(label);
        a->setCheckable(true);
        group->addAction(a);
        const Qt::DockWidgetArea target = area;
        connect(a, &QAction::triggered, this, [this, target]() { moveToArea(target); });
    }
    connect(menu, &QMenu::aboutToShow, this, [this, group]() {
        const QString current = areaKey(m_mainWindow->dockWidgetArea(this));
        const QStringList keys{"left", "top", "right", "bottom"};
        const auto actions = group->actions();
        for (int i = 0; i < actions.size(); ++i)
            actions[i]->setChecked(!isFloating() && keys[i] == current);
    });
    posButton->setMenu(menu);

    connect(makeButton(Icons::Clear, tr("Clear")),
            &QToolButton::clicked, this, [this]() {
                if (QTermWidget *t = current())
                    t->clear();
            });
    connect(makeButton(Icons::Float, tr("Detach or re-dock")),
            &QToolButton::clicked, this, [this]() { setFloating(!isFloating()); });
    connect(makeButton(Icons::Close, tr("Hide panel")),
            &QToolButton::clicked, this, &QWidget::hide);
    return bar;
}

QString TerminalDock::effectiveShell() const
{
    if (!m_shell.trimmed().isEmpty())
        return m_shell.trimmed();
    return qEnvironmentVariable("SHELL", QStringLiteral("/bin/bash"));
}

QTermWidget *TerminalDock::current() const
{
    return qobject_cast<QTermWidget *>(m_tabs->currentWidget());
}

QList<QTermWidget *> TerminalDock::terminals() const
{
    QList<QTermWidget *> list;
    for (int i = 0; i < m_tabs->count(); ++i) {
        if (auto *t = qobject_cast<QTermWidget *>(m_tabs->widget(i)))
            list << t;
    }
    return list;
}

void TerminalDock::configure(const QString &shell, bool cursorBlink)
{
    const bool shellChanged = (shell != m_shell);
    m_shell = shell;
    m_cursorBlink = cursorBlink;
    for (QTermWidget *t : terminals())
        t->setBlinkingCursor(cursorBlink);
    if (m_tabs->count() == 0)
        newTerminal();
    else if (shellChanged)
        restartShell();
}

/** Troca o shell de todas as abas (Settings → Terminal → Shell mudou). */
void TerminalDock::restartShell()
{
    const int count = qMax(1, m_tabs->count());
    const QList<QTermWidget *> old = terminals();
    for (QTermWidget *t : old) {
        t->disconnect(this);
        m_tabs->removeTab(m_tabs->indexOf(t));
        t->deleteLater();
    }
    for (int i = 0; i < count; ++i)
        newTerminal();
}

QTermWidget *TerminalDock::createTerminal()
{
    auto *term = new QTermWidget(0, m_tabs);
    term->setShellProgram(effectiveShell());
    QStringList env = QProcessEnvironment::systemEnvironment().toStringList();
    env << QStringLiteral("TERM=xterm-256color");
    term->setEnvironment(env);
    // Nova aba abre na pasta da aba atual, quando se sabe qual é.
    QString dir = m_workingDir.isEmpty() ? QDir::homePath() : m_workingDir;
    if (QTermWidget *cur = current()) {
        const QString cwd = cur->workingDirectory();
        if (!cwd.isEmpty() && QFileInfo(cwd).isDir())
            dir = cwd;
    }
    term->setWorkingDirectory(dir);
    term->setTerminalSizeHint(false);
    term->setBlinkingCursor(m_cursorBlink);
    applyAppearance(term);
    if (m_font != QFont())
        term->setTerminalFont(m_font);
    term->setTerminalOpacity(opacity());
    applyScrollbarStyle(term);
    installContextMenu(term);
    return term;
}

/**
 * Menu de contexto (botão direito) e atalhos do terminal: Copiar, Colar,
 * Colar seleção e Limpar. A seleção com o mouse e a cópia da seleção (botão do
 * meio) já são do QTermWidget. Ctrl+Shift+C/V porque Ctrl+C é sinal de
 * interrupção no shell.
 */
void TerminalDock::installContextMenu(QTermWidget *term)
{
    auto *copy = new QAction(tr("Copy"), term);
    copy->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_C));
    copy->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    copy->setEnabled(false);
    connect(copy, &QAction::triggered, term, &QTermWidget::copyClipboard);
    connect(term, &QTermWidget::copyAvailable, copy, &QAction::setEnabled);
    auto *paste = new QAction(tr("Paste"), term);
    paste->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_V));
    paste->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    connect(paste, &QAction::triggered, term, &QTermWidget::pasteClipboard);
    term->addAction(copy);
    term->addAction(paste);

    // O clique direito chega ao filho TerminalDisplay; vale para ele também.
    const auto hook = [this, term](QWidget *w) {
        w->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(w, &QWidget::customContextMenuRequested, this,
                [this, term, w](const QPoint &p) { showTerminalMenu(term, w->mapToGlobal(p)); });
    };
    hook(term);
    for (QWidget *w : term->findChildren<QWidget *>())
        hook(w);
    // Ctrl+Shift+F no terminal busca nele (como no Konsole); fora dele é "Buscar no projeto".
    term->installEventFilter(this);
    for (QWidget *w : term->findChildren<QWidget *>())
        w->installEventFilter(this);
}

void TerminalDock::showTerminalMenu(QTermWidget *term, const QPoint &globalPos)
{
    QMenu menu(this);
    // Sobre um link: abrir / copiar endereço (ações do próprio QTermWidget).
    const QList<QAction *> linkActions = term->filterActions(term->mapFromGlobal(globalPos));
    if (!linkActions.isEmpty()) {
        menu.addActions(linkActions);
        menu.addSeparator();
    }
    QAction *copy = menu.addAction(tr("Copy"), term, &QTermWidget::copyClipboard);
    copy->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_C));
    copy->setEnabled(!term->selectedText().isEmpty());
    menu.addAction(tr("Paste"), term, &QTermWidget::pasteClipboard)
        ->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_V));
    menu.addAction(tr("Paste Selection"), term, &QTermWidget::pasteSelection);
    menu.addAction(tr("Select All"), term, [term]() {
        term->setSelectionStart(-term->historyLinesCount(), 0);
        term->setSelectionEnd(term->screenLinesCount(), term->screenColumnsCount());
    });
    menu.addSeparator();
    menu.addAction(tr("Find…"), term, &QTermWidget::toggleShowSearchBar)
        ->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_F));
    menu.addAction(tr("Clear Terminal and History"), term, &QTermWidget::clear);
    menu.addAction(tr("Save Output As…"), this, [this, term]() {
        const QString path = QFileDialog::getSaveFileName(this, tr("Save Output As"),
                                                          QDir(term->workingDirectory()).filePath(QStringLiteral("terminal.txt")));
        if (path.isEmpty())
            return;
        QFile f(path);
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
            term->saveHistory(&f);
    });
    menu.addSeparator();
    menu.addAction(tr("Zoom In"), term, &QTermWidget::zoomIn);
    menu.addAction(tr("Zoom Out"), term, &QTermWidget::zoomOut);
    menu.addAction(tr("Reset Zoom"), this, [this, term]() { term->setTerminalFont(m_font != QFont() ? m_font : term->font()); });
    menu.addSeparator();
    menu.addAction(tr("Open File Manager Here"), this, [term]() {
        QDesktopServices::openUrl(QUrl::fromLocalFile(term->workingDirectory()));
    });
    menu.addAction(tr("Rename Tab…"), this, [this, term]() {
        const int i = m_tabs->indexOf(term);
        bool ok = false;
        const QString name = QInputDialog::getText(this, tr("Rename Tab"), tr("Name:"), QLineEdit::Normal,
                                                   m_tabs->tabText(i), &ok).trimmed();
        if (!ok || i < 0)
            return;
        term->setProperty("customTitle", !name.isEmpty()); // vazio volta ao título automático
        if (!name.isEmpty())
            m_tabs->setTabText(i, name);
    });
    QAction *activity = menu.addAction(tr("Monitor for Activity"));
    activity->setCheckable(true);
    activity->setChecked(term->property("monitorActivity").toBool());
    connect(activity, &QAction::toggled, term, [term](bool on) {
        term->setProperty("monitorActivity", on);
        term->setMonitorActivity(on);
    });
    QAction *silence = menu.addAction(tr("Monitor for Silence (10 s)"));
    silence->setCheckable(true);
    silence->setChecked(term->property("monitorSilence").toBool());
    connect(silence, &QAction::toggled, term, [term](bool on) {
        term->setProperty("monitorSilence", on);
        term->setSilenceTimeout(10);
        term->setMonitorSilence(on);
    });

    // Configurar: esquema de cores, cursor, histórico e barra de rolagem (salvos nas configurações).
    menu.addSeparator();
    QMenu *configure = menu.addMenu(tr("Configure"));
    const auto request = [this](const QString &scheme, int cursor, int history, int bar) {
        Q_EMIT appearanceChangeRequested(scheme, cursor, history, bar);
    };
    QMenu *schemes = configure->addMenu(tr("Color Scheme"));
    auto *schemeGroup = new QActionGroup(schemes);
    QAction *followTheme = schemes->addAction(tr("Follow the Theme"));
    followTheme->setCheckable(true);
    followTheme->setChecked(m_userScheme.isEmpty());
    schemeGroup->addAction(followTheme);
    connect(followTheme, &QAction::triggered, this,
            [=, this]() { request(QString(), m_cursorShape, m_historyLines, m_scrollbar); });
    schemes->addSeparator();
    for (const QString &name : colorSchemes()) {
        QAction *a = schemes->addAction(name);
        a->setCheckable(true);
        a->setChecked(name == m_userScheme);
        schemeGroup->addAction(a);
        connect(a, &QAction::triggered, this, [=, this]() { request(name, m_cursorShape, m_historyLines, m_scrollbar); });
    }
    QMenu *cursor = configure->addMenu(tr("Cursor Shape"));
    auto *cursorGroup = new QActionGroup(cursor);
    const QStringList cursorNames{tr("Block"), tr("Underline"), tr("I-Beam")};
    for (int i = 0; i < cursorNames.size(); ++i) {
        QAction *a = cursor->addAction(cursorNames.at(i));
        a->setCheckable(true);
        a->setChecked(i == m_cursorShape);
        cursorGroup->addAction(a);
        connect(a, &QAction::triggered, this, [=, this]() { request(m_userScheme, i, m_historyLines, m_scrollbar); });
    }
    QMenu *history = configure->addMenu(tr("History"));
    auto *historyGroup = new QActionGroup(history);
    for (int lines : {1000, 10000, 50000, 100000, -1}) {
        QAction *a = history->addAction(lines < 0 ? tr("Unlimited") : tr("%L1 lines").arg(lines));
        a->setCheckable(true);
        a->setChecked(lines == m_historyLines);
        historyGroup->addAction(a);
        connect(a, &QAction::triggered, this, [=, this]() { request(m_userScheme, m_cursorShape, lines, m_scrollbar); });
    }
    QMenu *bar = configure->addMenu(tr("Scrollbar"));
    auto *barGroup = new QActionGroup(bar);
    const QList<QPair<QString, int>> bars{{tr("Right"), 2}, {tr("Left"), 1}, {tr("Hidden"), 0}};
    for (const auto &[label, value] : bars) {
        QAction *a = bar->addAction(label);
        a->setCheckable(true);
        a->setChecked(value == m_scrollbar);
        barGroup->addAction(a);
        connect(a, &QAction::triggered, this, [=, this]() { request(m_userScheme, m_cursorShape, m_historyLines, value); });
    }
    configure->addSeparator();
    configure->addAction(tr("Terminal Settings…"), this, [this]() { Q_EMIT settingsRequested(); });
    menu.exec(globalPos);
}

QTermWidget *TerminalDock::newTerminal()
{
    QTermWidget *term = createTerminal();
    const int index = m_tabs->addTab(term, tr("Terminal %1").arg(++m_counter));
    auto *close = new QToolButton(m_tabs->tabBar());
    close->setObjectName(QStringLiteral("TabClose"));
    close->setAutoRaise(true);
    close->setIconSize(QSize(12, 12));
    close->setToolTip(tr("Close terminal tab"));
    connect(close, &QToolButton::clicked, this, [this, term]() { closeTerminal(m_tabs->indexOf(term)); });
    m_tabs->tabBar()->setTabButton(index, QTabBar::RightSide, close);
    m_tabs->setCurrentIndex(index);
    connect(term, &QTermWidget::titleChanged, this, [this, term]() {
        const int i = m_tabs->indexOf(term);
        const QString title = term->title().trimmed();
        if (i >= 0 && !title.isEmpty() && !term->property("customTitle").toBool()) {
            m_tabs->setTabText(i, title.length() > 32 ? QStringLiteral("…") + title.right(31) : title);
            m_tabs->setTabToolTip(i, title);
        }
    });
    connect(term, &QTermWidget::activity, this, [this, term]() { markTabActivity(term, tr("Activity")); });
    connect(term, &QTermWidget::silence, this, [this, term]() { markTabActivity(term, tr("Silence")); });
    // "exit": numa aba extra, fecha a aba; na última, abre um shell novo.
    connect(term, &QTermWidget::finished, this, [this, term]() {
        const int i = m_tabs->indexOf(term);
        if (i < 0)
            return;
        if (m_tabs->count() > 1) {
            m_tabs->removeTab(i);
            term->deleteLater();
        } else {
            m_tabs->removeTab(i);
            term->deleteLater();
            newTerminal();
        }
        updateTabBar();
    });
    term->startShellProgram();
    updateTabBar();
    term->setFocus();
    return term;
}

/**
 * Terminal para o programa depurado: a aba de shell atual. O shell fica em
 * espera ("sleep infinity" em primeiro plano, sem ler o teclado) e o gdb
 * liga o programa a este PTY (inferior-tty) — entrada e saída na mesma aba
 * usada pelo Run. releaseDebugTty() devolve o prompt no fim da sessão.
 */
QString TerminalDock::debugTty()
{
    QTermWidget *term = shellTerminal();
    show();
    raise();
    const int fd = term->getPtySlaveFd();
    const char *name = fd >= 0 ? ::ttyname(fd) : nullptr;
    if (!name)
        return QString();
    m_debugTerm = term;
    sendWhenIdle(term, QStringLiteral("clear; sleep infinity\r"), 0);
    return QString::fromLocal8Bit(name);
}

void TerminalDock::releaseDebugTty()
{
    QTermWidget *term = m_debugTerm;
    m_debugTerm = nullptr;
    if (!term)
        return;
    // Só interrompe o nosso "sleep" (o usuário pode já ter voltado ao prompt).
    const int fg = term->getForegroundProcessId();
    QFile comm(QStringLiteral("/proc/%1/comm").arg(fg));
    if (fg > 0 && fg != term->getShellPID() && comm.open(QIODevice::ReadOnly)
        && comm.readAll().trimmed() == "sleep")
        term->sendText(QStringLiteral("\x03"));
}

void TerminalDock::closeTerminal(int index)
{
    auto *term = qobject_cast<QTermWidget *>(m_tabs->widget(index));
    if (!term)
        return;
    term->disconnect(this);
    m_tabs->removeTab(index);
    term->deleteLater(); // encerra o shell (SIGHUP)
    if (m_tabs->count() == 0)
        newTerminal();
    updateTabBar();
    focusTerminal();
}

void TerminalDock::updateTabBar()
{
    // Com uma aba só não há ✕: fechá-la apenas recriaria outra igual.
    const bool closable = m_tabs->count() > 1;
    for (int i = 0; i < m_tabs->count(); ++i) {
        if (QWidget *b = m_tabs->tabBar()->tabButton(i, QTabBar::RightSide))
            b->setVisible(closable);
    }
    refreshTabIcons();
}

void TerminalDock::refreshTabIcons()
{
    for (int i = 0; i < m_tabs->count(); ++i) {
        if (auto *b = qobject_cast<QToolButton *>(m_tabs->tabBar()->tabButton(i, QTabBar::RightSide)))
            b->setIcon(Icons::icon(Icons::Close, m_iconColor, 12));
    }
}

/** Bordas (e cantos) sob o ponto, em coordenadas do painel. */
Qt::Edges TerminalDock::edgesAt(const QPoint &pos) const
{
    Qt::Edges edges;
    if (!isFloating())
        return edges;
    const int grip = kGrip + 2;
    if (pos.x() <= grip) edges |= Qt::LeftEdge;
    if (pos.x() >= width() - grip) edges |= Qt::RightEdge;
    if (pos.y() <= 4) edges |= Qt::TopEdge;
    if (pos.y() >= height() - grip) edges |= Qt::BottomEdge;
    return edges;
}

static Qt::CursorShape cursorFor(Qt::Edges e)
{
    if (e == (Qt::LeftEdge | Qt::TopEdge) || e == (Qt::RightEdge | Qt::BottomEdge))
        return Qt::SizeFDiagCursor;
    if (e == (Qt::RightEdge | Qt::TopEdge) || e == (Qt::LeftEdge | Qt::BottomEdge))
        return Qt::SizeBDiagCursor;
    if (e & (Qt::LeftEdge | Qt::RightEdge))
        return Qt::SizeHorCursor;
    return Qt::SizeVerCursor;
}

bool TerminalDock::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::ShortcutOverride || event->type() == QEvent::KeyPress) {
        const auto *ke = static_cast<QKeyEvent *>(event);
        // Ctrl+F no terminal abre a barra de busca; o atalho global não dispara.
        if (ke->key() == Qt::Key_F && ke->modifiers() == (Qt::ControlModifier | Qt::ShiftModifier)) {
            for (QObject *o = watched; o; o = o->parent()) {
                if (auto *term = qobject_cast<QTermWidget *>(o)) {
                    if (event->type() == QEvent::KeyPress)
                        term->toggleShowSearchBar();
                    event->accept();
                    return true;
                }
            }
        }
        // Ctrl+C sem Shift dentro do terminal → SIGINT; impede que o atalho
        // "Copy" do menu Edit consuma a tecla antes de ela chegar ao QTermWidget.
        if (ke->key() == Qt::Key_C && ke->modifiers() == Qt::ControlModifier) {
            for (QObject *o = watched; o; o = o->parent()) {
                if (qobject_cast<QTermWidget *>(o)) {
                    event->accept(); // ShortcutOverride: bloqueia o atalho global
                    return false;    // KeyPress: deixa o QTermWidget processar normalmente
                }
            }
        }
    }
    if ((watched == m_container || watched == titleBarWidget()) && isFloating()) {
        auto *w = static_cast<QWidget *>(watched);
        if (event->type() == QEvent::MouseMove || event->type() == QEvent::MouseButtonPress) {
            const auto *me = static_cast<QMouseEvent *>(event);
            const Qt::Edges edges = edgesAt(w->mapTo(this, me->position().toPoint()));
            if (event->type() == QEvent::MouseMove && me->buttons() == Qt::NoButton) {
                if (edges)
                    w->setCursor(cursorFor(edges));
                else
                    w->unsetCursor();
            } else if (event->type() == QEvent::MouseButtonPress && me->button() == Qt::LeftButton && edges) {
                if (QWindow *win = window()->windowHandle(); win && win->startSystemResize(edges))
                    return true;
            }
        } else if (event->type() == QEvent::Leave) {
            w->unsetCursor();
        }
    }
    if (watched == m_tabs->tabBar() && event->type() == QEvent::MouseButtonRelease) {
        const auto *me = static_cast<QMouseEvent *>(event);
        const int i = m_tabs->tabBar()->tabAt(me->position().toPoint());
        if (me->button() == Qt::MiddleButton && i >= 0 && m_tabs->count() > 1) {
            closeTerminal(i);
            return true;
        }
    }
    if (watched == m_tabs->tabBar() && event->type() == QEvent::MouseButtonDblClick) {
        const auto *me = static_cast<QMouseEvent *>(event);
        if (m_tabs->tabBar()->tabAt(me->position().toPoint()) < 0) {
            newTerminal();
            return true;
        }
    }
    return QDockWidget::eventFilter(watched, event);
}

void TerminalDock::setWorkingDirectory(const QString &dir)
{
    m_workingDir = dir;
}

/** Aba com shell para rodar comandos: a atual, outra com shell, ou uma nova. */
QTermWidget *TerminalDock::shellTerminal()
{
    QTermWidget *term = current();
    if (term && term->getShellPID())
        return term;
    for (int i = 0; i < m_tabs->count(); ++i) {
        auto *t = qobject_cast<QTermWidget *>(m_tabs->widget(i));
        if (t && t->getShellPID()) {
            m_tabs->setCurrentIndex(i);
            return t;
        }
    }
    return newTerminal();
}

void TerminalDock::runCommand(const QString &command, const QString &workingDir, RunFocus focus)
{
    shellTerminal();
    show();
    raise();
    QString dir = workingDir;
    dir.replace(QLatin1Char('\''), QLatin1String("'\\''"));
    const QString line = QStringLiteral("cd '") + dir + QStringLiteral("' && clear && ") + command + QLatin1Char('\r');
    QTermWidget *term = current();
    sendWhenIdle(term, line, 0);
    if (m_inputWatch)
        m_inputWatch->stop();
    if (focus == RunFocus::Terminal)
        focusTerminal();
    else if (focus == RunFocus::OnInput)
        watchForInput(term);
}

/**
 * Algum processo da árvore de `pid` está parado esperando o teclado? Olha a
 * chamada de sistema em curso de cada thread (/proc/<pid>/task/<tid>/syscall):
 * read() no fd 0, ou a espera do readline (select/poll), cujo fd não dá para
 * ver — por isso só conta se o fd 0 do processo for um terminal.
 */
static bool waitingForInput(int pid, int depth = 0)
{
#ifdef Q_OS_LINUX
    if (pid <= 0 || depth > 4)
        return false;
    const QString base = QStringLiteral("/proc/%1/task").arg(pid);
    const QStringList tids = QDir(base).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    const bool stdinIsTty = QFileInfo(QStringLiteral("/proc/%1/fd/0").arg(pid)).symLinkTarget()
                                .startsWith(QLatin1String("/dev/pts/"));
    for (const QString &tid : tids) {
        QFile f(base + QLatin1Char('/') + tid + QStringLiteral("/syscall"));
        if (f.open(QIODevice::ReadOnly)) {
            const QList<QByteArray> parts = f.readAll().simplified().split(' ');
            bool ok = false;
            const long nr = parts.value(0).toLong(&ok);
            if (ok && stdinIsTty) {
                if (nr == SYS_read && parts.value(1).toLongLong(nullptr, 16) == 0)
                    return true;
                if (nr == SYS_pselect6 || nr == SYS_ppoll
#ifdef SYS_select
                    || nr == SYS_select
#endif
#ifdef SYS_poll
                    || nr == SYS_poll
#endif
                )
                    return true;
            }
        }
        // Filhos (ex.: "make run" → ./programa).
        QFile children(base + QLatin1Char('/') + tid + QStringLiteral("/children"));
        if (children.open(QIODevice::ReadOnly)) {
            for (const QByteArray &c : children.readAll().simplified().split(' '))
                if (!c.isEmpty() && waitingForInput(c.toInt(), depth + 1))
                    return true;
        }
    }
#else
    Q_UNUSED(pid)
    Q_UNUSED(depth)
#endif
    return false;
}

/** Enquanto o programa roda, passa o foco ao terminal quando ele pedir entrada. */
void TerminalDock::watchForInput(QTermWidget *term)
{
    if (!m_inputWatch) {
        m_inputWatch = new QTimer(this);
        m_inputWatch->setInterval(150);
    }
    m_inputWatch->disconnect();
    QPointer<QTermWidget> guard(term);
    auto started = std::make_shared<bool>(false);
    auto ticks = std::make_shared<int>(0);
    connect(m_inputWatch, &QTimer::timeout, this, [this, guard, started, ticks]() {
        if (!guard) {
            m_inputWatch->stop();
            return;
        }
        const int shell = guard->getShellPID();
        const int fg = guard->getForegroundProcessId();
        const bool running = fg > 0 && fg != shell;
        ++*ticks;
        if (!running) {
            // Terminou (ou nem começou em ~5 s: compilação que falhou etc.).
            if (*started || *ticks > 33)
                m_inputWatch->stop();
            return;
        }
        *started = true;
        if (waitingForInput(fg)) {
            m_inputWatch->stop();
            guard->setFocus();
        }
    });
    m_inputWatch->start();
}

/**
 * Se algo roda no terminal (um REPL do "Executar interativo", um programa
 * esperando entrada…), encerra antes de mandar o comando: Ctrl+C e, se o
 * processo ainda estiver lá (REPLs ignoram Ctrl+C), Ctrl+D. Ctrl+D só é
 * enviado a um processo que não é o shell, que fecharia o terminal.
 */
void TerminalDock::sendWhenIdle(QTermWidget *term, const QString &line, int attempt)
{
    if (!term)
        return;
    const int shell = term->getShellPID();
    const int fg = term->getForegroundProcessId();
    // O terminal pode informar um grupo de primeiro plano que já não existe
    // (ex.: o lançador do debugpy): só conta como ocupado se o processo vive.
    const bool busy = fg > 0 && fg != shell && QFileInfo::exists(QStringLiteral("/proc/%1").arg(fg));
    if (!busy || attempt >= 6) {
        // Ctrl+U limpa o que houver digitado no prompt antes do comando.
        term->sendText(QStringLiteral("\x15") + line);
        return;
    }
    term->sendText(attempt % 2 == 0 ? QStringLiteral("\x03") : QStringLiteral("\x04"));
    QPointer<QTermWidget> guard(term);
    QTimer::singleShot(150, this, [this, guard, line, attempt]() { sendWhenIdle(guard, line, attempt + 1); });
}

void TerminalDock::setTheme(const Theme &theme)
{
    for (const auto &[button, glyph] : std::as_const(m_buttons))
        button->setIcon(Icons::icon(glyph, theme.textSecondary, 16));
    m_titleIcon->setPixmap(Icons::icon(Icons::Terminal, theme.primary, 16).pixmap(16, 16));
    m_iconColor = theme.textSecondary;
    refreshTabIcons();
    // Arquivo novo só quando as cores mudam; o nome inclui o PID para não
    // colidir com outra instância do app.
    const QString schemeKey = theme.id + theme.terminalBackground.name() + theme.text.name()
                              + theme.ansi[0].name() + theme.ansi[7].name() + theme.ansi[15].name();
    if (schemeKey != m_schemeKey) {
        m_schemeKey = schemeKey;
        const QString name = QStringLiteral("SlayerEditor-%1-%2").arg(QCoreApplication::applicationPid()).arg(m_nextSlot++);
        theme.writeTerminalScheme(schemeDir(), name);
        // Caminho completo: o QTermWidget lê o arquivo, sem depender da lista
        // de esquemas que ele montou na primeira consulta.
        m_schemeName = schemeDir() + QLatin1Char('/') + name + QStringLiteral(".colorscheme");
    }
    m_background = theme.terminalBackground;
    // O scrollbar do QTermWidget não segue o esquema de cores: fica escuro nos
    // temas claros. Estiliza-se com as cores do terminal.
    const QColor track = theme.terminalBackground;
    QColor handle = theme.dark ? track.lighter(170) : track.darker(135);
    QColor handleHover = theme.dark ? track.lighter(210) : track.darker(160);
    m_scrollbarStyle = QStringLiteral(
        "QScrollBar:vertical { background: %1; width: 12px; margin: 0; }"
        "QScrollBar::handle:vertical { background: %2; border-radius: 4px; min-height: 28px; margin: 2px; }"
        "QScrollBar::handle:vertical:hover { background: %3; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
        "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: %1; }")
        .arg(track.name(), handle.name(), handleHover.name());
    updateContainerBackground();
    for (QTermWidget *t : terminals()) {
        if (m_userScheme.isEmpty())
            t->setColorScheme(m_schemeName);
        applyScrollbarStyle(t);
    }
}

QStringList TerminalDock::colorSchemes()
{
    QStringList list;
    for (const QString &s : QTermWidget::availableColorSchemes())
        if (!s.startsWith(QLatin1String("SlayerEditor-"))) // os do tema são internos
            list << s;
    list.sort(Qt::CaseInsensitive);
    return list;
}

void TerminalDock::applyAppearance(QTermWidget *term) const
{
    if (!effectiveScheme().isEmpty())
        term->setColorScheme(effectiveScheme());
    static const QTermWidget::KeyboardCursorShape shapes[] = {
        QTermWidget::KeyboardCursorShape::BlockCursor, QTermWidget::KeyboardCursorShape::UnderlineCursor,
        QTermWidget::KeyboardCursorShape::IBeamCursor};
    term->setKeyboardCursorShape(shapes[qBound(0, m_cursorShape, 2)]);
    term->setHistorySize(m_historyLines);
    static const QTermWidget::ScrollBarPosition bars[] = {QTermWidget::NoScrollBar, QTermWidget::ScrollBarLeft,
                                                         QTermWidget::ScrollBarRight};
    term->setScrollBarPosition(bars[qBound(0, m_scrollbar, 2)]);
}

void TerminalDock::setAppearance(const QString &colorScheme, int cursorShape, int historyLines, int scrollbar)
{
    m_userScheme = colorScheme;
    m_cursorShape = cursorShape;
    m_historyLines = historyLines;
    m_scrollbar = scrollbar;
    for (QTermWidget *t : terminals())
        applyAppearance(t);
}

/** Aba em segundo plano com atividade (ou silêncio): marca com ● até ser vista. */
void TerminalDock::markTabActivity(QTermWidget *term, const QString &reason)
{
    const int i = m_tabs->indexOf(term);
    if (i < 0 || i == m_tabs->currentIndex())
        return;
    const QString text = m_tabs->tabText(i);
    if (!text.startsWith(QStringLiteral("● ")))
        m_tabs->setTabText(i, QStringLiteral("● ") + text);
    m_tabs->setTabToolTip(i, reason);
}

/** Cores do tema no scrollbar e na "sobra" de célula à direita/abaixo do texto. */
void TerminalDock::applyScrollbarStyle(QTermWidget *term) const
{
    if (!term)
        return;
    if (!m_scrollbarStyle.isEmpty()) {
        const auto bars = term->findChildren<QScrollBar *>();
        for (QScrollBar *bar : bars)
            bar->setStyleSheet(m_scrollbarStyle);
    }
    if (!m_background.isValid())
        return;
    // A grade de caracteres raramente preenche o widget por inteiro: a faixa
    // que sobra à direita e embaixo é pintada com a cor de fundo do widget (a
    // paleta), não a do esquema. No tema escuro isso aparecia como uma borda
    // clara. Iguala-se a paleta ao fundo do terminal (o scrollbar, com folha de
    // estilo própria, não é afetado).
    QPalette pal = term->palette();
    pal.setColor(QPalette::Window, m_background);
    pal.setColor(QPalette::Base, m_background);
    term->setPalette(pal);
    for (QWidget *w : term->findChildren<QWidget *>()) {
        if (qobject_cast<QScrollBar *>(w))
            continue;
        w->setPalette(pal);
        if (QLatin1String(w->metaObject()->className()) == QLatin1String("Konsole::TerminalDisplay"))
            w->setAutoFillBackground(true);
    }
}

/** Margens em volta do terminal com a mesma opacidade dele. */
void TerminalDock::updateContainerBackground()
{
    QColor c = m_background;
    c.setAlphaF(opacity());
    m_container->setStyleSheet(QStringLiteral("#TerminalContainer { background: rgba(%1,%2,%3,%4); }")
                                   .arg(c.red()).arg(c.green()).arg(c.blue()).arg(c.alpha()));
}

void TerminalDock::setTransparency(int percent)
{
    m_transparency = qBound(0, percent, 90);
    applyOpacity();
}

qreal TerminalDock::opacity() const
{
    return isFloating() ? 1.0 - m_transparency / 100.0 : 1.0;
}

void TerminalDock::applyOpacity()
{
    updateContainerBackground();
    for (QTermWidget *t : terminals())
        t->setTerminalOpacity(opacity());
}

void TerminalDock::setTerminalFont(const QFont &font)
{
    m_font = font;
    for (QTermWidget *t : terminals())
        t->setTerminalFont(font);
}

void TerminalDock::moveToArea(Qt::DockWidgetArea area)
{
    setFloating(false);
    m_mainWindow->removeDockWidget(this);
    m_mainWindow->addDockWidget(area, this);
    show();
    const bool horizontal = (area == Qt::LeftDockWidgetArea || area == Qt::RightDockWidgetArea);
    const int base = horizontal ? m_mainWindow->width() : m_mainWindow->height();
    m_mainWindow->resizeDocks({this}, {horizontal ? base * 2 / 5 : base / 3},
                              horizontal ? Qt::Horizontal : Qt::Vertical);
    Q_EMIT areaChangeRequested(area);
}

void TerminalDock::focusTerminal()
{
    if (QTermWidget *t = current())
        t->setFocus();
}
