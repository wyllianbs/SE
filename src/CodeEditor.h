#pragma once

#include "Language.h"
#include "Theme.h"

#include <QPlainTextEdit>

class SyntaxHighlighter;

/** Editor de código: numeração de linhas, realce, indentação e pares automáticos. */
class CodeEditor : public QPlainTextEdit
{
    Q_OBJECT
public:
    explicit CodeEditor(QWidget *parent = nullptr);

    QString filePath() const { return m_filePath; }
    void setFilePath(const QString &path);
    QString displayName() const;

    const Language &language() const { return m_language; }
    void setLanguage(const Language &language);
    void setTheme(const Theme &theme);
    void setEditorFont(const QFont &font);
    void setAutoClosePairs(bool enabled) { m_autoClosePairs = enabled; }
    void setTabWidthSpaces(int spaces);

    bool loadFile(const QString &path, QString *error = nullptr);
    bool saveFile(const QString &path, QString *error = nullptr);

    void toggleComment();
    void goToLine(int line);

    int lineNumberAreaWidth() const;
    void lineNumberAreaPaintEvent(QPaintEvent *event);

protected:
    void resizeEvent(QResizeEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    void updateLineNumberAreaWidth();
    void updateLineNumberArea(const QRect &rect, int dy);
    void highlightCurrentLine();
    bool handleAutoPair(QKeyEvent *event);
    void insertNewlineWithIndent();

    QWidget *m_lineNumberArea;
    SyntaxHighlighter *m_highlighter;
    Language m_language;
    Theme m_theme;
    QString m_filePath;
    bool m_autoClosePairs = true;
    int m_indentSpaces = 4;
};
