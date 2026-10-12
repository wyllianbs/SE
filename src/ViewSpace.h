#pragma once

#include "Theme.h"

#include <QTabWidget>

class CodeEditor;
class QAction;
class QToolButton;

/**
 * Espaço de visão: um grupo de abas de editores. A janela principal os
 * organiza em divisores aninhados (split view, como no Kate). O botão no
 * canto direito da barra de abas abre o menu de divisão.
 */
class ViewSpace : public QTabWidget
{
    Q_OBJECT
public:
    explicit ViewSpace(QWidget *parent = nullptr);

    CodeEditor *currentEditor() const;
    CodeEditor *editorAt(int index) const;
    QList<CodeEditor *> editors() const;

    void setActive(bool active);
    bool isActive() const { return m_active; }
    void setTheme(const Theme &theme);
    /** Habilita as ações que dependem de haver mais de um espaço de visão. */
    void setMultipleViews(bool multiple, bool inactiveHidden);

Q_SIGNALS:
    void splitRequested(ViewSpace *space, Qt::Orientation orientation, bool moveDocument);
    void closeViewRequested(ViewSpace *space);
    void closeInactiveViewsRequested(ViewSpace *space);
    void hideInactiveViewsToggled(ViewSpace *space, bool hide);
    /** Menu de contexto / duplo clique na barra de abas. */
    void newTabRequested(ViewSpace *space);
    void tabActionRequested(ViewSpace *space, int index, const QString &action);
    /** Arquivos locais soltos sobre a barra de abas: abrir em novas abas. */
    void filesDropped(const QStringList &paths);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    // A área vazia à direita das abas pertence ao QTabWidget, não à QTabBar.
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    bool inEmptyTabStrip(const QPoint &pos) const;
    void showTabMenu(int index, const QPoint &globalPos);

private:
    QToolButton *m_splitButton;
    QAction *m_splitSide;
    QAction *m_splitStacked;
    QAction *m_moveSide;
    QAction *m_moveStacked;
    QAction *m_closeView;
    QAction *m_closeInactive;
    QAction *m_hideInactive;
    bool m_active = false;
};
