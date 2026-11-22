#pragma once

#include <QColor>
#include <QWidget>

class QComboBox;
class QLineEdit;
class QCheckBox;
class QLabel;
class QPushButton;
class QTextEdit;

/** Barra de busca e substituição, exibida sob as abas (estilo Kate). */
class FindBar : public QWidget
{
    Q_OBJECT
public:
    explicit FindBar(QWidget *parent = nullptr);

    void setEditor(QTextEdit *editor) { m_editor = editor; }
    void activate(bool replace);
    // Alterna a barra: fecha se já estiver visível, senão abre (como o ícone
    // da lupa no VS Code). Usada pela ação Localizar.
    void toggle(bool replace);
    /** Recolore os ícones das setas conforme o tema; `accent` destaca o aviso de
     *  "a busca deu a volta" no status. */
    void setIconColor(const QColor &color, const QColor &accent = QColor());

    void findNext();
    void findPrevious();

protected:
    void keyPressEvent(QKeyEvent *event) override;

private:
    bool find(bool backward);
    /** Mostra, destacado, que a busca passou do fim (ou início) e recomeçou. */
    void showWrapStatus(bool backward);
    void replaceOne();
    void replaceAll();
    QString pattern() const;
    void addToHistory(const QString &text);
    void loadHistory();
    void saveHistory();

    QTextEdit *m_editor = nullptr;
    QComboBox *m_find;          //< combo editável com histórico de buscas
    QLineEdit *m_replace;
    QWidget *m_replaceRow;
    QCheckBox *m_caseSensitive;
    QCheckBox *m_wholeWord;
    QLabel *m_status;
    QPushButton *m_prev;
    QPushButton *m_next;
    QColor m_iconColor;
    QColor m_accent; //< destaque do aviso de wrap (cor de acento do tema)
};
