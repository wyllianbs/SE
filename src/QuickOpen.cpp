#include "QuickOpen.h"

#include "Project.h"

#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QVBoxLayout>

#include <algorithm>

QuickOpen::QuickOpen(Project *project, QWidget *parent)
    : QDialog(parent, Qt::Popup | Qt::FramelessWindowHint)
    , m_project(project)
    , m_input(new QLineEdit)
    , m_list(new QListWidget)
    , m_info(new QLabel)
{
    setObjectName(QStringLiteral("QuickOpen"));
    setAttribute(Qt::WA_DeleteOnClose);
    m_input->setPlaceholderText(tr("Search files by name (%1)").arg(project->name()));
    m_input->setClearButtonEnabled(true);
    m_input->installEventFilter(this);
    m_info->setEnabled(false);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->addWidget(m_input);
    layout->addWidget(m_list);
    layout->addWidget(m_info);
    connect(m_input, &QLineEdit::textChanged, this, &QuickOpen::filter);
    connect(m_list, &QListWidget::itemActivated, this, [this]() { accept(); });
    connect(project, &Project::filesListed, this, &QuickOpen::filter);
    if (parent) {
        const QRect r = parent->geometry();
        const int w = qMin(640, r.width() - 40);
        setGeometry(r.x() + (r.width() - w) / 2, r.y() + 60, w, qMin(420, r.height() - 100));
    }
    project->refreshFiles(); // a árvore pode ter mudado; enquanto isso usa o cache
    filter();
    m_input->setFocus();
}

void QuickOpen::filter()
{
    if (!m_project)
        return;
    if (!m_project->filesReady()) {
        m_info->setText(tr("Listing files…"));
        return;
    }
    const QString pattern = m_input->text().trimmed();
    const QStringList files = m_project->files();
    QList<QPair<int, int>> scored; // (pontuação, índice)
    for (int i = 0; i < files.size(); ++i) {
        const int s = Project::fuzzyScore(pattern, files.at(i));
        if (s >= 0)
            scored.append({s, i});
    }
    constexpr int maxShown = 200;
    const auto byScore = [](const QPair<int, int> &a, const QPair<int, int> &b) {
        return a.first != b.first ? a.first > b.first : a.second < b.second;
    };
    if (scored.size() > maxShown) {
        std::partial_sort(scored.begin(), scored.begin() + maxShown, scored.end(), byScore);
        scored.resize(maxShown);
    } else {
        std::sort(scored.begin(), scored.end(), byScore);
    }
    m_list->clear();
    for (const auto &[score, i] : std::as_const(scored)) {
        const QString &rel = files.at(i);
        const int slash = rel.lastIndexOf(QLatin1Char('/'));
        auto *item = new QListWidgetItem(slash < 0 ? rel : rel.mid(slash + 1) + QStringLiteral("    ") + rel.left(slash));
        item->setData(Qt::UserRole, rel);
        item->setToolTip(rel);
        m_list->addItem(item);
    }
    m_list->setCurrentRow(0);
    QString info = tr("%n file(s)", nullptr, int(files.size()));
    if (m_project->filesTruncated())
        info += QStringLiteral(" — ") + tr("listing truncated (very large folder)");
    m_info->setText(info);
}

bool QuickOpen::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_input && event->type() == QEvent::KeyPress) {
        auto *ke = static_cast<QKeyEvent *>(event);
        switch (ke->key()) {
        case Qt::Key_Down:
        case Qt::Key_Up:
        case Qt::Key_PageDown:
        case Qt::Key_PageUp:
            QCoreApplication::sendEvent(m_list, event);
            return true;
        case Qt::Key_Return:
        case Qt::Key_Enter:
            accept();
            return true;
        default:
            break;
        }
    }
    return QDialog::eventFilter(watched, event);
}

void QuickOpen::accept()
{
    if (QListWidgetItem *item = m_list->currentItem(); item && m_project)
        Q_EMIT fileChosen(m_project->absolutePath(item->data(Qt::UserRole).toString()));
    QDialog::accept();
}
