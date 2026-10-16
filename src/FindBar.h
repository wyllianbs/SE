#pragma once

#include <QWidget>

class QLineEdit;
class QCheckBox;
class QLabel;
class QTextEdit;

/** Barra de busca e substituição, exibida sob as abas. */
class FindBar : public QWidget
{
    Q_OBJECT
public:
    explicit FindBar(QWidget *parent = nullptr);

    void setEditor(QTextEdit *editor) { m_editor = editor; }
    void activate(bool replace);

    void findNext();
    void findPrevious();

protected:
    void keyPressEvent(QKeyEvent *event) override;

private:
    bool find(bool backward);
    void replaceOne();
    void replaceAll();

    QTextEdit *m_editor = nullptr;
    QLineEdit *m_find;
    QLineEdit *m_replace;
    QWidget *m_replaceRow;
    QCheckBox *m_caseSensitive;
    QCheckBox *m_wholeWord;
    QLabel *m_status;
};
