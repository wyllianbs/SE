#include "ToolbarDialog.h"

#include <QAction>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

namespace {
constexpr int kIdRole = Qt::UserRole;
const QString kSeparator = QStringLiteral("separator");

QString plainText(QString text)
{
    text.remove(QLatin1Char('&'));
    if (text.endsWith(QStringLiteral("…")))
        text.chop(1);
    return text;
}
} // namespace

ToolbarDialog::ToolbarDialog(const QList<QPair<QString, QAction *>> &actions, const QStringList &current,
                             const QStringList &defaults, QWidget *parent)
    : QDialog(parent)
    , m_actions(actions)
    , m_defaults(defaults)
    , m_available(new QListWidget(this))
    , m_current(new QListWidget(this))
    , m_add(new QPushButton(QStringLiteral("→"), this))
    , m_remove(new QPushButton(QStringLiteral("←"), this))
    , m_up(new QPushButton(QStringLiteral("↑"), this))
    , m_down(new QPushButton(QStringLiteral("↓"), this))
{
    setWindowTitle(tr("Configure Toolbars"));
    setObjectName(QStringLiteral("SettingsDialog"));
    resize(760, 520);

    for (QListWidget *l : {m_available, m_current}) {
        l->setObjectName(QStringLiteral("ProfileList"));
        l->setIconSize(QSize(18, 18));
    }
    m_current->setDragDropMode(QAbstractItemView::InternalMove);
    m_add->setToolTip(tr("Add to the toolbar"));
    m_remove->setToolTip(tr("Remove from the toolbar"));
    m_up->setToolTip(tr("Move up"));
    m_down->setToolTip(tr("Move down"));

    auto *filter = new QLineEdit(this);
    filter->setPlaceholderText(tr("Filter"));
    filter->setClearButtonEnabled(true);
    connect(filter, &QLineEdit::textChanged, this, [this](const QString &t) {
        for (int i = 0; i < m_available->count(); ++i)
            m_available->item(i)->setHidden(!m_available->item(i)->text().contains(t, Qt::CaseInsensitive));
    });

    auto *arrows = new QVBoxLayout;
    arrows->addStretch(1);
    for (QPushButton *b : {m_add, m_remove, m_up, m_down}) {
        b->setAutoDefault(false);
        b->setFixedWidth(44);
        arrows->addWidget(b);
    }
    arrows->addStretch(1);

    auto *grid = new QGridLayout;
    grid->addWidget(new QLabel(tr("Available actions:")), 0, 0);
    grid->addWidget(new QLabel(tr("Current actions:")), 0, 2);
    grid->addWidget(filter, 1, 0);
    grid->addWidget(m_available, 2, 0);
    grid->addLayout(arrows, 2, 1);
    grid->addWidget(m_current, 1, 2, 2, 1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel
                                         | QDialogButtonBox::RestoreDefaults, this);
    buttons->button(QDialogButtonBox::RestoreDefaults)->setText(tr("Defaults"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons->button(QDialogButtonBox::RestoreDefaults), &QPushButton::clicked, this,
            [this]() { fill(m_defaults); });

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 16, 16, 12);
    layout->addLayout(grid, 1);
    layout->addWidget(buttons);

    connect(m_add, &QPushButton::clicked, this, [this]() {
        QListWidgetItem *item = m_available->currentItem();
        if (!item)
            return;
        const QString id = item->data(kIdRole).toString();
        const int at = m_current->currentRow() < 0 ? m_current->count() : m_current->currentRow() + 1;
        m_current->insertItem(at, makeItem(id));
        m_current->setCurrentRow(at);
        if (id != kSeparator) // separador pode repetir; ações, não
            delete m_available->takeItem(m_available->row(item));
        updateButtons();
    });
    connect(m_remove, &QPushButton::clicked, this, [this]() {
        const int row = m_current->currentRow();
        if (row < 0)
            return;
        const QString id = m_current->item(row)->data(kIdRole).toString();
        delete m_current->takeItem(row);
        if (id != kSeparator)
            m_available->addItem(makeItem(id));
        updateButtons();
    });
    auto move = [this](int delta) {
        const int row = m_current->currentRow();
        const int to = row + delta;
        if (row < 0 || to < 0 || to >= m_current->count())
            return;
        QListWidgetItem *item = m_current->takeItem(row);
        m_current->insertItem(to, item);
        m_current->setCurrentRow(to);
    };
    connect(m_up, &QPushButton::clicked, this, [move]() { move(-1); });
    connect(m_down, &QPushButton::clicked, this, [move]() { move(1); });
    connect(m_available, &QListWidget::itemDoubleClicked, m_add, &QPushButton::click);
    connect(m_current, &QListWidget::itemDoubleClicked, m_remove, &QPushButton::click);
    connect(m_available, &QListWidget::currentRowChanged, this, &ToolbarDialog::updateButtons);
    connect(m_current, &QListWidget::currentRowChanged, this, &ToolbarDialog::updateButtons);

    fill(current);
}

QListWidgetItem *ToolbarDialog::makeItem(const QString &id) const
{
    if (id == kSeparator) {
        auto *item = new QListWidgetItem(tr("--- separator ---"));
        item->setData(kIdRole, kSeparator);
        return item;
    }
    for (const auto &[aid, action] : m_actions) {
        if (aid == id) {
            auto *item = new QListWidgetItem(action->icon(), plainText(action->text()));
            item->setData(kIdRole, id);
            item->setToolTip(action->toolTip());
            return item;
        }
    }
    return nullptr;
}

void ToolbarDialog::fill(const QStringList &current)
{
    m_available->clear();
    m_current->clear();
    m_available->addItem(makeItem(kSeparator));
    for (const QString &id : current) {
        if (QListWidgetItem *item = makeItem(id))
            m_current->addItem(item);
    }
    for (const auto &[id, action] : m_actions) {
        if (!current.contains(id))
            m_available->addItem(makeItem(id));
    }
    updateButtons();
}

void ToolbarDialog::updateButtons()
{
    m_add->setEnabled(m_available->currentItem());
    m_remove->setEnabled(m_current->currentItem());
    m_up->setEnabled(m_current->currentRow() > 0);
    m_down->setEnabled(m_current->currentRow() >= 0 && m_current->currentRow() < m_current->count() - 1);
}

QStringList ToolbarDialog::currentIds() const
{
    QStringList ids;
    for (int i = 0; i < m_current->count(); ++i)
        ids << m_current->item(i)->data(kIdRole).toString();
    return ids;
}
