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
    /** Recolore os ícones das setas conforme o tema. */
    void setIconColor(const QColor &color);

    void findNext();
    void findPrevious();

protected:
    void keyPressEvent(QKeyEvent *event) override;

private:
    bool find(bool backward);
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
};
