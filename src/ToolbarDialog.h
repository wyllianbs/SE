#pragma once

#include <QDialog>
#include <QHash>
#include <QStringList>

class QAction;
class QListWidget;
class QListWidgetItem;
class QPushButton;

/**
 * "Configurar barras de ferramentas", como no Kate: ações disponíveis à
 * esquerda, as da barra à direita (com separadores), botões para mover.
 */
class ToolbarDialog : public QDialog
{
    Q_OBJECT
public:
    /** actions: id → ação (texto e ícone vêm dela); current: ids na barra ("separator" = separador). */
    ToolbarDialog(const QList<QPair<QString, QAction *>> &actions, const QStringList &current,
                  const QStringList &defaults, QWidget *parent = nullptr);

    QStringList currentIds() const;

private:
    QListWidgetItem *makeItem(const QString &id) const;
    void fill(const QStringList &current);
    void updateButtons();

    QList<QPair<QString, QAction *>> m_actions;
    QStringList m_defaults;
    QListWidget *m_available;
    QListWidget *m_current;
    QPushButton *m_add;
    QPushButton *m_remove;
    QPushButton *m_up;
    QPushButton *m_down;
};
