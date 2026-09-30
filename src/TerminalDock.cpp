#include "TerminalDock.h"

#include <QActionGroup>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QHBoxLayout>
#include <QLabel>
#include <QMainWindow>
#include <QMenu>
#include <QStandardPaths>
#include <QToolButton>
#include <QVBoxLayout>

#include <qtermwidget.h>

#include "Icons.h"

TerminalDock::TerminalDock(QMainWindow *mainWindow)
    : QDockWidget(tr("Terminal"), mainWindow)
    , m_mainWindow(mainWindow)
    , m_container(new QWidget(this))
{
    setObjectName(QStringLiteral("TerminalDock"));
    setAllowedAreas(Qt::AllDockWidgetAreas);
    setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable
                | QDockWidget::DockWidgetClosable);
    auto *layout = new QVBoxLayout(m_container);
    layout->setContentsMargins(6, 0, 6, 6);
    setWidget(m_container);
    setTitleBarWidget(buildTitleBar());
    registerColorSchemes();
}

/**
 * O QTermWidget lê a lista de esquemas de cores uma única vez, ao criar o
 * primeiro terminal: diretórios registrados depois são ignorados. Por isso
 * todos os esquemas são gravados e o diretório registrado antes disso.
 */
void TerminalDock::registerColorSchemes()
{
    static bool done = false;
    if (done)
        return;
    done = true;
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
                        + QStringLiteral("/color-schemes");
    for (const Theme &theme : Theme::all())
        theme.writeTerminalScheme(dir);
    QTermWidget::addCustomColorSchemeDir(dir);
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
                if (m_term)
                    m_term->clear();
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

void TerminalDock::configure(const QString &shell, bool cursorBlink)
{
    const bool shellChanged = (shell != m_shell);
    m_shell = shell;
    m_cursorBlink = cursorBlink;
    if (m_term) {
        m_term->setBlinkingCursor(cursorBlink);
        if (shellChanged)
            restartShell();
    } else {
        ensureTerminal();
    }
}

void TerminalDock::restartShell()
{
    if (!m_term)
        return;
    QTermWidget *old = m_term;
    m_term = nullptr;
    old->disconnect(this);
    old->deleteLater();
    ensureTerminal();
}

void TerminalDock::ensureTerminal()
{
    if (m_term)
        return;
    m_term = new QTermWidget(0, m_container);
    const QString shell = effectiveShell();
    m_term->setShellProgram(shell);
    QStringList env = QProcessEnvironment::systemEnvironment().toStringList();
    env << QStringLiteral("TERM=xterm-256color");
    m_term->setEnvironment(env);
    m_term->setWorkingDirectory(m_workingDir.isEmpty() ? QDir::homePath() : m_workingDir);
    m_term->setScrollBarPosition(QTermWidget::ScrollBarRight);
    m_term->setTerminalSizeHint(false);
    m_term->setBlinkingCursor(m_cursorBlink);
    if (!m_schemeName.isEmpty())
        m_term->setColorScheme(m_schemeName);
    if (m_font != QFont())
        m_term->setTerminalFont(m_font);
    m_container->layout()->addWidget(m_term);

    // "exit" encerra o shell: recria um novo terminal.
    connect(m_term, &QTermWidget::finished, this, [this]() { restartShell(); });
    m_term->startShellProgram();
}

void TerminalDock::setWorkingDirectory(const QString &dir)
{
    m_workingDir = dir;
}

void TerminalDock::runCommand(const QString &command, const QString &workingDir)
{
    ensureTerminal();
    show();
    raise();
    QString dir = workingDir;
    dir.replace(QLatin1Char('\''), QLatin1String("'\\''"));
    // Ctrl+U limpa o que houver digitado no prompt antes do comando.
    m_term->sendText(QStringLiteral("\x15") + QStringLiteral("cd '") + dir + QStringLiteral("' && clear && ")
                     + command + QLatin1Char('\r'));
    focusTerminal();
}

void TerminalDock::setTheme(const Theme &theme)
{
    for (const auto &[button, glyph] : std::as_const(m_buttons))
        button->setIcon(Icons::icon(glyph, theme.textSecondary, 16));
    m_titleIcon->setPixmap(Icons::icon(Icons::Terminal, theme.primary, 16).pixmap(16, 16));
    m_schemeName = QStringLiteral("SlayerEditor-") + theme.id;
    if (m_term && !m_schemeName.isEmpty())
        m_term->setColorScheme(m_schemeName);
}

void TerminalDock::setTerminalFont(const QFont &font)
{
    m_font = font;
    if (m_term)
        m_term->setTerminalFont(font);
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
    if (m_term)
        m_term->setFocus();
}
