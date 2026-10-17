#include "DebugPanel.h"

#include <QFileInfo>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QSplitter>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {
constexpr int kRefRole = Qt::UserRole;
constexpr int kLoadedRole = Qt::UserRole + 1;

QString pathOf(QTreeWidgetItem *item)
{
    QStringList parts;
    for (QTreeWidgetItem *i = item; i; i = i->parent())
        parts.prepend(i->text(0));
    return parts.join(QLatin1Char('/'));
}
} // namespace

DebugPanel::DebugPanel(DebugSession *session, QWidget *parent)
    : QDockWidget(tr("Debugger"), parent)
    , m_session(session)
    , m_status(new QLabel(this))
    , m_variables(new QTreeWidget(this))
    , m_stack(new QListWidget(this))
{
    setObjectName(QStringLiteral("DebugPanel"));
    setAllowedAreas(Qt::AllDockWidgetAreas);

    m_status->setObjectName(QStringLiteral("Hint"));
    m_status->setWordWrap(true);
    m_variables->setObjectName(QStringLiteral("DebugVariables"));
    m_variables->setHeaderLabels({tr("Name"), tr("Value"), tr("Type")});
    m_variables->setColumnCount(3);
    m_variables->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_variables->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_variables->setAlternatingRowColors(true);
    m_variables->setUniformRowHeights(true);
    m_stack->setObjectName(QStringLiteral("DebugStack"));

    auto *varsBox = new QWidget;
    auto *varsLayout = new QVBoxLayout(varsBox);
    varsLayout->setContentsMargins(0, 0, 0, 0);
    auto *varsTitle = new QLabel(tr("VARIABLES"));
    varsTitle->setObjectName(QStringLiteral("DockTitle"));
    varsLayout->addWidget(varsTitle);
    varsLayout->addWidget(m_variables, 1);

    auto *stackBox = new QWidget;
    auto *stackLayout = new QVBoxLayout(stackBox);
    stackLayout->setContentsMargins(0, 0, 0, 0);
    auto *stackTitle = new QLabel(tr("CALL STACK"));
    stackTitle->setObjectName(QStringLiteral("DockTitle"));
    stackLayout->addWidget(stackTitle);
    stackLayout->addWidget(m_stack, 1);

    auto *split = new QSplitter(Qt::Vertical);
    split->addWidget(varsBox);
    split->addWidget(stackBox);
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 1);

    auto *content = new QWidget(this);
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(8, 6, 8, 8);
    layout->addWidget(m_status);
    layout->addWidget(split, 1);
    setWidget(content);
    setStatus(tr("Not debugging. Press Ctrl+F5 to debug the current file."));

    connect(m_session, &DebugSession::stopped, this, [this](const QList<DebugFrame> &frames, const QString &reason) {
        m_frames = frames;
        m_stack->clear();
        for (const DebugFrame &f : frames) {
            auto *item = new QListWidgetItem(QStringLiteral("%1  —  %2:%3")
                                                 .arg(f.name, QFileInfo(f.path).fileName())
                                                 .arg(f.line + 1));
            item->setToolTip(f.path);
            m_stack->addItem(item);
        }
        m_stack->setCurrentRow(0);
        setStatus(tr("Paused (%1)").arg(reason));
    });
    connect(m_session, &DebugSession::scopesReady, this, &DebugPanel::showScopes);
    connect(m_session, &DebugSession::stateChanged, this, [this](DebugSession::State s) {
        if (s == DebugSession::State::Running) {
            setStatus(tr("Running…"));
        } else if (s == DebugSession::State::Starting) {
            setStatus(tr("Starting the debugger…"));
            m_expanded.clear();
        } else if (s == DebugSession::State::Idle) {
            ++m_generation;
            m_variables->clear();
            m_stack->clear();
            m_frames.clear();
        }
    });
    connect(m_session, &DebugSession::finished, this, [this](const QString &msg) { setStatus(msg); });
    connect(m_session, &DebugSession::status, this, [this](const QString &msg) { setStatus(msg); });
    connect(m_stack, &QListWidget::itemActivated, this, [this](QListWidgetItem *item) {
        const int row = m_stack->row(item);
        if (row >= 0 && row < m_frames.size()) {
            m_session->selectFrame(m_frames[row].id);
            Q_EMIT frameActivated(m_frames[row]);
        }
    });
    connect(m_stack, &QListWidget::itemClicked, m_stack, &QListWidget::itemActivated);

    // Expansão preguiçosa: os filhos são pedidos ao adaptador ao abrir o item.
    connect(m_variables, &QTreeWidget::itemExpanded, this, [this](QTreeWidgetItem *item) {
        m_expanded.insert(pathOf(item), true);
        if (item->data(0, kLoadedRole).toBool())
            return;
        item->setData(0, kLoadedRole, true);
        // A árvore é refeita a cada parada: resposta de uma geração antiga
        // apontaria para um item já destruído.
        const int generation = m_generation;
        m_session->variables(item->data(0, kRefRole).toInt(), [this, generation, item](QList<DebugVariable> vars) {
            if (generation != m_generation)
                return;
            qDeleteAll(item->takeChildren());
            fill(item, vars);
        });
    });
    connect(m_variables, &QTreeWidget::itemCollapsed, this,
            [this](QTreeWidgetItem *item) { m_expanded.remove(pathOf(item)); });
}

void DebugPanel::setStatus(const QString &text)
{
    m_status->setText(text);
}

void DebugPanel::showScopes(const QList<QPair<QString, int>> &scopes)
{
    ++m_generation;
    m_variables->clear();
    for (int i = 0; i < scopes.size(); ++i) {
        auto *scope = new QTreeWidgetItem(m_variables, {scopes[i].first});
        QFont f = scope->font(0);
        f.setBold(true);
        scope->setFont(0, f);
        scope->setData(0, kRefRole, scopes[i].second);
        scope->setChildIndicatorPolicy(QTreeWidgetItem::ShowIndicator);
        // O primeiro escopo (Locals) abre sozinho; os demais, se já estavam abertos.
        if (i == 0 || m_expanded.value(pathOf(scope)))
            scope->setExpanded(true);
    }
}

void DebugPanel::fill(QTreeWidgetItem *parent, const QList<DebugVariable> &vars)
{
    for (const DebugVariable &v : vars) {
        auto *item = new QTreeWidgetItem(parent, {v.name, v.value, v.type});
        item->setToolTip(1, v.value);
        item->setData(0, kRefRole, v.reference);
        if (v.reference > 0) {
            item->setChildIndicatorPolicy(QTreeWidgetItem::ShowIndicator);
            if (m_expanded.value(pathOf(item)))
                item->setExpanded(true);
        }
    }
}
