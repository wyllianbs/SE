#include "ProjectPanel.h"

#include "Project.h"

#include <QApplication>
#include <QClipboard>
#include <QFileSystemModel>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QSortFilterProxyModel>
#include <QStackedWidget>
#include <QTreeView>

namespace {

/** Esconde o que o projeto exclui (.git, node_modules…); pastas primeiro. */
class ProjectFilter : public QSortFilterProxyModel
{
public:
    using QSortFilterProxyModel::QSortFilterProxyModel;
    QPointer<Project> project;

protected:
    bool filterAcceptsRow(int row, const QModelIndex &parent) const override
    {
        if (!project)
            return true;
        auto *fs = static_cast<QFileSystemModel *>(sourceModel());
        const QModelIndex idx = fs->index(row, 0, parent);
        return !project->isHidden(fs->filePath(idx), fs->isDir(idx));
    }
    bool lessThan(const QModelIndex &a, const QModelIndex &b) const override
    {
        auto *fs = static_cast<QFileSystemModel *>(sourceModel());
        const bool da = fs->isDir(a), db = fs->isDir(b);
        if (da != db)
            return da;
        return QString::compare(fs->fileName(a), fs->fileName(b), Qt::CaseInsensitive) < 0;
    }
};

} // namespace

ProjectPanel::ProjectPanel(QWidget *parent)
    : QDockWidget(tr("Project"), parent)
    , m_model(new QFileSystemModel(this))
    , m_proxy(new ProjectFilter(this))
    , m_tree(new QTreeView)
    , m_empty(new QLabel(tr("No folder open.\nFile ▸ Open Folder… (Ctrl+Shift+O)")))
{
    setObjectName(QStringLiteral("ProjectPanel"));
    setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable
                | QDockWidget::DockWidgetClosable);
    m_model->setReadOnly(true);
    m_model->setFilter(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
    m_proxy->setSourceModel(m_model);
    m_proxy->setDynamicSortFilter(true);
    m_tree->setModel(m_proxy);
    m_tree->setHeaderHidden(true);
    for (int c = 1; c < m_model->columnCount(); ++c)
        m_tree->hideColumn(c);
    m_tree->setSortingEnabled(true);
    m_tree->sortByColumn(0, Qt::AscendingOrder);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    m_tree->setFrameShape(QFrame::NoFrame);
    m_empty->setAlignment(Qt::AlignCenter);
    m_empty->setWordWrap(true);
    m_empty->setEnabled(false);
    auto *stack = new QStackedWidget;
    stack->addWidget(m_empty);
    stack->addWidget(m_tree);
    setWidget(stack);

    connect(m_tree, &QTreeView::activated, this, [this](const QModelIndex &idx) {
        const QString path = pathAt(idx);
        if (!path.isEmpty() && QFileInfo(path).isFile())
            Q_EMIT fileActivated(path);
    });
    connect(m_tree, &QTreeView::customContextMenuRequested, this, &ProjectPanel::showContextMenu);
}

void ProjectPanel::setProject(Project *project)
{
    m_project = project;
    static_cast<ProjectFilter *>(m_proxy)->project = project;
    auto *stack = static_cast<QStackedWidget *>(widget());
    if (!project) {
        m_model->setRootPath(QString());
        stack->setCurrentWidget(m_empty);
        setWindowTitle(tr("Project"));
        return;
    }
    const QModelIndex root = m_model->setRootPath(project->root());
    m_proxy->invalidate();
    m_tree->setRootIndex(m_proxy->mapFromSource(root));
    stack->setCurrentWidget(m_tree);
    setWindowTitle(project->name());
}

void ProjectPanel::reveal(const QString &path)
{
    if (!m_project || !m_project->contains(path))
        return;
    const QModelIndex idx = m_proxy->mapFromSource(m_model->index(path));
    if (!idx.isValid())
        return;
    m_tree->setCurrentIndex(idx);
    m_tree->scrollTo(idx);
}

QString ProjectPanel::pathAt(const QModelIndex &proxyIndex) const
{
    return proxyIndex.isValid() ? m_model->filePath(m_proxy->mapToSource(proxyIndex)) : QString();
}

void ProjectPanel::showContextMenu(const QPoint &pos)
{
    if (!m_project)
        return;
    const QString path = pathAt(m_tree->indexAt(pos));
    const QFileInfo fi(path);
    const QString dir = path.isEmpty() ? m_project->root() : (fi.isDir() ? path : fi.absolutePath());
    QMenu menu(this);
    if (fi.isFile())
        connect(menu.addAction(tr("Open")), &QAction::triggered, this, [this, path]() { Q_EMIT fileActivated(path); });
    connect(menu.addAction(tr("New File…")), &QAction::triggered, this, [this, dir]() { createEntry(dir, false); });
    connect(menu.addAction(tr("New Folder…")), &QAction::triggered, this, [this, dir]() { createEntry(dir, true); });
    if (!path.isEmpty()) {
        menu.addSeparator();
        connect(menu.addAction(tr("Rename…")), &QAction::triggered, this, [this, path]() { renameEntry(path); });
        connect(menu.addAction(tr("Move to Trash")), &QAction::triggered, this, [this, path]() { trashEntry(path); });
    }
    menu.addSeparator();
    const QString target = path.isEmpty() ? m_project->root() : path;
    connect(menu.addAction(tr("Copy Path")), &QAction::triggered, this,
            [target]() { QApplication::clipboard()->setText(target); });
    connect(menu.addAction(tr("Copy Relative Path")), &QAction::triggered, this,
            [this, target]() { QApplication::clipboard()->setText(m_project->relativePath(target)); });
    menu.exec(m_tree->viewport()->mapToGlobal(pos));
}

void ProjectPanel::createEntry(const QString &dir, bool folder)
{
    const QString name = QInputDialog::getText(this, folder ? tr("New Folder") : tr("New File"), tr("Name:"))
                             .trimmed();
    if (name.isEmpty())
        return;
    const QString path = QDir(dir).filePath(name);
    if (QFileInfo::exists(path)) {
        QMessageBox::warning(this, tr("Project"), tr("“%1” already exists.").arg(name));
        return;
    }
    bool ok;
    if (folder) {
        ok = QDir().mkpath(path);
    } else {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile f(path);
        ok = f.open(QIODevice::WriteOnly);
    }
    if (!ok) {
        QMessageBox::warning(this, tr("Project"), tr("Could not create “%1”.").arg(name));
        return;
    }
    Q_EMIT filesChanged();
    if (!folder)
        Q_EMIT fileActivated(path);
}

void ProjectPanel::renameEntry(const QString &path)
{
    const QFileInfo fi(path);
    const QString name = QInputDialog::getText(this, tr("Rename"), tr("New name:"), QLineEdit::Normal,
                                               fi.fileName()).trimmed();
    if (name.isEmpty() || name == fi.fileName())
        return;
    const QString to = fi.dir().filePath(name);
    if (QFileInfo::exists(to) || !QDir().rename(path, to)) {
        QMessageBox::warning(this, tr("Project"), tr("Could not rename to “%1”.").arg(name));
        return;
    }
    Q_EMIT fileRenamed(path, to);
    Q_EMIT filesChanged();
}

void ProjectPanel::trashEntry(const QString &path)
{
    const QString name = QFileInfo(path).fileName();
    if (QMessageBox::question(this, tr("Move to Trash"), tr("Move “%1” to the trash?").arg(name))
        != QMessageBox::Yes)
        return;
    if (!QFile::moveToTrash(path)) {
        QMessageBox::warning(this, tr("Project"), tr("Could not move “%1” to the trash.").arg(name));
        return;
    }
    Q_EMIT fileRemoved(path);
    Q_EMIT filesChanged();
}
