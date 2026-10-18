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
    /** Foco ao executar: no terminal já, nunca, ou só quando o programa pedir entrada. */
    enum class RunFocus { Terminal, None, OnInput };
    void runCommand(const QString &command, const QString &workingDir, RunFocus focus = RunFocus::Terminal);
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
    /**
     * Esquema de cores (vazio = cores do tema), forma do cursor (0 bloco,
     * 1 sublinhado, 2 barra), histórico em linhas (-1 = ilimitado) e barra de
     * rolagem (0 oculta, 1 esquerda, 2 direita) de todas as abas.
     */
    void setAppearance(const QString &colorScheme, int cursorShape, int historyLines, int scrollbar);
    /** Esquemas de cores disponíveis (do QTermWidget/Konsole). */
    static QStringList colorSchemes();

    /** Abre uma aba de terminal nova e a torna a atual. */
    QTermWidget *newTerminal();
    void closeTerminal(int index);
    /**
     * Aba sem shell (PTY livre) para o programa depurado: devolve o caminho do
     * TTY (/dev/pts/N), que o gdb usa com "set inferior-tty".
     */
    /** PTY da aba de shell para o programa depurado (o shell fica em espera). */
    QString debugTty();
    void releaseDebugTty();
    QTermWidget *current() const;
    QList<QTermWidget *> terminals() const;

    static QString areaKey(Qt::DockWidgetArea area);
    static Qt::DockWidgetArea areaFromKey(const QString &key);

Q_SIGNALS:
    void areaChangeRequested(Qt::DockWidgetArea area);
    /** Mudança feita pelo submenu Configurar: deve ir para as configurações. */
    void appearanceChangeRequested(const QString &colorScheme, int cursorShape, int historyLines, int scrollbar);
    /** "Configurações do terminal…" no menu do botão direito. */
    void settingsRequested();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QWidget *buildTitleBar();
    Qt::Edges edgesAt(const QPoint &pos) const;
    static constexpr int kGrip = 8; //< largura das bordas de redimensionar (destacado)
    QTermWidget *createTerminal();
    void updateTabBar();
    void sendWhenIdle(QTermWidget *term, const QString &line, int attempt);
    void watchForInput(QTermWidget *term);
    QTermWidget *shellTerminal();
    QTimer *m_inputWatch = nullptr;
    QPointer<QTermWidget> m_debugTerm;
    void refreshTabIcons();
    void installContextMenu(QTermWidget *term);
    void showTerminalMenu(QTermWidget *term, const QPoint &globalPos);
    QString effectiveShell() const;
    static void registerColorSchemes();

    QMainWindow *m_mainWindow;
    QWidget *m_container;
    class QTabWidget *m_tabs = nullptr;
    int m_counter = 0;
    QColor m_iconColor;
    QString m_schemeName;
    QString m_schemeKey;
    int m_nextSlot = 0;
    QFont m_font;
    QString m_shell;
    QString m_workingDir;
    bool m_cursorBlink = true;
    QString m_userScheme;  //< escolhido pelo usuário; vazio = m_schemeName (tema)
    int m_cursorShape = 0;
    int m_historyLines = 10000;
    int m_scrollbar = 2;
    QString effectiveScheme() const { return m_userScheme.isEmpty() ? m_schemeName : m_userScheme; }
    void applyAppearance(QTermWidget *term) const;
    void markTabActivity(QTermWidget *term, const QString &reason);
    int m_transparency = 0;
    QColor m_background;
    QString m_scrollbarStyle; //< QSS do scrollbar do terminal, nas cores do tema
    void applyScrollbarStyle(QTermWidget *term) const;
    void updateContainerBackground();
    qreal opacity() const;   //< transparência só vale destacado
    void applyOpacity();
    QLabel *m_titleIcon = nullptr;
    QList<QPair<QToolButton *, Icons::Glyph>> m_buttons;
};
