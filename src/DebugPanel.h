#pragma once

#include "DebugSession.h"

#include <QDockWidget>

class QLabel;
class QListWidget;
class QTreeWidget;
class QTreeWidgetItem;

/** Painel do depurador: variáveis (expansíveis) e pilha de chamadas. */
class DebugPanel : public QDockWidget
{
    Q_OBJECT
public:
    DebugPanel(DebugSession *session, QWidget *parent = nullptr);

Q_SIGNALS:
    /** O usuário escolheu um quadro da pilha: mostrar a linha dele. */
    void frameActivated(const DebugFrame &frame);

private:
    void showScopes(const QList<QPair<QString, int>> &scopes);
    void fill(QTreeWidgetItem *parent, const QList<DebugVariable> &vars);
    void setStatus(const QString &text);

    DebugSession *m_session;
    QLabel *m_status;
    QTreeWidget *m_variables;
    QListWidget *m_stack;
    QList<DebugFrame> m_frames;
    QHash<QString, bool> m_expanded; //< caminho do item → aberto (preservado entre passos)
    int m_generation = 0;
};
