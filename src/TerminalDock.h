#pragma once

#include "Icons.h"
#include "Theme.h"

#include <QDockWidget>
#include <QPointer>

class QMainWindow;
class QTermWidget;
class QLabel;
class QToolButton;

/**
 * Terminal embutido (QTermWidget) num QDockWidget. Pode ser arrastado ou
 * enviado pelo menu da barra de título para esquerda, topo, direita ou base,
 * além de destacado como janela flutuante.
 */
class TerminalDock : public QDockWidget
{
    Q_OBJECT
public:
    explicit TerminalDock(QMainWindow *mainWindow);

    /** Executa um comando no shell, a partir de um diretório. */
    void runCommand(const QString &command, const QString &workingDir);
    void setTheme(const Theme &theme);
    void setTerminalFont(const QFont &font);
    void moveToArea(Qt::DockWidgetArea area);
    void focusTerminal();

    static QString areaKey(Qt::DockWidgetArea area);
    static Qt::DockWidgetArea areaFromKey(const QString &key);

Q_SIGNALS:
    void areaChangeRequested(Qt::DockWidgetArea area);

private:
    QWidget *buildTitleBar();
    void ensureTerminal();

    QMainWindow *m_mainWindow;
    QWidget *m_container;
    QPointer<QTermWidget> m_term;
    QString m_schemeName;
    QFont m_font;
    QLabel *m_titleIcon = nullptr;
    QList<QPair<QToolButton *, Icons::Glyph>> m_buttons;
};
