#include "ProjectSearch.h"

#include "Project.h"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QThread>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <memory>

namespace {
constexpr int kMaxMatches = 5000;
enum Role { PathRole = Qt::UserRole, LineRole, ColumnRole, LengthRole };
} // namespace

ProjectSearch::ProjectSearch(QWidget *parent)
    : QDockWidget(tr("Search"), parent)
    , m_input(new QLineEdit)
    , m_case(new QCheckBox(tr("Aa")))
    , m_word(new QCheckBox(tr("Word")))
    , m_regex(new QCheckBox(tr(".*")))
    , m_results(new QTreeWidget)
    , m_status(new QLabel)
{
    setObjectName(QStringLiteral("ProjectSearch"));
    setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable
                | QDockWidget::DockWidgetClosable);
    m_input->setPlaceholderText(tr("Search in project (Enter)"));
    m_input->setClearButtonEnabled(true);
    m_case->setToolTip(tr("Match case"));
    m_word->setToolTip(tr("Whole words"));
    m_regex->setToolTip(tr("Regular expression"));
    m_results->setHeaderHidden(true);
    m_results->setUniformRowHeights(true);
    m_results->setFrameShape(QFrame::NoFrame);
    m_status->setEnabled(false);
    m_status->setWordWrap(true);

    auto *opts = new QHBoxLayout;
    opts->addWidget(m_case);
    opts->addWidget(m_word);
    opts->addWidget(m_regex);
    opts->addStretch(1);
    auto *box = new QWidget;
    auto *layout = new QVBoxLayout(box);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->addWidget(m_input);
    layout->addLayout(opts);
    layout->addWidget(m_status);
    layout->addWidget(m_results, 1);
    setWidget(box);

    connect(m_input, &QLineEdit::returnPressed, this, &ProjectSearch::run);
    for (QCheckBox *c : {m_case, m_word, m_regex})
        connect(c, &QCheckBox::toggled, this, [this]() {
            if (!m_input->text().isEmpty())
                run();
        });
    connect(m_results, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem *item) {
        if (!m_project || item->data(0, LineRole).isNull())
            return;
        Q_EMIT matchActivated(m_project->absolutePath(item->data(0, PathRole).toString()),
                              item->data(0, LineRole).toInt(), item->data(0, ColumnRole).toInt(),
                              item->data(0, LengthRole).toInt());
    });
}

void ProjectSearch::setProject(Project *project)
{
    if (m_project)
        disconnect(m_project, nullptr, this, nullptr);
    m_project = project;
    ++m_generation;
    m_results->clear();
    m_status->clear();
    m_waitingFiles = false;
    setEnabled(project != nullptr);
    if (project)
        connect(project, &Project::filesListed, this, [this]() {
            if (m_waitingFiles) {
                m_waitingFiles = false;
                runOn(m_project->files());
            }
        });
}

void ProjectSearch::activate(const QString &text)
{
    show();
    raise();
    if (!text.isEmpty())
        m_input->setText(text);
    m_input->setFocus();
    m_input->selectAll();
}

void ProjectSearch::run()
{
    if (!m_project || m_input->text().isEmpty())
        return;
    // Lista sempre atualizada: a busca roda quando a listagem terminar.
    m_waitingFiles = true;
    m_status->setText(tr("Searching…"));
    m_project->refreshFiles();
}

void ProjectSearch::runOn(const QStringList &files)
{
    const int generation = ++m_generation;
    const QString root = m_project->root();
    const QString pattern = m_input->text();
    Project::SearchOptions o;
    o.caseSensitive = m_case->isChecked();
    o.wholeWord = m_word->isChecked();
    o.regex = m_regex->isChecked();
    auto result = std::make_shared<std::pair<QList<Project::Match>, bool>>();
    QThread *thread = QThread::create([=]() {
        result->first = Project::search(root, files, pattern, o, kMaxMatches, &result->second);
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    connect(thread, &QThread::finished, this, [this, result, generation, pattern]() {
        if (generation != m_generation)
            return;
        m_results->clear();
        QTreeWidgetItem *fileItem = nullptr;
        QString current;
        int fileCount = 0;
        for (const Project::Match &m : std::as_const(result->first)) {
            if (m.file != current) {
                current = m.file;
                fileItem = new QTreeWidgetItem(m_results, {m.file});
                fileItem->setData(0, PathRole, m.file);
                fileItem->setExpanded(true);
                ++fileCount;
            }
            const QString text = m.text.trimmed().left(300);
            auto *it = new QTreeWidgetItem(fileItem, {QStringLiteral("%1: %2").arg(m.line + 1).arg(text)});
            it->setData(0, PathRole, m.file);
            it->setData(0, LineRole, m.line);
            it->setData(0, ColumnRole, m.column);
            it->setData(0, LengthRole, m.length);
            it->setToolTip(0, m.text);
        }
        QString status = result->first.isEmpty()
                             ? tr("No results for “%1”").arg(pattern)
                             : tr("%n result(s)", nullptr, int(result->first.size()))
                                   + QStringLiteral(" — ") + tr("%n file(s)", nullptr, fileCount);
        if (result->second)
            status += QStringLiteral(" — ") + tr("stopped at %1 results").arg(kMaxMatches);
        m_status->setText(status);
    });
    thread->start(QThread::LowPriority);
}
