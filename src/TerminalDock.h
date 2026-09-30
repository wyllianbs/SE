#pragma once

#include "Icons.h"
#include "Theme.h"

#include <QDockWidget>

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
    /** Shell (vazio = $SHELL) e cursor piscante. */
    void configure(const QString &shell, bool cursorBlink);
    void setWorkingDirectory(const QString &dir);
    void restartShell();
    /** 0 = opaco … 90 = quase invisível. */
    void setTransparency(int percent);

    /** Abre uma aba de terminal nova e a torna a atual. */
    QTermWidget *newTerminal();
    void closeTerminal(int index);
    QTermWidget *current() const;
    QList<QTermWidget *> terminals() const;

    static QString areaKey(Qt::DockWidgetArea area);
    static Qt::DockWidgetArea areaFromKey(const QString &key);

Q_SIGNALS:
    void areaChangeRequested(Qt::DockWidgetArea area);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QWidget *buildTitleBar();
    Qt::Edges edgesAt(const QPoint &pos) const;
    static constexpr int kGrip = 8; //< largura das bordas de redimensionar (destacado)
    QTermWidget *createTerminal();
    void updateTabBar();
    void refreshTabIcons();
    QString effectiveShell() const;
    static void registerColorSchemes();

    QMainWindow *m_mainWindow;
    QWidget *m_container;
    class QTabWidget *m_tabs = nullptr;
    int m_counter = 0;
    QColor m_iconColor;
    QString m_schemeName;
    QFont m_font;
    QString m_shell;
    QString m_workingDir;
    bool m_cursorBlink = true;
    int m_transparency = 0;
    QColor m_background;
    void updateContainerBackground();
    QLabel *m_titleIcon = nullptr;
    QList<QPair<QToolButton *, Icons::Glyph>> m_buttons;
};
