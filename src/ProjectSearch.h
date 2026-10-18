#pragma once

#include <QDockWidget>
#include <QPointer>

class Project;
class QCheckBox;
class QLabel;
class QLineEdit;
class QTreeWidget;

/** Buscar no projeto (Ctrl+Shift+F): resultados agrupados por arquivo. */
class ProjectSearch : public QDockWidget
{
    Q_OBJECT
public:
    explicit ProjectSearch(QWidget *parent = nullptr);

    void setProject(Project *project);
    /** Mostra o painel e foca a busca (com o texto dado, se houver). */
    void activate(const QString &text = QString());

Q_SIGNALS:
    /** Abrir o arquivo e selecionar a ocorrência (linha/coluna base 0). */
    void matchActivated(const QString &path, int line, int column, int length);

private:
    void run();
    void runOn(const QStringList &files);

    QPointer<Project> m_project;
    QLineEdit *m_input;
    QCheckBox *m_case;
    QCheckBox *m_word;
    QCheckBox *m_regex;
    QTreeWidget *m_results;
    QLabel *m_status;
    int m_generation = 0;
    bool m_waitingFiles = false;
};
