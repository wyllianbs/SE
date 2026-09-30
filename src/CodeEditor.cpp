#include "CodeEditor.h"
#include "SyntaxHighlighter.h"

#include <QAbstractItemView>
#include <QCompleter>
#include <QFile>
#include <QScrollBar>
#include <QStringListModel>
#include <QFileInfo>
#include <QPainter>
#include <QSaveFile>
#include <QTextBlock>
#include <QTextStream>

namespace {

class LineNumberArea : public QWidget
{
public:
    explicit LineNumberArea(CodeEditor *editor)
        : QWidget(editor), m_editor(editor) {}
    QSize sizeHint() const override { return {m_editor->lineNumberAreaWidth(), 0}; }

protected:
    void paintEvent(QPaintEvent *event) override { m_editor->lineNumberAreaPaintEvent(event); }

private:
    CodeEditor *m_editor;
};

const QHash<QChar, QChar> &pairs()
{
    static const QHash<QChar, QChar> map{
        {'(', ')'}, {'[', ']'}, {'{', '}'}, {'"', '"'}, {'\'', '\''}, {'`', '`'}};
    return map;
}

} // namespace

CodeEditor::CodeEditor(QWidget *parent)
    : QPlainTextEdit(parent)
    , m_lineNumberArea(new LineNumberArea(this))
    , m_highlighter(new SyntaxHighlighter(document()))
    , m_language(Language::plainText())
    , m_theme(Theme::all().first())
    , m_completer(new QCompleter(this))
    , m_completionModel(new QStringListModel(this))
{
    setLineWrapMode(QPlainTextEdit::NoWrap);
    m_completer->setModel(m_completionModel);
    m_completer->setWidget(this);
    m_completer->setCompletionMode(QCompleter::PopupCompletion);
    m_completer->setCaseSensitivity(Qt::CaseInsensitive);
    m_completer->setModelSorting(QCompleter::UnsortedModel);
    m_completer->setMaxVisibleItems(10);
    connect(m_completer, qOverload<const QString &>(&QCompleter::activated), this, &CodeEditor::insertCompletion);
    setFrameShape(QFrame::NoFrame);

    connect(this, &QPlainTextEdit::blockCountChanged, this, &CodeEditor::updateLineNumberAreaWidth);
    connect(this, &QPlainTextEdit::updateRequest, this, &CodeEditor::updateLineNumberArea);
    connect(this, &QPlainTextEdit::cursorPositionChanged, this, &CodeEditor::highlightCurrentLine);

    updateLineNumberAreaWidth();
    highlightCurrentLine();
}

void CodeEditor::setFilePath(const QString &path)
{
    m_filePath = path;
}

QString CodeEditor::displayName() const
{
    return m_filePath.isEmpty() ? tr("Untitled") : QFileInfo(m_filePath).fileName();
}

void CodeEditor::setLanguage(const Language &language)
{
    m_language = language;
    m_highlighter->setLanguage(language);
}

void CodeEditor::setTheme(const Theme &theme)
{
    m_theme = theme;
    QPalette p = palette();
    p.setColor(QPalette::Base, theme.surface);
    p.setColor(QPalette::Text, theme.text);
    p.setColor(QPalette::Highlight, theme.selection);
    p.setColor(QPalette::HighlightedText, theme.text);
    setPalette(p);
    m_highlighter->setTheme(theme);
    highlightCurrentLine();
    m_lineNumberArea->update();
}

void CodeEditor::setEditorFont(const QFont &font)
{
    setFont(font);
    setTabWidthSpaces(m_indentSpaces);
    updateLineNumberAreaWidth();
}

void CodeEditor::setTabWidthSpaces(int spaces)
{
    m_indentSpaces = spaces;
    setTabStopDistance(fontMetrics().horizontalAdvance(QLatin1Char(' ')) * spaces);
}

bool CodeEditor::loadFile(const QString &path, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error)
            *error = file.errorString();
        return false;
    }
    QTextStream in(&file);
    setPlainText(in.readAll());
    document()->setModified(false);
    setFilePath(path);
    return true;
}

bool CodeEditor::saveFile(const QString &path, QString *error)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error)
            *error = file.errorString();
        return false;
    }
    QTextStream out(&file);
    out << toPlainText();
    out.flush();
    if (!file.commit()) {
        if (error)
            *error = file.errorString();
        return false;
    }
    document()->setModified(false);
    setFilePath(path);
    return true;
}

void CodeEditor::toggleComment()
{
    const QString marker = m_language.lineComment;
    if (marker.isEmpty())
        return;

    QTextCursor cursor = textCursor();
    QTextBlock first = document()->findBlock(cursor.selectionStart());
    QTextBlock last = document()->findBlock(cursor.selectionEnd());
    if (cursor.hasSelection() && cursor.selectionEnd() == last.position() && last != first)
        last = last.previous();

    // Se todas as linhas não vazias já estão comentadas, remove; senão, adiciona.
    bool allCommented = true;
    for (QTextBlock b = first; b.isValid(); b = b.next()) {
        const QString t = b.text().trimmed();
        if (!t.isEmpty() && !t.startsWith(marker))
            allCommented = false;
        if (b == last)
            break;
    }

    cursor.beginEditBlock();
    for (QTextBlock b = first; b.isValid(); b = b.next()) {
        const QString text = b.text();
        const int indent = text.length() - QString(text).remove(QRegularExpression("^\\s+")).length();
        QTextCursor c(b);
        if (allCommented) {
            if (text.mid(indent).startsWith(marker)) {
                c.setPosition(b.position() + indent);
                int len = marker.length();
                if (text.mid(indent + len, 1) == QLatin1String(" "))
                    ++len;
                c.setPosition(b.position() + indent + len, QTextCursor::KeepAnchor);
                c.removeSelectedText();
            }
        } else if (!text.trimmed().isEmpty()) {
            c.setPosition(b.position() + indent);
            c.insertText(marker + QLatin1Char(' '));
        }
        if (b == last)
            break;
    }
    cursor.endEditBlock();
}

void CodeEditor::goToLine(int line)
{
    const QTextBlock block = document()->findBlockByNumber(qMax(0, line - 1));
    if (!block.isValid())
        return;
    QTextCursor c(block);
    setTextCursor(c);
    centerCursor();
    setFocus();
}

int CodeEditor::lineNumberAreaWidth() const
{
    if (!m_lineNumbers)
        return 0;
    int digits = QString::number(qMax(1, blockCount())).length();
    return 16 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * qMax(3, digits);
}

void CodeEditor::updateLineNumberAreaWidth()
{
    setViewportMargins(lineNumberAreaWidth(), 0, 0, 0);
}

void CodeEditor::updateLineNumberArea(const QRect &rect, int dy)
{
    if (dy)
        m_lineNumberArea->scroll(0, dy);
    else
        m_lineNumberArea->update(0, rect.y(), m_lineNumberArea->width(), rect.height());
    if (rect.contains(viewport()->rect()))
        updateLineNumberAreaWidth();
}

void CodeEditor::resizeEvent(QResizeEvent *event)
{
    if (event)
        QPlainTextEdit::resizeEvent(event);
    const QRect cr = contentsRect();
    m_lineNumberArea->setGeometry(QRect(cr.left(), cr.top(), lineNumberAreaWidth(), cr.height()));
}

void CodeEditor::setLineNumbersVisible(bool visible)
{
    m_lineNumbers = visible;
    m_lineNumberArea->setVisible(visible);
    updateLineNumberAreaWidth();
    resizeEvent(nullptr);
}

void CodeEditor::setWordWrap(bool wrap)
{
    setLineWrapMode(wrap ? QPlainTextEdit::WidgetWidth : QPlainTextEdit::NoWrap);
}

void CodeEditor::setHighlightCurrentLine(bool enabled)
{
    m_highlightLine = enabled;
    highlightCurrentLine();
}

void CodeEditor::highlightCurrentLine()
{
    QList<QTextEdit::ExtraSelection> extra;
    if (!m_highlightLine) {
        setExtraSelections(extra);
        return;
    }
    QTextEdit::ExtraSelection sel;
    sel.format.setBackground(m_theme.currentLine);
    sel.format.setProperty(QTextFormat::FullWidthSelection, true);
    sel.cursor = textCursor();
    sel.cursor.clearSelection();
    extra.append(sel);
    setExtraSelections(extra);
}

void CodeEditor::lineNumberAreaPaintEvent(QPaintEvent *event)
{
    QPainter painter(m_lineNumberArea);
    painter.fillRect(event->rect(), m_theme.surface);
    painter.setFont(font());

    const int currentBlock = textCursor().blockNumber();
    QTextBlock block = firstVisibleBlock();
    int number = block.blockNumber();
    int top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
    int bottom = top + qRound(blockBoundingRect(block).height());

    while (block.isValid() && top <= event->rect().bottom()) {
        if (block.isVisible() && bottom >= event->rect().top()) {
            painter.setPen(number == currentBlock ? m_theme.primary : m_theme.textSecondary);
            painter.drawText(0, top, m_lineNumberArea->width() - 8, fontMetrics().height(),
                             Qt::AlignRight, QString::number(number + 1));
        }
        block = block.next();
        top = bottom;
        bottom = top + qRound(blockBoundingRect(block).height());
        ++number;
    }
}

void CodeEditor::insertNewlineWithIndent()
{
    QTextCursor c = textCursor();
    const QString line = c.block().text();
    const QString before = line.left(c.positionInBlock());
    QString indent;
    for (QChar ch : line) {
        if (ch != QLatin1Char(' ') && ch != QLatin1Char('\t'))
            break;
        indent += ch;
    }
    const QString trimmed = before.trimmed();
    const bool opens = trimmed.endsWith(QLatin1Char('{')) || trimmed.endsWith(QLatin1Char('['))
                       || trimmed.endsWith(QLatin1Char('('))
                       || (m_language.id == QLatin1String("python") && trimmed.endsWith(QLatin1Char(':')));
    const QString step(m_indentSpaces, QLatin1Char(' '));

    c.beginEditBlock();
    const QChar next = c.positionInBlock() < line.length() ? line.at(c.positionInBlock()) : QChar();
    if (opens && (next == QLatin1Char('}') || next == QLatin1Char(']') || next == QLatin1Char(')'))) {
        // {|} -> abre bloco com o cursor na linha do meio.
        c.insertText(QLatin1Char('\n') + indent + step);
        const int pos = c.position();
        c.insertText(QLatin1Char('\n') + indent);
        c.setPosition(pos);
    } else {
        c.insertText(QLatin1Char('\n') + indent + (opens ? step : QString()));
    }
    c.endEditBlock();
    setTextCursor(c);
}

bool CodeEditor::handleAutoPair(QKeyEvent *event)
{
    if (!m_autoClosePairs || event->text().length() != 1)
        return false;
    const QChar ch = event->text().at(0);
    QTextCursor c = textCursor();
    const QString line = c.block().text();
    const int col = c.positionInBlock();
    const QChar next = col < line.length() ? line.at(col) : QChar();

    // Digitar o fechamento que já está à direita só avança o cursor.
    if ((ch == QLatin1Char(')') || ch == QLatin1Char(']') || ch == QLatin1Char('}')
         || ch == QLatin1Char('"') || ch == QLatin1Char('\'') || ch == QLatin1Char('`'))
        && next == ch && !c.hasSelection()) {
        c.movePosition(QTextCursor::Right);
        setTextCursor(c);
        return true;
    }

    if (!pairs().contains(ch))
        return false;
    const bool quote = (ch == QLatin1Char('"') || ch == QLatin1Char('\'') || ch == QLatin1Char('`'));
    if (quote) {
        const QChar prev = col > 0 ? line.at(col - 1) : QChar();
        if (prev.isLetterOrNumber() || prev == QLatin1Char('\\'))
            return false;
    }
    if (!c.hasSelection() && next.isLetterOrNumber())
        return false;

    const QChar close = pairs().value(ch);
    c.beginEditBlock();
    if (c.hasSelection()) {
        const QString selected = c.selectedText();
        c.insertText(ch + selected + close);
    } else {
        c.insertText(QString(ch) + close);
        c.movePosition(QTextCursor::Left);
    }
    c.endEditBlock();
    setTextCursor(c);
    return true;
}

void CodeEditor::keyPressEvent(QKeyEvent *event)
{
    // Com o popup aberto, Enter/Tab aceitam e Esc fecha (o QCompleter trata).
    if (m_completer->popup()->isVisible()) {
        switch (event->key()) {
        case Qt::Key_Return:
        case Qt::Key_Enter:
        case Qt::Key_Tab:
            if (m_completer->popup()->currentIndex().isValid()) {
                insertCompletion(m_completer->popup()->currentIndex().data().toString());
                m_completer->popup()->hide();
                return;
            }
            m_completer->popup()->hide();
            break;
        case Qt::Key_Escape:
        case Qt::Key_Backtab:
            event->ignore();
            m_completer->popup()->hide();
            return;
        default:
            break;
        }
    }

    QTextCursor c = textCursor();

    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        if (event->modifiers() == Qt::NoModifier || event->modifiers() == Qt::KeypadModifier) {
            insertNewlineWithIndent();
            return;
        }
    }

    if (event->key() == Qt::Key_Tab && event->modifiers() == Qt::NoModifier) {
        if (c.hasSelection()) {
            // Indenta as linhas selecionadas.
            QTextBlock b = document()->findBlock(c.selectionStart());
            const QTextBlock last = document()->findBlock(c.selectionEnd());
            c.beginEditBlock();
            for (; b.isValid(); b = b.next()) {
                QTextCursor(b).insertText(QString(m_indentSpaces, QLatin1Char(' ')));
                if (b == last)
                    break;
            }
            c.endEditBlock();
        } else {
            const int col = c.positionInBlock();
            c.insertText(QString(m_indentSpaces - col % m_indentSpaces, QLatin1Char(' ')));
        }
        return;
    }

    if (event->key() == Qt::Key_Backtab) {
        QTextBlock b = document()->findBlock(c.selectionStart());
        const QTextBlock last = document()->findBlock(c.selectionEnd());
        c.beginEditBlock();
        for (; b.isValid(); b = b.next()) {
            const QString t = b.text();
            int n = 0;
            while (n < m_indentSpaces && n < t.length() && t.at(n) == QLatin1Char(' '))
                ++n;
            QTextCursor bc(b);
            bc.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor, n);
            bc.removeSelectedText();
            if (b == last)
                break;
        }
        c.endEditBlock();
        return;
    }

    if (event->key() == Qt::Key_Backspace && m_autoClosePairs && !c.hasSelection()) {
        const QString line = c.block().text();
        const int col = c.positionInBlock();
        if (col > 0 && col < line.length()) {
            const QChar prev = line.at(col - 1);
            if (pairs().contains(prev) && pairs().value(prev) == line.at(col)) {
                c.movePosition(QTextCursor::Left);
                c.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor, 2);
                c.removeSelectedText();
                return;
            }
        }
    }

    if (!(event->modifiers() & (Qt::ControlModifier | Qt::AltModifier)) && handleAutoPair(event))
        return;

    const bool manual = (event->modifiers() & Qt::ControlModifier) && event->key() == Qt::Key_Space;
    if (!manual)
        QPlainTextEdit::keyPressEvent(event);

    const QString typed = event->text();
    if (manual || (m_completionEnabled && !typed.isEmpty()
                   && (typed.at(0).isLetterOrNumber() || typed.at(0) == QLatin1Char('_')))) {
        updateCompletion();
    } else if (m_completer->popup()->isVisible() && event->key() == Qt::Key_Backspace) {
        updateCompletion();
    } else if (!typed.isEmpty()) {
        m_completer->popup()->hide();
    }
}

QString CodeEditor::wordUnderCursor() const
{
    const QTextCursor c = textCursor();
    const QString line = c.block().text();
    int start = c.positionInBlock();
    while (start > 0 && (line.at(start - 1).isLetterOrNumber() || line.at(start - 1) == QLatin1Char('_')))
        --start;
    return line.mid(start, c.positionInBlock() - start);
}

/** Sugestões: palavras-chave e tipos da linguagem + palavras do próprio arquivo. */
void CodeEditor::updateCompletion()
{
    const QString prefix = wordUnderCursor();
    if (prefix.length() < 2 || prefix.at(0).isDigit()) {
        m_completer->popup()->hide();
        return;
    }

    QStringList candidates;
    QSet<QString> seen;
    auto consider = [&](const QString &w) {
        if (w.length() > prefix.length() && w.startsWith(prefix, Qt::CaseInsensitive) && !seen.contains(w)) {
            seen.insert(w);
            candidates << w;
        }
    };
    for (const QString &k : m_language.keywords) consider(k);
    for (const QString &t : m_language.types) consider(t);
    static const QRegularExpression wordRe(QStringLiteral("[A-Za-z_][A-Za-z0-9_]{2,}"));
    auto it = wordRe.globalMatch(toPlainText());
    int scanned = 0;
    while (it.hasNext() && scanned++ < 20000)
        consider(it.next().captured());

    if (candidates.isEmpty()) {
        m_completer->popup()->hide();
        return;
    }
    // Prefixo com a mesma caixa primeiro, depois os mais curtos.
    std::stable_sort(candidates.begin(), candidates.end(), [&prefix](const QString &a, const QString &b) {
        const bool ea = a.startsWith(prefix), eb = b.startsWith(prefix);
        if (ea != eb) return ea;
        return a.length() < b.length();
    });
    if (candidates.size() > 50)
        candidates = candidates.mid(0, 50);
    m_completionModel->setStringList(candidates);
    m_completer->setCompletionPrefix(QString());
    m_completer->popup()->setCurrentIndex(m_completionModel->index(0));

    QRect rect = cursorRect();
    rect.translate(viewportMargins().left(), 0);
    rect.setWidth(m_completer->popup()->sizeHintForColumn(0)
                  + m_completer->popup()->verticalScrollBar()->sizeHint().width() + 24);
    m_completer->complete(rect);
}

void CodeEditor::insertCompletion(const QString &word)
{
    const QString prefix = wordUnderCursor();
    QTextCursor c = textCursor();
    c.movePosition(QTextCursor::Left, QTextCursor::KeepAnchor, prefix.length());
    c.insertText(word);
    setTextCursor(c);
}

void CodeEditor::wheelEvent(QWheelEvent *event)
{
    if (event->modifiers() & Qt::ControlModifier) {
        const int delta = event->angleDelta().y();
        if (delta > 0)
            zoomIn(1);
        else if (delta < 0)
            zoomOut(1);
        setTabWidthSpaces(m_indentSpaces);
        updateLineNumberAreaWidth();
        return;
    }
    QPlainTextEdit::wheelEvent(event);
}
