#include "TerminalDock.h"

#include <QActionGroup>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QHBoxLayout>
#include <QLabel>
#include <QMainWindow>
#include <QMenu>
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
    term->setScrollBarPosition(QTermWidget::ScrollBarRight);
    term->setTerminalSizeHint(false);
    term->setBlinkingCursor(m_cursorBlink);
    if (!m_schemeName.isEmpty())
        term->setColorScheme(m_schemeName);
    if (m_font != QFont())
        term->setTerminalFont(m_font);
    term->setTerminalOpacity(opacity());
    return term;
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
        if (i >= 0 && !title.isEmpty()) {
            m_tabs->setTabText(i, title.length() > 32 ? QStringLiteral("…") + title.right(31) : title);
            m_tabs->setTabToolTip(i, title);
        }
    });
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

QString TerminalDock::newTeletype(const QString &title)
{
    // Um PTY cujo processo nunca lê o teclado: o programa depurado (em outra
    // sessão, sem terminal de controle) lê e escreve nele, e o que se digita
    // chega a ele com eco normal. (O modo "teletype" do QTermWidget não serve:
    // as teclas viram sinais em vez de ir para o PTY.)
    auto *term = new QTermWidget(0, m_tabs);
    term->setShellProgram(QStringLiteral("/bin/sleep"));
    term->setArgs({QStringLiteral("infinity")});
    term->setScrollBarPosition(QTermWidget::ScrollBarRight);
    term->setTerminalSizeHint(false);
    term->setBlinkingCursor(m_cursorBlink);
    if (!m_schemeName.isEmpty())
        term->setColorScheme(m_schemeName);
    if (m_font != QFont())
        term->setTerminalFont(m_font);
    term->setTerminalOpacity(opacity());
    term->startShellProgram();
    const int fd = term->getPtySlaveFd();
    const char *name = fd >= 0 ? ::ttyname(fd) : nullptr;
    if (!name) {
        delete term;
        return QString();
    }
    const int index = m_tabs->addTab(term, title);
    auto *close = new QToolButton(m_tabs->tabBar());
    close->setObjectName(QStringLiteral("TabClose"));
    close->setAutoRaise(true);
    close->setIconSize(QSize(12, 12));
    close->setToolTip(tr("Close terminal tab"));
    connect(close, &QToolButton::clicked, this, [this, term]() { closeTerminal(m_tabs->indexOf(term)); });
    m_tabs->tabBar()->setTabButton(index, QTabBar::RightSide, close);
    m_tabs->setCurrentIndex(index);
    updateTabBar();
    show();
    raise();
    return QString::fromLocal8Bit(name);
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

void TerminalDock::runCommand(const QString &command, const QString &workingDir)
{
    if (m_tabs->count() == 0 || !current()->getShellPID())
        newTerminal();
    show();
    raise();
    QString dir = workingDir;
    dir.replace(QLatin1Char('\''), QLatin1String("'\\''"));
    const QString line = QStringLiteral("cd '") + dir + QStringLiteral("' && clear && ") + command + QLatin1Char('\r');
    QTermWidget *term = current();
    sendWhenIdle(term, line, 0);
    focusTerminal();
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
    const QJsonObject colors = theme.toJson();
    if (colors != m_schemeColors) {
        m_schemeColors = colors;
        const QString name = QStringLiteral("SlayerEditor-%1-%2").arg(QCoreApplication::applicationPid()).arg(m_nextSlot++);
        theme.writeTerminalScheme(schemeDir(), name);
        // Caminho completo: o QTermWidget lê o arquivo, sem depender da lista
        // de esquemas que ele montou na primeira consulta.
        m_schemeName = schemeDir() + QLatin1Char('/') + name + QStringLiteral(".colorscheme");
    }
    m_background = theme.terminalBackground;
    updateContainerBackground();
    for (QTermWidget *t : terminals())
        t->setColorScheme(m_schemeName);
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
