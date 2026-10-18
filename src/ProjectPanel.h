#pragma once

#include <QDockWidget>
#include <QPointer>

class Project;
class QFileSystemModel;
class QSortFilterProxyModel;
class QTreeView;
class QLabel;

/** Painel lateral com a árvore de arquivos do projeto (pasta aberta). */
class ProjectPanel : public QDockWidget
{
    Q_OBJECT
public:
    explicit ProjectPanel(QWidget *parent = nullptr);

    /** nullptr = nenhum projeto aberto. */
    void setProject(Project *project);
    /** Seleciona (e expande até) o arquivo na árvore, se ele estiver no projeto. */
    void reveal(const QString &path);

Q_SIGNALS:
    void fileActivated(const QString &path);
    /** A árvore mudou no disco (arquivo criado, apagado ou renomeado pelo painel). */
    void filesChanged();
    /** Um arquivo aberto foi renomeado ou apagado aqui: as abas devem acompanhar. */
    void fileRenamed(const QString &from, const QString &to);
    void fileRemoved(const QString &path);

private:
    void showContextMenu(const QPoint &pos);
    QString pathAt(const QModelIndex &proxyIndex) const;
    void createEntry(const QString &dir, bool folder);
    void renameEntry(const QString &path);
    void trashEntry(const QString &path);

    QPointer<Project> m_project;
    QFileSystemModel *m_model;
    QSortFilterProxyModel *m_proxy;
    QTreeView *m_tree;
    QLabel *m_empty;
};
