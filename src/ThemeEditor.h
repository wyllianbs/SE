#pragma once

#include "Theme.h"

#include <QDialog>

class QCheckBox;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QToolButton;
class SyntaxHighlighter;

/**
 * Editor de temas: cria temas novos (a partir de um existente) e personaliza
 * os do app, com prévia ao vivo. Trabalha sobre uma cópia da lista de temas
 * do usuário; userThemes() devolve o resultado.
 */
class ThemeEditor : public QDialog
{
    Q_OBJECT
public:
    ThemeEditor(const QList<Theme> &userThemes, const QString &currentId, const Theme &uiTheme,
                QWidget *parent = nullptr);

    const QList<Theme> &userThemes() const { return m_user; }
    const QString &selectedId() const { return m_current.id; }

private:
    void refreshList();
    void select(const QString &id);
    void loadForm();
    void storeCurrent();
    void updatePreview();
    void updateButtons();
    void pickColor(QToolButton *button, QColor *target);
    bool isModified(const Theme &t) const;

    QList<Theme> m_user;
    Theme m_current;
    QListWidget *m_list;
    QLineEdit *m_name;
    QCheckBox *m_dark;
    QList<QPair<QToolButton *, QColor Theme::*>> m_swatches;
    QList<QToolButton *> m_ansiSwatches;
    QPlainTextEdit *m_preview;
    QPlainTextEdit *m_terminalPreview;
    SyntaxHighlighter *m_highlighter;
    QPushButton *m_newButton;
    QPushButton *m_restoreButton;
    QPushButton *m_deleteButton;
};
