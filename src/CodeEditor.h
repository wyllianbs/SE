#pragma once

#include "Document.h"
#include "Language.h"
#include "Theme.h"

#include <QHash>
#include <QPlainTextEdit>

#include <functional>

class QCompleter;
class QStringListModel;

/** Editor de código: numeração de linhas, realce, indentação e pares automáticos. */
class CodeEditor : public QPlainTextEdit
{
    Q_OBJECT
public:
    explicit CodeEditor(Document *document, QWidget *parent = nullptr);

    Document *doc() const { return m_doc; }
    QString filePath() const { return m_doc->filePath(); }
    void setFilePath(const QString &path) { m_doc->setFilePath(path); }
    QString displayName() const { return m_doc->displayName(); }

    const Language &language() const { return m_doc->language(); }
    void setLanguage(const Language &language);
    void setTheme(const Theme &theme);
    void setEditorFont(const QFont &font);
    void setAutoClosePairs(bool enabled) { m_autoClosePairs = enabled; }
    void setTabWidthSpaces(int spaces);
    void setLineNumbersVisible(bool visible);
    void setWordWrap(bool wrap);
    void setHighlightCurrentLine(bool enabled);
    void setCodeCompletion(bool enabled) { m_completionEnabled = enabled; }
    void setInsertSpaces(bool spaces) { m_insertSpaces = spaces; }

    /** Sugestão do servidor: rótulo exibido e texto inserido. */
    struct ServerItem { QString label; QString insert; };
    using CompletionProvider = std::function<void(int line, int column, std::function<void(QList<ServerItem>)>)>;
    /** Fonte assíncrona de sugestões (LSP) e caracteres que a disparam ("." "->"…). */
    void setCompletionProvider(CompletionProvider provider, const QStringList &triggers);
    bool insertSpaces() const { return m_insertSpaces; }
    int tabWidth() const { return m_indentSpaces; }

    bool loadFile(const QString &path, QString *error = nullptr);
    bool saveFile(const QString &path, QString *error = nullptr);

    void toggleComment();
    void goToLine(int line);

    int lineNumberAreaWidth() const;
    void lineNumberAreaPaintEvent(QPaintEvent *event);
    void lineNumberAreaClicked(const QPoint &pos);
    void lineNumberAreaHovered(const QPoint &pos);

    void foldAtCursor(bool fold);

Q_SIGNALS:
    void overwriteModeToggled(bool overwrite);

protected:
    void resizeEvent(QResizeEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    bool viewportEvent(QEvent *event) override;

private:
    void updateLineNumberAreaWidth();
    void updateLineNumberArea(const QRect &rect, int dy);
    void highlightCurrentLine() { updateExtraSelections(); }
    bool handleAutoPair(QKeyEvent *event);
    void insertNewlineWithIndent();
    QString wordUnderCursor() const;
    int foldMarkerWidth() const;
    int breakpointWidth() const;
    void onFoldingChanged();
    void updateCompletion(bool fromTrigger = false);
    void showCompletions(const QString &prefix, QStringList words, const QList<ServerItem> &server);
    void updateExtraSelections();
    QString diagnosticTextAt(const QPoint &viewportPos) const;
    void insertCompletion(const QString &word);

    QWidget *m_lineNumberArea;
    Document *m_doc;
    Theme m_theme;
    bool m_autoClosePairs = true;
    int m_indentSpaces = 4;
    bool m_lineNumbers = true;
    bool m_highlightLine = true;
    bool m_completionEnabled = true;
    bool m_insertSpaces = true;
    int m_hoverBreakpointLine = -1;
    int m_reloadLine = 0;
    int m_reloadColumn = 0;
    int m_reloadScroll = 0;
    QString indentUnit() const;
    QCompleter *m_completer;
    QStringListModel *m_completionModel;
    CompletionProvider m_provider;
    QStringList m_triggers;
    QHash<QString, QString> m_insertFor; //< rótulo → texto a inserir
    int m_completionRequest = 0;
};
