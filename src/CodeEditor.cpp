#include "CodeEditor.h"
#include "Icons.h"

#include <QAbstractItemView>
#include <QCompleter>
#include <QFile>
#include <QScrollBar>
#include <QStringListModel>
#include <QFileInfo>
#include <QHelpEvent>
#include <QMouseEvent>
#include <QPointer>
#include <QToolTip>
#include <QPainter>
#include <QSaveFile>
#include <QTextBlock>
#include <QTextStream>

namespace {

class LineNumberArea : public QWidget
{
public:
    explicit LineNumberArea(CodeEditor *editor)
        : QWidget(editor), m_editor(editor) { setMouseTracking(true); }
    QSize sizeHint() const override { return {m_editor->lineNumberAreaWidth(), 0}; }

protected:
    void paintEvent(QPaintEvent *event) override { m_editor->lineNumberAreaPaintEvent(event); }
    void mousePressEvent(QMouseEvent *event) override { m_editor->lineNumberAreaClicked(event->pos()); }
    void mouseMoveEvent(QMouseEvent *event) override { m_editor->lineNumberAreaHovered(event->pos()); }
    void leaveEvent(QEvent *) override { m_editor->lineNumberAreaHovered(QPoint(-1, -1)); }

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

CodeEditor::CodeEditor(Document *document, QWidget *parent)
    : QPlainTextEdit(parent)
    , m_lineNumberArea(new LineNumberArea(this))
    , m_doc(document)
    , m_theme(Theme::all().first())
    , m_completer(new QCompleter(this))
    , m_completionModel(new QStringListModel(this))
{
    setDocument(document->text());
    connect(document, &Document::foldingChanged, this, &CodeEditor::onFoldingChanged);
    connect(document, &Document::breakpointsChanged, m_lineNumberArea, qOverload<>(&QWidget::update));
    // Recarregar do disco não tira o cursor nem a rolagem do lugar.
    connect(document, &Document::aboutToReload, this, [this]() {
        const QTextCursor c = textCursor();
        m_reloadLine = c.blockNumber();
        m_reloadColumn = c.positionInBlock();
        m_reloadScroll = verticalScrollBar()->value();
    });
    connect(document, &Document::reloaded, this, [this]() {
        const QTextBlock b = this->document()->findBlockByNumber(qMin(m_reloadLine, this->document()->blockCount() - 1));
        QTextCursor c(b);
        c.setPosition(b.position() + qMin(m_reloadColumn, b.length() - 1));
        setTextCursor(c);
        verticalScrollBar()->setValue(m_reloadScroll);
    });
    connect(document, &Document::executionLineChanged, this, [this]() {
        updateExtraSelections();
        m_lineNumberArea->update();
        const int line = m_doc->executionLine();
        if (line >= 0 && isVisible()) {
            m_doc->revealLine(line);
            QTextCursor c(this->document()->findBlockByNumber(line));
            setTextCursor(c);
            centerCursor();
        }
    });
    connect(document, &Document::diagnosticsChanged, this, [this]() {
        updateExtraSelections();
        m_lineNumberArea->update();
    });
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
    // O cursor nunca fica numa linha escondida: desdobra o bloco.
    connect(this, &QPlainTextEdit::cursorPositionChanged, this, [this]() {
        if (!textCursor().block().isVisible())
            m_doc->revealLine(textCursor().blockNumber());
    });

    updateLineNumberAreaWidth();
    highlightCurrentLine();
}

void CodeEditor::setLanguage(const Language &language)
{
    m_doc->setLanguage(language);
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
    m_doc->setTheme(theme);
    highlightCurrentLine();
    m_lineNumberArea->update();
}

void CodeEditor::setEditorFont(const QFont &font)
{
    setFont(font);
    setTabWidthSpaces(m_indentSpaces);
    updateLineNumberAreaWidth();
}

QString CodeEditor::indentUnit() const
{
    return m_insertSpaces ? QString(m_indentSpaces, QLatin1Char(' ')) : QStringLiteral("\t");
}

void CodeEditor::setTabWidthSpaces(int spaces)
{
    m_indentSpaces = spaces;
    setTabStopDistance(fontMetrics().horizontalAdvance(QLatin1Char(' ')) * spaces);
}

bool CodeEditor::loadFile(const QString &path, QString *error)
{
    return m_doc->load(path, error);
}

bool CodeEditor::saveFile(const QString &path, QString *error)
{
    return m_doc->save(path, error);
}

void CodeEditor::toggleComment()
{
    const QString marker = language().lineComment;
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

int CodeEditor::foldMarkerWidth() const
{
    return m_doc->foldingEnabled() && Folding::supports(language().id) ? fontMetrics().height() + 2 : 0;
}

int CodeEditor::lineNumberAreaWidth() const
{
    const int numbers = m_lineNumbers
        ? 12 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * qMax(3, int(QString::number(qMax(1, blockCount())).length()))
        : 0;
    const int fold = foldMarkerWidth();
    return breakpointWidth() + numbers + fold + 4;
}

int CodeEditor::breakpointWidth() const
{
    return fontMetrics().height() + 2;
}

void CodeEditor::onFoldingChanged()
{
    updateLineNumberAreaWidth();
    resizeEvent(nullptr);
    m_lineNumberArea->update();
    viewport()->update();
}

/** Prévia do ponto de parada sob o mouse, na coluna da esquerda. */
void CodeEditor::lineNumberAreaHovered(const QPoint &pos)
{
    const int line = (pos.x() >= 0 && pos.x() < breakpointWidth())
                         ? cursorForPosition(QPoint(0, pos.y())).blockNumber() : -1;
    if (line == m_hoverBreakpointLine)
        return;
    m_hoverBreakpointLine = line;
    m_lineNumberArea->setCursor(line >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
    m_lineNumberArea->setToolTip(line < 0 ? QString()
                                 : m_doc->breakpoints().contains(line) ? tr("Click to remove the breakpoint")
                                                                       : tr("Click to add a breakpoint (F9)"));
    m_lineNumberArea->update();
}

void CodeEditor::lineNumberAreaClicked(const QPoint &pos)
{
    // Coluna da esquerda: ponto de parada (como no Thonny e no Kate).
    if (pos.x() < breakpointWidth()) {
        m_doc->toggleBreakpoint(cursorForPosition(QPoint(0, pos.y())).blockNumber());
        return;
    }
    const int markerLeft = m_lineNumberArea->width() - foldMarkerWidth() - 4;
    if (foldMarkerWidth() == 0 || pos.x() < markerLeft)
        return;
    const QTextCursor c = cursorForPosition(QPoint(0, pos.y()));
    const int line = c.blockNumber();
    if (m_doc->regionAt(line))
        m_doc->toggleFold(line);
}

void CodeEditor::foldAtCursor(bool fold)
{
    const int line = textCursor().blockNumber();
    // A dobra mais interna que contém a linha do cursor.
    const FoldRegion *best = nullptr;
    for (const FoldRegion &r : m_doc->foldRegions()) {
        if (line >= r.headerLine && line <= r.lastLine && (!best || r.headerLine > best->headerLine)
            && (fold ? !m_doc->isFolded(r.headerLine) : m_doc->isFolded(r.headerLine)))
            best = &r;
    }
    if (!best)
        return;
    const int header = best->headerLine;
    m_doc->setFolded(header, fold);
    if (fold) {
        QTextCursor c(document()->findBlockByNumber(header));
        c.movePosition(QTextCursor::EndOfBlock);
        setTextCursor(c);
    }
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

static QColor severityColor(int severity)
{
    switch (severity) {
    case 1: return QColor("#f38ba8"); // erro
    case 2: return QColor("#f9e2af"); // aviso
    case 3: return QColor("#89b4fa"); // informação
    default: return QColor("#94e2d5"); // dica
    }
}

/** Linha atual + sublinhado ondulado dos diagnósticos (do menos para o mais grave). */
void CodeEditor::updateExtraSelections()
{
    QList<QTextEdit::ExtraSelection> extra;
    if (m_highlightLine) {
        QTextEdit::ExtraSelection sel;
        sel.format.setBackground(m_theme.currentLine);
        sel.format.setProperty(QTextFormat::FullWidthSelection, true);
        sel.cursor = textCursor();
        sel.cursor.clearSelection();
        extra.append(sel);
    }
    if (m_doc->executionLine() >= 0) {
        const QTextBlock b = document()->findBlockByNumber(m_doc->executionLine());
        if (b.isValid()) {
            QTextEdit::ExtraSelection sel;
            QColor c("#f9e2af");
            c.setAlpha(55);
            sel.format.setBackground(c);
            sel.format.setProperty(QTextFormat::FullWidthSelection, true);
            sel.cursor = QTextCursor(b);
            extra.append(sel);
        }
    }
    QList<Diagnostic> diags = m_doc->diagnostics();
    std::stable_sort(diags.begin(), diags.end(), [](const Diagnostic &a, const Diagnostic &b) { return a.severity > b.severity; });
    for (const Diagnostic &d : std::as_const(diags)) {
        const QTextBlock start = document()->findBlockByNumber(d.line);
        const QTextBlock end = document()->findBlockByNumber(d.endLine);
        if (!start.isValid())
            continue;
        QTextCursor c(document());
        c.setPosition(start.position() + qMin(d.column, start.length() - 1));
        int endPos = end.isValid() ? end.position() + qMin(d.endColumn, end.length() - 1) : c.position();
        if (endPos <= c.position()) {
            // Faixa vazia: sublinha a palavra (ou o caractere) na posição.
            QTextCursor w = c;
            w.select(QTextCursor::WordUnderCursor);
            endPos = w.hasSelection() ? qMax(w.selectionEnd(), c.position() + 1) : c.position() + 1;
            endPos = qMin(endPos, start.position() + start.length() - 1);
            if (endPos <= c.position() && c.position() > start.position())
                c.setPosition(c.position() - 1);
        }
        c.setPosition(endPos, QTextCursor::KeepAnchor);
        QTextEdit::ExtraSelection sel;
        sel.cursor = c;
        sel.format.setUnderlineStyle(QTextCharFormat::WaveUnderline);
        sel.format.setUnderlineColor(severityColor(d.severity));
        extra.append(sel);
    }
    setExtraSelections(extra);
}

QString CodeEditor::diagnosticTextAt(const QPoint &viewportPos) const
{
    const QTextCursor c = cursorForPosition(viewportPos);
    const int line = c.blockNumber();
    const int col = c.positionInBlock();
    QStringList lines;
    for (const Diagnostic &d : m_doc->diagnosticsAt(line)) {
        const bool inside = (line > d.line || col >= d.column - 1) && (line < d.endLine || col <= qMax(d.endColumn, d.column + 1));
        if (inside)
            lines << (d.source.isEmpty() ? d.message : QStringLiteral("%1: %2").arg(d.source, d.message)).toHtmlEscaped();
    }
    return lines.join(QStringLiteral("<br>"));
}

bool CodeEditor::viewportEvent(QEvent *event)
{
    if (event->type() == QEvent::ToolTip) {
        auto *help = static_cast<QHelpEvent *>(event);
        const QString text = diagnosticTextAt(help->pos());
        if (text.isEmpty())
            QToolTip::hideText();
        else
            QToolTip::showText(help->globalPos(), text, viewport());
        return true;
    }
    return QPlainTextEdit::viewportEvent(event);
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

    const int fold = foldMarkerWidth();
    const int markerSize = qMax(10, fontMetrics().height() - 4);
    const QPixmap open = fold ? Icons::icon(Icons::ChevronDown, m_theme.textSecondary, markerSize).pixmap(markerSize) : QPixmap();
    const QPixmap closed = fold ? Icons::icon(Icons::ChevronRight, m_theme.primary, markerSize).pixmap(markerSize) : QPixmap();
    const int numbersRight = m_lineNumberArea->width() - fold - 6;
    const int bpw = breakpointWidth();
    const int execLine = m_doc->executionLine();
    const QPixmap arrow = execLine >= 0
        ? Icons::icon(Icons::DebugArrow, QColor("#f9e2af"), markerSize + 2).pixmap(markerSize + 2) : QPixmap();
    painter.setRenderHint(QPainter::Antialiasing);
    while (block.isValid() && top <= event->rect().bottom()) {
        if (block.isVisible() && bottom >= event->rect().top()) {
            const int h = fontMetrics().height();
            const bool hasBreakpoint = m_doc->breakpoints().contains(number);
            if (hasBreakpoint || number == m_hoverBreakpointLine) {
                const int d = qMax(8, h - 6);
                QColor c("#f38ba8");
                if (!hasBreakpoint)
                    c.setAlpha(90); // prévia ao passar o mouse
                painter.setPen(Qt::NoPen);
                painter.setBrush(c);
                painter.drawEllipse(QRectF((bpw - d) / 2.0 + 1, top + (h - d) / 2.0, d, d));
            }
            if (number == execLine)
                painter.drawPixmap(1, top + (h - arrow.height() / arrow.devicePixelRatio()) / 2, arrow);
            if (m_lineNumbers) {
                const QList<Diagnostic> here = m_doc->diagnosticsAt(number);
                QColor pen = number == currentBlock ? m_theme.primary : m_theme.textSecondary;
                if (!here.isEmpty())
                    pen = severityColor(here.first().severity);
                painter.setPen(pen);
                painter.drawText(0, top, numbersRight, fontMetrics().height(), Qt::AlignRight,
                                 QString::number(number + 1));
            }
            if (fold && m_doc->regionAt(number)) {
                const bool folded = m_doc->isFolded(number);
                const int x = m_lineNumberArea->width() - fold - 2 + (fold - markerSize) / 2;
                const int y = top + (fontMetrics().height() - markerSize) / 2;
                painter.drawPixmap(x, y, folded ? closed : open);
            }
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
                       || (language().id == QLatin1String("python") && trimmed.endsWith(QLatin1Char(':')));
    const QString step = indentUnit();

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

    if (event->key() == Qt::Key_Insert && event->modifiers() == Qt::NoModifier) {
        setOverwriteMode(!overwriteMode());
        Q_EMIT overwriteModeToggled(overwriteMode());
        return;
    }

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
                QTextCursor(b).insertText(indentUnit());
                if (b == last)
                    break;
            }
            c.endEditBlock();
        } else {
            const int col = c.positionInBlock();
            c.insertText(m_insertSpaces ? QString(m_indentSpaces - col % m_indentSpaces, QLatin1Char(' '))
                                        : QStringLiteral("\t"));
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
            if (!t.isEmpty() && t.at(0) == QLatin1Char('\t'))
                n = 1;
            else
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
    if (m_completionEnabled && m_provider && !typed.isEmpty() && !manual) {
        // Só acesso a membro: "<", "/" e aspas (também anunciados pelo clangd)
        // abririam a lista em qualquer comparação ou #include.
        const QString before = textCursor().block().text().left(textCursor().positionInBlock());
        const bool member = (typed == QLatin1String(".") && !before.endsWith(QLatin1String("..")))
                            || (typed == QLatin1String(">") && before.endsWith(QLatin1String("->")))
                            || (typed == QLatin1String(":") && before.endsWith(QLatin1String("::")));
        if (member && m_triggers.contains(typed)) {
            updateCompletion(true);
            return;
        }
    }
    if (manual || (m_completionEnabled && !typed.isEmpty()
                   && (typed.at(0).isLetterOrNumber() || typed.at(0) == QLatin1Char('_')))) {
        updateCompletion();
    } else if (m_completer->popup()->isVisible() && event->key() == Qt::Key_Backspace) {
        updateCompletion();
    } else if (!typed.isEmpty()) {
        m_completer->popup()->hide();
    }
}

void CodeEditor::setCompletionProvider(CompletionProvider provider, const QStringList &triggers)
{
    m_provider = std::move(provider);
    m_triggers = triggers;
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

/**
 * Sugestões: palavras-chave e tipos da linguagem + palavras do arquivo, logo;
 * as do servidor LSP chegam depois e vão para o topo da lista.
 */
void CodeEditor::updateCompletion(bool fromTrigger)
{
    const QString prefix = wordUnderCursor();
    if (!fromTrigger && (prefix.length() < 2 || prefix.at(0).isDigit())) {
        m_completer->popup()->hide();
        return;
    }

    QStringList words;
    if (!prefix.isEmpty()) {
        QSet<QString> seen;
        auto consider = [&](const QString &w) {
            if (w.length() > prefix.length() && w.startsWith(prefix, Qt::CaseInsensitive) && !seen.contains(w)) {
                seen.insert(w);
                words << w;
            }
        };
        for (const QString &k : language().keywords) consider(k);
        for (const QString &t : language().types) consider(t);
        static const QRegularExpression wordRe(QStringLiteral("[A-Za-z_][A-Za-z0-9_]{2,}"));
        auto it = wordRe.globalMatch(toPlainText());
        int scanned = 0;
        while (it.hasNext() && scanned++ < 20000)
            consider(it.next().captured());
    }
    showCompletions(prefix, words, {});

    if (!m_provider)
        return;
    const int request = ++m_completionRequest;
    const int position = textCursor().position();
    const QTextCursor c = textCursor();
    QPointer<CodeEditor> self(this);
    m_provider(c.blockNumber(), c.positionInBlock(), [self, request, position, words](QList<ServerItem> items) {
        // Descarta respostas antigas ou de outra posição do cursor.
        if (!self || request != self->m_completionRequest || self->textCursor().position() != position)
            return;
        self->showCompletions(self->wordUnderCursor(), words, items);
    });
}

void CodeEditor::showCompletions(const QString &prefix, QStringList words, const QList<ServerItem> &server)
{
    m_insertFor.clear();
    QStringList labels;
    QSet<QString> seen;
    for (const ServerItem &item : server) {
        const QString filter = item.insert.isEmpty() ? item.label : item.insert;
        if (!prefix.isEmpty() && !filter.startsWith(prefix, Qt::CaseInsensitive)
            && !item.label.startsWith(prefix, Qt::CaseInsensitive))
            continue;
        if (seen.contains(item.label) || filter == prefix)
            continue;
        seen.insert(item.label);
        labels << item.label;
        m_insertFor.insert(item.label, filter);
    }
    // Prefixo com a mesma caixa primeiro, depois os mais curtos (o clangd põe
    // PRIb16 antes de printf, por exemplo).
    auto rank = [&prefix](const QString &a, const QString &b) {
        const bool ea = a.startsWith(prefix), eb = b.startsWith(prefix);
        if (ea != eb) return ea;
        return a.length() < b.length();
    };
    std::stable_sort(labels.begin(), labels.end(), [&](const QString &a, const QString &b) {
        return rank(m_insertFor.value(a), m_insertFor.value(b));
    });
    std::stable_sort(words.begin(), words.end(), rank);
    for (const QString &w : std::as_const(words)) {
        bool dup = seen.contains(w);
        for (auto it = m_insertFor.cbegin(); !dup && it != m_insertFor.cend(); ++it)
            dup = it.value() == w;
        if (!dup) {
            labels << w;
            seen.insert(w);
        }
    }
    if (labels.isEmpty()) {
        m_completer->popup()->hide();
        return;
    }
    if (labels.size() > 60)
        labels = labels.mid(0, 60);
    m_completionModel->setStringList(labels);
    m_completer->setCompletionPrefix(QString());
    m_completer->popup()->setCurrentIndex(m_completionModel->index(0));

    QRect rect = cursorRect();
    rect.translate(viewportMargins().left(), 0);
    rect.setWidth(qMin(520, m_completer->popup()->sizeHintForColumn(0)
                                + m_completer->popup()->verticalScrollBar()->sizeHint().width() + 24));
    m_completer->complete(rect);
}

void CodeEditor::insertCompletion(const QString &label)
{
    const QString word = m_insertFor.value(label, label);
    const QString prefix = wordUnderCursor();
    QTextCursor c = textCursor();
    c.movePosition(QTextCursor::Left, QTextCursor::KeepAnchor, prefix.length());
    c.insertText(word);
    setTextCursor(c);
}

/** Marcador "⋯" no fim das linhas cujo bloco está dobrado. */
void CodeEditor::paintEvent(QPaintEvent *event)
{
    QPlainTextEdit::paintEvent(event);
    if (!m_doc->foldingEnabled())
        return;
    QPainter p(viewport());
    p.setRenderHint(QPainter::Antialiasing);
    const QFontMetrics fm(font());
    for (QTextBlock b = firstVisibleBlock(); b.isValid(); b = b.next()) {
        const QRectF geo = blockBoundingGeometry(b).translated(contentOffset());
        if (geo.top() > viewport()->height())
            break;
        if (!b.isVisible() || !m_doc->isFolded(b.blockNumber()))
            continue;
        const QTextLine lastLine = b.layout()->lineAt(b.layout()->lineCount() - 1);
        const qreal x = geo.left() + lastLine.naturalTextWidth() + fm.horizontalAdvance(QLatin1Char(' ')) + 4;
        const qreal y = geo.top() + lastLine.y() + 2;
        const QRectF pill(x, y, fm.horizontalAdvance(QStringLiteral(" ⋯ ")) + 6, lastLine.height() - 4);
        p.setPen(Qt::NoPen);
        p.setBrush(m_theme.selection);
        p.drawRoundedRect(pill, 5, 5);
        p.setPen(m_theme.text);
        p.drawText(pill, Qt::AlignCenter, QStringLiteral("⋯"));
    }
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
