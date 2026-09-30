#include "ViewSpace.h"
#include "CodeEditor.h"
#include "Icons.h"

#include <QHBoxLayout>
#include <QMenu>
#include <QStyle>
#include <QTabBar>
#include <QToolButton>

ViewSpace::ViewSpace(QWidget *parent)
    : QTabWidget(parent)
    , m_splitButton(new QToolButton(this))
{
    setObjectName(QStringLiteral("ViewSpace"));
    setDocumentMode(true);
    setTabsClosable(true);
    setMovable(true);
    tabBar()->setExpanding(false);
    tabBar()->setElideMode(Qt::ElideMiddle);
    tabBar()->setUsesScrollButtons(true);
    tabBar()->setDrawBase(false);

    auto *menu = new QMenu(m_splitButton);
    m_splitSide = menu->addAction(tr("Split Vertical"));
    m_splitSide->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_L));
    m_splitStacked = menu->addAction(tr("Split Horizontal"));
    m_splitStacked->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_T));
    m_moveSide = menu->addAction(tr("Move Document to New Vertical Split"));
    m_moveStacked = menu->addAction(tr("Move Document to New Horizontal Split"));
    menu->addSeparator();
    m_closeView = menu->addAction(tr("Close Current View"));
    m_closeView->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_R));
    m_closeInactive = menu->addAction(tr("Close Inactive Views"));
    m_hideInactive = menu->addAction(tr("Hide Inactive Views"));
    m_hideInactive->setCheckable(true);

    // Os atalhos ficam no menu só como dica: quem os trata é a janela principal.
    for (QAction *a : menu->actions())
        a->setShortcutContext(Qt::WidgetShortcut);

    connect(m_splitSide, &QAction::triggered, this, [this]() { Q_EMIT splitRequested(this, Qt::Horizontal, false); });
    connect(m_splitStacked, &QAction::triggered, this, [this]() { Q_EMIT splitRequested(this, Qt::Vertical, false); });
    connect(m_moveSide, &QAction::triggered, this, [this]() { Q_EMIT splitRequested(this, Qt::Horizontal, true); });
    connect(m_moveStacked, &QAction::triggered, this, [this]() { Q_EMIT splitRequested(this, Qt::Vertical, true); });
    connect(m_closeView, &QAction::triggered, this, [this]() { Q_EMIT closeViewRequested(this); });
    connect(m_closeInactive, &QAction::triggered, this, [this]() { Q_EMIT closeInactiveViewsRequested(this); });
    connect(m_hideInactive, &QAction::toggled, this, [this](bool on) { Q_EMIT hideInactiveViewsToggled(this, on); });
    connect(menu, &QMenu::aboutToShow, this, [this]() {
        const bool hasDoc = count() > 0;
        m_splitSide->setEnabled(hasDoc);
        m_splitStacked->setEnabled(hasDoc);
        m_moveSide->setEnabled(count() > 1);
        m_moveStacked->setEnabled(count() > 1);
    });

    m_splitButton->setObjectName(QStringLiteral("SplitButton"));
    m_splitButton->setAutoRaise(true);
    m_splitButton->setPopupMode(QToolButton::InstantPopup);
    m_splitButton->setMenu(menu);
    m_splitButton->setIconSize(QSize(16, 16));
    m_splitButton->setToolTip(tr("Split view"));
    // Contêiner com fundo próprio: sem ele o Fusion desenha a linha de base
    // da barra de abas sobre o canto.
    auto *corner = new QWidget(this);
    corner->setObjectName(QStringLiteral("SplitCorner"));
    corner->setAttribute(Qt::WA_StyledBackground);
    auto *cornerLayout = new QHBoxLayout(corner);
    cornerLayout->setContentsMargins(0, 0, 0, 0);
    cornerLayout->addWidget(m_splitButton);
    setCornerWidget(corner, Qt::TopRightCorner);
    setMultipleViews(false, false);
}

CodeEditor *ViewSpace::currentEditor() const
{
    return qobject_cast<CodeEditor *>(currentWidget());
}

CodeEditor *ViewSpace::editorAt(int index) const
{
    return qobject_cast<CodeEditor *>(widget(index));
}

QList<CodeEditor *> ViewSpace::editors() const
{
    QList<CodeEditor *> list;
    for (int i = 0; i < count(); ++i)
        list << editorAt(i);
    return list;
}

void ViewSpace::setActive(bool active)
{
    if (m_active == active)
        return;
    m_active = active;
    // A folha de estilo destaca a aba atual do espaço ativo.
    tabBar()->setProperty("activeSpace", active);
    tabBar()->style()->unpolish(tabBar());
    tabBar()->style()->polish(tabBar());
}

void ViewSpace::setTheme(const Theme &theme)
{
    const QColor c = theme.textSecondary;
    m_splitButton->setIcon(Icons::icon(Icons::SplitSideBySide, c, 16));
    m_splitSide->setIcon(Icons::icon(Icons::SplitSideBySide, c, 16));
    m_moveSide->setIcon(Icons::icon(Icons::SplitSideBySide, c, 16));
    m_splitStacked->setIcon(Icons::icon(Icons::SplitStacked, c, 16));
    m_moveStacked->setIcon(Icons::icon(Icons::SplitStacked, c, 16));
    m_closeView->setIcon(Icons::icon(Icons::Close, c, 16));
    m_closeInactive->setIcon(Icons::icon(Icons::CloseAll, c, 16));
    m_hideInactive->setIcon(Icons::icon(Icons::EyeClosed, c, 16));
}

void ViewSpace::setMultipleViews(bool multiple, bool inactiveHidden)
{
    m_closeView->setEnabled(multiple);
    m_closeInactive->setEnabled(multiple);
    m_hideInactive->setEnabled(multiple || inactiveHidden);
    const QSignalBlocker blocker(m_hideInactive);
    m_hideInactive->setChecked(inactiveHidden);
}
