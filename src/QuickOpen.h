#pragma once

#include <QDialog>
#include <QPointer>

class Project;
class QLineEdit;
class QListWidget;
class QLabel;

/** Quick Open (Ctrl+P): acha um arquivo do projeto pelo nome, com busca aproximada. */
class QuickOpen : public QDialog
{
    Q_OBJECT
public:
    QuickOpen(Project *project, QWidget *parent = nullptr);

Q_SIGNALS:
    void fileChosen(const QString &path);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void filter();
    void accept() override;

    QPointer<Project> m_project;
    QLineEdit *m_input;
    QListWidget *m_list;
    QLabel *m_info;
};
