#include "CodeEditor.h"
#include "Icons.h"

#include <QAbstractItemView>
#include <QAbstractTextDocumentLayout>
#include <QCompleter>
#include <QFile>
#include <QMimeData>
#include <QRegularExpression>
#include <QSet>
#include <QUrl>
#include <QScrollBar>
#include <QStringListModel>
#include <QFileInfo>
#include <QHelpEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QPointer>
#include <QToolTip>
#include <QPainter>
#include <QPen>
#include <QSaveFile>
#include <QTextBlock>
#include <QTextDocument>
#include <QTimer>
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

/** Minimap: visão reduzida do documento à direita (como no Kate/VS Code). */
class MinimapArea : public QWidget
{
public:
    explicit MinimapArea(CodeEditor *editor)
        : QWidget(editor), m_editor(editor) { setCursor(Qt::PointingHandCursor); }
    QSize sizeHint() const override { return {m_editor->minimapWidth(), 0}; }

protected:
    void paintEvent(QPaintEvent *event) override { m_editor->minimapPaintEvent(event); }
    void mousePressEvent(QMouseEvent *event) override { m_editor->minimapMousePressEvent(event); }
    void mouseMoveEvent(QMouseEvent *event) override { m_editor->minimapMouseMoveEvent(event); }
    void mouseReleaseEvent(QMouseEvent *event) override { m_editor->minimapMouseReleaseEvent(event); }
    void wheelEvent(QWheelEvent *event) override { m_editor->minimapWheelEvent(event); }

private:
    CodeEditor *m_editor;
};

// Geometria do minimap.
constexpr int kMinimapLineH = 2;     //< altura de cada linha do documento no minimap
constexpr double kMinimapCharW = 1.0; //< largura de cada caractere no minimap

const QHash<QChar, QChar> &pairs()
{
    static const QHash<QChar, QChar> map{
        {'(', ')'}, {'[', ']'}, {'{', '}'}, {'"', '"'}, {'\'', '\''}, {'`', '`'}};
    return map;
}

} // namespace

CodeEditor::CodeEditor(Document *document, QWidget *parent)
    : QTextEdit(parent)
    , m_lineNumberArea(new LineNumberArea(this))
    , m_minimap(new MinimapArea(this))
    , m_doc(document)
    , m_theme(Theme::all().first())
    , m_completer(new QCompleter(this))
    , m_completionModel(new QStringListModel(this))
{
    setDocument(document->text());
    // Recuo do wrap incremental: guarda só a faixa editada desde a última aplicação.
    connect(document->text(), &QTextDocument::contentsChange, this, [this](int pos, int, int added) {
        if (m_applyingWrapIndent)
            return;
        m_wrapDirtyFrom = m_wrapDirtyFrom < 0 ? pos : qMin(m_wrapDirtyFrom, pos);
        m_wrapDirtyTo = qMax(m_wrapDirtyTo, pos + added);
    });
    // Modo desempenho: decidido pelo documento ao abrir o arquivo.
    connect(document, &Document::largeFileChanged, this, &CodeEditor::updatePerformanceState);
    QTimer::singleShot(0, this, &CodeEditor::updatePerformanceState); // nova visão de um arquivo já grande
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
            centerCursorCompat();
        }
    });
    connect(document, &Document::diagnosticsChanged, this, [this]() {
        updateExtraSelections();
        m_lineNumberArea->update();
    });
    setLineWrapMode(QTextEdit::NoWrap);
    setAcceptRichText(false); // colar insere texto puro, não HTML
    m_completer->setModel(m_completionModel);
    m_completer->setWidget(this);
    m_completer->setCompletionMode(QCompleter::PopupCompletion);
    m_completer->setCaseSensitivity(Qt::CaseInsensitive);
    m_completer->setModelSorting(QCompleter::UnsortedModel);
    m_completer->setMaxVisibleItems(10);
    connect(m_completer, qOverload<const QString &>(&QCompleter::activated), this, &CodeEditor::insertCompletion);
    setFrameShape(QFrame::NoFrame);

    // Sem o sinal updateRequest/blockCountChanged do QPlainTextEdit: a área de
    // numeração e o minimap acompanham rolagem (ambas as barras) e edições.
    connect(this->document(), &QTextDocument::blockCountChanged, this, &CodeEditor::updateLineNumberAreaWidth);
    connect(verticalScrollBar(), &QScrollBar::valueChanged, this, &CodeEditor::onEditorViewportChanged);
    connect(horizontalScrollBar(), &QScrollBar::valueChanged, this, &CodeEditor::onEditorViewportChanged);
    connect(this, &QTextEdit::textChanged, this, &CodeEditor::onEditorViewportChanged);
    connect(verticalScrollBar(), &QScrollBar::valueChanged, m_minimap, qOverload<>(&QWidget::update));
    connect(this, &QTextEdit::textChanged, m_minimap, qOverload<>(&QWidget::update));
    connect(this, &QTextEdit::cursorPositionChanged, this, &CodeEditor::highlightCurrentLine);
    connect(this, &QTextEdit::cursorPositionChanged, this, [this]() {
        if (m_prettySymbols)
            viewport()->update();
    });
    // O cursor nunca fica numa linha escondida: desdobra o bloco.
    connect(this, &QTextEdit::cursorPositionChanged, this, [this]() {
        if (!textCursor().block().isVisible())
            m_doc->revealLine(textCursor().blockNumber());
    });
    // Mover o cursor (ou editar) invalida a sugestão inline; quem digita a recria.
    connect(this, &QTextEdit::cursorPositionChanged, this, [this]() {
        if (m_ghostPos != textCursor().position())
            clearGhost();
    });
    connect(this, &QTextEdit::selectionChanged, this, [this]() {
        if (textCursor().hasSelection())
            clearGhost();
    });

    updateLineNumberAreaWidth();
    highlightCurrentLine();
}

void CodeEditor::setLanguage(const Language &language)
{
    m_doc->setLanguage(language);
    // As cores mudam sem alterar o texto: força reconstruir o cache do minimap.
    m_mmCacheRevision = -1;
    if (m_minimap)
        m_minimap->update();
}

void CodeEditor::setTheme(const Theme &theme)
{
    const bool same = m_theme.id == theme.id && m_theme.surface == theme.surface
                      && m_theme.text == theme.text && m_theme.selection == theme.selection
                      && m_theme.currentLine == theme.currentLine
                      && m_theme.textSecondary == theme.textSecondary
                      && m_theme.matchHighlight == theme.matchHighlight
                      && m_theme.occurrenceHighlight == theme.occurrenceHighlight;
    m_theme = theme;
    if (!same) {
        QPalette p = palette();
        p.setColor(QPalette::Base, theme.surface);
        p.setColor(QPalette::Text, theme.text);
        p.setColor(QPalette::Highlight, theme.selection);
        p.setColor(QPalette::HighlightedText, theme.text);
        setPalette(p);
        highlightCurrentLine();
        // O minimap guarda as cores do realce: reconstrói com o novo tema.
        m_mmCacheRevision = -1;
        m_lineNumberArea->update();
        if (m_minimap)
            m_minimap->update();
    }
    m_doc->setTheme(theme);
}

void CodeEditor::setEditorFont(const QFont &font)
{
    if (this->font() == font)
        return;
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
    // A indentação do wrap depende da largura do espaço/tab: reaplica.
    m_wrapIndentRevision = -1;
    applyWrapIndent();
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
    if (marker.isEmpty()) {
        // Sem comentário de linha (HTML, CSS, XML…): usa o de bloco <!-- --> / /* */.
        const QString bs = language().blockCommentStart;
        const QString be = language().blockCommentEnd;
        if (bs.isEmpty() || be.isEmpty())
            return;

        QTextCursor c = textCursor();
        int start, end;
        if (c.hasSelection()) {
            start = c.selectionStart();
            end = c.selectionEnd();
        } else {
            const QTextBlock b = c.block();
            const QString text = b.text();
            if (text.trimmed().isEmpty())
                return;
            const int indent = text.length() - QString(text).remove(QRegularExpression("^\\s+")).length();
            start = b.position() + indent;
            end = b.position() + text.length();
        }
        c.beginEditBlock();
        c.setPosition(start);
        c.setPosition(end, QTextCursor::KeepAnchor);
        const QString sel = c.selectedText();
        // Separa espaços externos do miolo (inclui quebras de linha como espaço).
        int l = 0;
        while (l < sel.size() && sel.at(l).isSpace())
            ++l;
        int r = sel.size();
        while (r > l && sel.at(r - 1).isSpace())
            --r;
        const QString leading = sel.left(l);
        const QString trailing = sel.mid(r);
        QString core = sel.mid(l, r - l);
        if (core.startsWith(bs) && core.endsWith(be) && core.size() >= bs.size() + be.size()) {
            QString inner = core.mid(bs.size(), core.size() - bs.size() - be.size());
            if (inner.startsWith(QLatin1Char(' ')))
                inner.remove(0, 1);
            if (inner.endsWith(QLatin1Char(' ')))
                inner.chop(1);
            c.insertText(leading + inner + trailing);
        } else {
            c.insertText(leading + bs + QLatin1Char(' ') + core + QLatin1Char(' ') + be + trailing);
        }
        c.endEditBlock();
        return;
    }

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

void CodeEditor::goToLine(int line, bool focusEditor)
{
    const QTextBlock block = document()->findBlockByNumber(qMax(0, line - 1));
    if (!block.isValid())
        return;
    QTextCursor c(block);
    setTextCursor(c);
    centerCursorCompat();
    // Na navegação ao vivo (barra "ir para linha") NÃO roubamos o foco, senão
    // as próximas teclas digitadas vão para o editor em vez do campo.
    if (focusEditor)
        setFocus();
}

int CodeEditor::foldMarkerWidth() const
{
    return m_doc->foldingEnabled() && Folding::supports(language().id) ? fontMetrics().height() + 2 : 0;
}

int CodeEditor::lineNumberAreaWidth() const
{
    const int numbers = m_lineNumbers
        ? 12 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * qMax(3, int(QString::number(qMax(1, document()->blockCount())).length()))
        : 0;
    const int fold = foldMarkerWidth();
    return breakpointWidth() + numbers + fold + 4;
}

int CodeEditor::breakpointWidth() const
{
    // Sem depurador para a linguagem (HTML, CSS…): sem coluna de pontos de parada.
    if (!Language::debuggable(language().id))
        return 0;
    return fontMetrics().height() + 2;
}

void CodeEditor::onFoldingChanged()
{
    // Cursor dentro de um bloco que acabou de dobrar: vai para o cabeçalho
    // (senão a próxima tecla desdobraria o bloco de novo).
    QTextBlock b = textCursor().block();
    if (!b.isVisible()) {
        while (b.isValid() && !b.isVisible())
            b = b.previous();
        if (b.isValid()) {
            QTextCursor c(b);
            c.movePosition(QTextCursor::EndOfBlock);
            setTextCursor(c);
        }
    }
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

void CodeEditor::updateLineNumberAreaWidth()
{
    updateViewportMargins();
}

void CodeEditor::updateViewportMargins()
{
    setViewportMargins(lineNumberAreaWidth(), 0, minimapWidth(), 0);
}

void CodeEditor::updateLineNumberArea(const QRect &, int)
{
    m_lineNumberArea->update();
}

// --- Compatibilidade com a API de blocos do QPlainTextEdit (via layout) ---

QPointF CodeEditor::contentOffset() const
{
    return QPointF(-horizontalScrollBar()->value(), -verticalScrollBar()->value());
}

QRectF CodeEditor::blockBoundingGeometry(const QTextBlock &block) const
{
    return document()->documentLayout()->blockBoundingRect(block);
}

QRectF CodeEditor::blockBoundingRect(const QTextBlock &block) const
{
    return document()->documentLayout()->blockBoundingRect(block);
}

QTextBlock CodeEditor::firstVisibleBlock() const
{
    // Bloco no topo da área visível (pula blocos dobrados/invisíveis).
    return cursorForPosition(QPoint(0, 0)).block();
}

void CodeEditor::centerCursorCompat()
{
    ensureCursorVisible();
    const QRectF r = cursorRect();
    const int delta = int(r.center().y() - viewport()->height() / 2.0);
    QScrollBar *bar = verticalScrollBar();
    bar->setValue(bar->value() + delta);
}

void CodeEditor::onEditorViewportChanged()
{
    m_lineNumberArea->update();
    applyWrapIndent();
}

/**
 * Indentação das linhas quebradas (wrap), como no VSCode: cada bloco recebe uma
 * margem esquerda igual à sua indentação, e um recuo de primeira linha negativo
 * que a cancela — assim a 1ª linha começa na margem e as continuações alinham
 * sob o início do conteúdo. (O QPlainTextEdit ignorava isso; o QTextEdit honra.)
 */
void CodeEditor::applyWrapIndent()
{
    if (m_applyingWrapIndent)
        return;
    // Wrap desligado e nada aplicado: nada a fazer (caso comum, custo zero).
    if (!m_wordWrap && !m_wrapIndentsApplied)
        return;
    if (document()->revision() == m_wrapIndentRevision)
        return; // nada mudou desde a última aplicação (ex.: só rolagem)

    // O recuo das linhas quebradas (margem esquerda + textIndent negativo) é
    // OPCIONAL e desligado por padrão: num documento com wrap, ao desenhar uma
    // seleção sobre esses blocos houve segfault no QTextDocumentLayout::draw do
    // QTextEdit (QTextLine::textStart) — observado só no Wayland (core dump).
    // Mitigação: arredondar o recuo para pixel inteiro (evita coordenadas
    // fracionárias que o backend pode tratar mal). Fica atrás de uma opção.
    const qreal spaceW = fontMetrics().horizontalAdvance(QLatin1Char(' '));
    const int tabW = qMax(1, m_indentSpaces);

    m_applyingWrapIndent = true;
    const bool wasModified = document()->isModified();
    QTextCursor cur(document());
    // joinPreviousEditBlock: funde o recuo na mesma ação de desfazer da edição
    // do usuário (um só Ctrl+Z), em vez de criar um passo separado.
    cur.joinPreviousEditBlock();
    // Só os blocos editados desde a última vez; tudo quando a configuração
    // mudou (revisão -1) — antes, cada tecla percorria o documento inteiro.
    QTextBlock b = document()->firstBlock();
    QTextBlock stop;
    if (m_wrapIndentRevision >= 0 && m_wrapDirtyFrom >= 0) {
        b = document()->findBlock(m_wrapDirtyFrom);
        stop = document()->findBlock(m_wrapDirtyTo).next();
    }
    m_wrapDirtyFrom = -1;
    m_wrapDirtyTo = -1;
    for (; b.isValid() && b != stop; b = b.next()) {
        qreal indentPx = 0;
        if (m_wrapIndentEnabled && m_wordWrap) {
            const QString t = b.text();
            int cols = 0, i = 0;
            for (; i < t.size(); ++i) {
                const QChar ch = t.at(i);
                if (ch == QLatin1Char('\t'))
                    cols += tabW - (cols % tabW);
                else if (ch == QLatin1Char(' '))
                    ++cols;
                else
                    break;
            }
            // Indenta só linhas com conteúdo. Por padrão (0 = modo VSCode) a
            // continuação alinha sob o 1º caractere da linha; com recuo extra,
            // soma esses espaços para destacar a dobra.
            if (i < t.size())
                indentPx = qRound((cols + m_wrapIndentExtra) * spaceW); // pixel inteiro
        }
        QTextBlockFormat fmt = b.blockFormat();
        if (!qFuzzyCompare(fmt.leftMargin(), indentPx) || !qFuzzyCompare(fmt.textIndent(), -indentPx)) {
            fmt.setLeftMargin(indentPx);
            fmt.setTextIndent(-indentPx);
            cur.setPosition(b.position());
            cur.setBlockFormat(fmt);
        }
    }
    cur.endEditBlock();
    document()->setModified(wasModified);
    m_wrapIndentsApplied = m_wordWrap;
    m_wrapIndentRevision = document()->revision();
    m_applyingWrapIndent = false;
}

void CodeEditor::resizeEvent(QResizeEvent *event)
{
    // Com quebra de linha, redimensionar reflui o texto e muda a altura total;
    // o QTextEdit preserva a rolagem em pixels, o que tira a linha do cursor de
    // vista. Como no Kate, se o cursor estava visível antes, mantém-no visível.
    const bool keepCursor = event && viewport()->rect().intersects(cursorRect());
    if (event)
        QTextEdit::resizeEvent(event);
    const QRect cr = contentsRect();
    m_lineNumberArea->setGeometry(QRect(cr.left(), cr.top(), lineNumberAreaWidth(), cr.height()));
    if (m_minimapVisible)
        m_minimap->setGeometry(QRect(cr.right() - minimapWidth() + 1, cr.top(), minimapWidth(), cr.height()));
    if (keepCursor)
        ensureCursorVisible();
}

void CodeEditor::setLineNumbersVisible(bool visible)
{
    if (m_lineNumbers == visible)
        return;
    m_lineNumbers = visible;
    m_lineNumberArea->setVisible(visible);
    updateLineNumberAreaWidth();
    resizeEvent(nullptr);
}

void CodeEditor::setWordWrap(bool wrap)
{
    if (m_wordWrap == wrap && lineWrapMode() == (wrap ? QTextEdit::WidgetWidth : QTextEdit::NoWrap))
        return;
    m_wordWrap = wrap;
    setLineWrapMode(wrap ? QTextEdit::WidgetWidth : QTextEdit::NoWrap);
    m_wrapIndentRevision = -1; // força reaplicar o recuo suspenso
    applyWrapIndent();
}

void CodeEditor::setWrapIndentEnabled(bool on)
{
    m_wrapIndentWanted = on;
    applyWrapIndentEnabled();
}

void CodeEditor::applyWrapIndentEnabled()
{
    const bool on = m_wrapIndentWanted && !m_perfActive;
    if (m_wrapIndentEnabled == on)
        return;
    m_wrapIndentEnabled = on;
    m_wrapIndentRevision = -1; // liga/desliga: reaplica (ou limpa)
    applyWrapIndent();
}

void CodeEditor::setWrapIndentExtra(int spaces)
{
    spaces = qBound(0, spaces, 32);
    if (m_wrapIndentExtra == spaces)
        return;
    m_wrapIndentExtra = spaces;
    m_wrapIndentRevision = -1; // o recuo mudou: reaplica
    applyWrapIndent();
}

int CodeEditor::minimapWidth() const
{
    return m_minimapVisible ? m_minimapWidth : 0;
}

void CodeEditor::setMinimapWidth(int width)
{
    width = qBound(20, width, 400);
    if (m_minimapWidth == width)
        return;
    m_minimapWidth = width;
    updateViewportMargins();
    resizeEvent(nullptr);
    m_minimap->update();
}

void CodeEditor::setMinimapVisible(bool visible)
{
    m_minimapWanted = visible;
    applyMinimapVisible();
}

void CodeEditor::applyMinimapVisible()
{
    const bool visible = m_minimapWanted && !m_perfActive;
    if (m_minimapVisible == visible)
        return;
    m_minimapVisible = visible;
    m_minimap->setVisible(visible);
    updateViewportMargins();
    resizeEvent(nullptr);
    m_minimap->update();
}

void CodeEditor::updatePerformanceState()
{
    const bool active = m_doc->isLargeFile();
    if (m_perfActive == active)
        return;
    m_perfActive = active;
    applyMinimapVisible();
    applyWrapIndentEnabled();
    updateExtraSelections(); // some/volta o destaque de ocorrências
    viewport()->update();    // guias de indentação
    Q_EMIT performanceActiveChanged(active);
}

/**
 * Geometria do minimap num modelo único (como o VSCode). O indicador (slider)
 * ocupa `sliderHeight` px e desliza de 0 a `maxSliderTop` em proporção direta
 * à fração rolada; quando o documento é maior que o minimap, o próprio minimap
 * rola para manter o indicador visível. Pintura e arraste usam estes valores,
 * então o indicador fica sempre sob o cursor.
 */
CodeEditor::MinimapMetrics CodeEditor::minimapMetrics() const
{
    MinimapMetrics m;
    m.h = qMax(0, m_minimap->height());
    m.lineCount = qMax(1, document()->blockCount());

    const QTextBlock fvb = firstVisibleBlock();
    const double blockH = blockBoundingRect(fvb).height();
    const int lineSpacing = blockH > 0 ? int(blockH) : qMax(1, fontMetrics().lineSpacing());
    m.visibleLines = qMax(1, viewport()->height() / lineSpacing);

    const QScrollBar *bar = verticalScrollBar();
    m.scrollMin = bar->minimum();
    m.scrollRange = bar->maximum() - bar->minimum();
    const double frac = m.scrollRange > 0
        ? qBound(0.0, double(bar->value() - m.scrollMin) / m.scrollRange, 1.0)
        : 0.0;

    // Altura do indicador = linhas visíveis; limitada à altura do minimap.
    m.sliderHeight = qBound(kMinimapLineH, m.visibleLines * kMinimapLineH, qMax(kMinimapLineH, m.h));
    // Curso do indicador: até o documento inteiro caber, ou até o fim do minimap.
    const int docLinesH = m.lineCount * kMinimapLineH;
    m.maxSliderTop = qMax(0, qMin(docLinesH, m.h) - m.sliderHeight);
    m.sliderTop = int(qRound(frac * m.maxSliderTop));
    m.firstVisibleLine = qMax(0, int(qRound(frac * (m.lineCount - m.visibleLines))));
    return m;
}

/** Primeira linha do documento mostrada no topo do minimap. */
int CodeEditor::minimapFirstLine() const
{
    const MinimapMetrics m = minimapMetrics();
    const int capacity = m.h / kMinimapLineH;            //< linhas que o minimap exibe
    const int maxFirst = qMax(0, m.lineCount - capacity);
    // O minimap rola o bastante para que a linha visível fique em `sliderTop`.
    return qBound(0, m.firstVisibleLine - m.sliderTop / kMinimapLineH, maxFirst);
}

void CodeEditor::minimapPaintEvent(QPaintEvent *)
{
    const QSize sz = m_minimap->size();
    const int w = sz.width();
    const int h = sz.height();
    const int first = minimapFirstLine();

    // Reconstrói a fatia renderizada só quando algo que a afeta muda.
    if (m_minimapCache.size() != sz || m_mmCacheFirstLine != first
        || m_mmCacheRevision != document()->revision() || m_mmCacheThemeId != m_theme.id) {
        m_minimapCache = QPixmap(sz);
        m_minimapCache.fill(m_theme.surface);
        QPainter cp(&m_minimapCache);
        const int rows = h / kMinimapLineH + 1;
        for (int row = 0; row < rows; ++row) {
            const QTextBlock block = document()->findBlockByNumber(first + row);
            if (!block.isValid())
                break;
            if (!block.isVisible())
                continue;
            const int y = row * kMinimapLineH;
            const QString text = block.text();
            const QList<QTextLayout::FormatRange> fmts =
                block.layout() ? block.layout()->formats() : QList<QTextLayout::FormatRange>();
            const int n = text.size();
            int i = 0;
            while (i < n) {
                if (text.at(i).isSpace()) { ++i; continue; }
                const int start = i;
                while (i < n && !text.at(i).isSpace())
                    ++i;
                QColor col = m_theme.text;
                for (const QTextLayout::FormatRange &fr : fmts)
                    if (start >= fr.start && start < fr.start + fr.length
                        && fr.format.foreground().style() != Qt::NoBrush) {
                        col = fr.format.foreground().color();
                        break;
                    }
                col.setAlpha(205);
                const int x = int(start * kMinimapCharW);
                if (x >= w)
                    break;
                cp.fillRect(x, y, qMin(int((i - start) * kMinimapCharW), w - x), kMinimapLineH, col);
            }
        }
        m_mmCacheFirstLine = first;
        m_mmCacheRevision = document()->revision();
        m_mmCacheThemeId = m_theme.id;
    }

    QPainter p(m_minimap);
    p.drawPixmap(0, 0, m_minimapCache);
    // Divisória à esquerda.
    p.setPen(m_theme.divider);
    p.drawLine(0, 0, 0, h);

    // Indicador da área visível (clicável/arrastável): mesma geometria usada
    // pelo arraste, então o indicador acompanha o cursor sem descompasso.
    const MinimapMetrics mm = minimapMetrics();
    const int indY = mm.sliderTop;
    const int indH = mm.sliderHeight;
    QColor fill = m_theme.primary;
    fill.setAlpha(40);
    p.fillRect(QRect(0, indY, w, indH), fill);
    QColor edge = m_theme.primary;
    edge.setAlpha(130);
    p.setPen(edge);
    p.drawRect(QRect(0, indY, w - 1, indH - 1));
}

/**
 * Rola o editor para que o topo do indicador fique em `sliderTop` (px no
 * minimap). É a relação direta usada na pintura: sliderTop = fração * curso.
 */
void CodeEditor::minimapScrollToSliderTop(int sliderTop, const MinimapMetrics &m)
{
    if (m.maxSliderTop <= 0)
        return;
    const double frac = qBound(0.0, double(sliderTop) / m.maxSliderTop, 1.0);
    QScrollBar *bar = verticalScrollBar();
    bar->setValue(qRound(m.scrollMin + frac * m.scrollRange));
}

void CodeEditor::minimapMousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton)
        return;
    const MinimapMetrics m = minimapMetrics();
    const int y = event->position().toPoint().y();
    // Clique fora do indicador: traz o centro do indicador até o cursor
    // (como o VSCode fora do modo proporcional); em seguida o arraste é
    // relativo, com o indicador seguindo o mouse 1:1.
    if (y < m.sliderTop || y >= m.sliderTop + m.sliderHeight)
        minimapScrollToSliderTop(y - m.sliderHeight / 2, m);
    m_minimapDragging = true;
    m_mmDragStartY = y;
    m_mmDragStartScroll = verticalScrollBar()->value();
}

void CodeEditor::minimapMouseMoveEvent(QMouseEvent *event)
{
    if (!m_minimapDragging)
        return;
    const MinimapMetrics m = minimapMetrics();
    if (m.maxSliderTop <= 0)
        return;
    // Arraste relativo: cada pixel do mouse move o indicador um pixel, logo a
    // rolagem anda `delta * cursoDaRolagem / cursoDoIndicador`.
    const int delta = event->position().toPoint().y() - m_mmDragStartY;
    const double target = double(m_mmDragStartScroll)
        + double(delta) * m.scrollRange / m.maxSliderTop;
    QScrollBar *bar = verticalScrollBar();
    bar->setValue(qBound(bar->minimum(), int(qRound(target)), bar->maximum()));
}

void CodeEditor::minimapMouseReleaseEvent(QMouseEvent *)
{
    m_minimapDragging = false;
}

void CodeEditor::minimapWheelEvent(QWheelEvent *event)
{
    // Rola o editor (3 linhas por entalhe), como se a roda fosse sobre ele.
    QScrollBar *bar = verticalScrollBar();
    const int steps = event->angleDelta().y() / 120;
    bar->setValue(bar->value() - steps * 3 * qMax(1, bar->singleStep()));
    event->accept();
}

void CodeEditor::setHighlightCurrentLine(bool enabled)
{
    if (m_highlightLine == enabled)
        return;
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
/**
 * Com um trecho selecionado numa só linha (um caractere, uma palavra…),
 * destaca as demais ocorrências no documento. As posições ficam em cache
 * por (texto buscado, revisão do documento): mover o cursor não refaz a busca.
 */
/** Texto do documento, copiado só quando a revisão muda (cursor/seleção não copiam de novo). */
const QString &CodeEditor::plainTextCached() const
{
    const int revision = document()->revision();
    if (revision != m_plainRevision) {
        m_plainText = document()->toPlainText();
        m_plainRevision = revision;
    }
    return m_plainText;
}

void CodeEditor::appendSelectionOccurrences(QList<QTextEdit::ExtraSelection> &extra)
{
    const QTextCursor cur = textCursor();
    QString needle;
    if (cur.hasSelection()) {
        needle = cur.selectedText();
        // Multilinha (U+2029), longo demais ou só espaços: não destaca.
        if (needle.size() > 200 || needle.contains(QChar::ParagraphSeparator) || needle.trimmed().isEmpty())
            needle.clear();
    }
    if (needle.isEmpty() || m_perfActive) {
        m_occNeedle.clear();
        m_occPositions.clear();
        return;
    }
    const int revision = document()->revision();
    if (needle != m_occNeedle || revision != m_occRevision) {
        m_occNeedle = needle;
        m_occRevision = revision;
        m_occPositions.clear();
        const QString text = plainTextCached();
        constexpr int maxMatches = 2000; // limite: arquivos enormes não travam
        for (int pos = text.indexOf(needle); pos >= 0 && m_occPositions.size() < maxMatches;
             pos = text.indexOf(needle, pos + needle.size()))
            m_occPositions.append(pos);
    }
    if (m_occPositions.size() < 2)
        return; // só a própria seleção
    const QColor bg = m_theme.occurrenceHighlight;
    const int selStart = cur.selectionStart();
    for (int pos : std::as_const(m_occPositions)) {
        if (pos == selStart)
            continue;
        QTextEdit::ExtraSelection sel;
        sel.cursor = QTextCursor(document());
        sel.cursor.setPosition(pos);
        sel.cursor.setPosition(pos + needle.size(), QTextCursor::KeepAnchor);
        sel.format.setBackground(bg);
        extra.append(sel);
        m_occRanges.append({pos, pos + int(needle.size())});
    }
}

void CodeEditor::updateExtraSelections()
{
    QList<QTextEdit::ExtraSelection> extra;
    m_matchRanges.clear();
    m_occRanges.clear();
    if (m_highlightLine) {
        QTextEdit::ExtraSelection sel;
        sel.format.setBackground(m_theme.currentLine);
        sel.format.setProperty(QTextFormat::FullWidthSelection, true);
        sel.cursor = textCursor();
        sel.cursor.clearSelection();
        extra.append(sel);
    }
    appendSelectionOccurrences(extra);
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

    // Realce dos delimitadores de bloco: com o cursor encostado em ( [ { ou
    // ) ] }, destaca ele e o par correspondente (respeitando aninhamento).
    {
        auto kind = [](QChar c) -> int { // +1 abre, -1 fecha, 0 nenhum
            if (c == QLatin1Char('(') || c == QLatin1Char('[') || c == QLatin1Char('{')) return 1;
            if (c == QLatin1Char(')') || c == QLatin1Char(']') || c == QLatin1Char('}')) return -1;
            return 0;
        };
        auto partner = [](QChar c) -> QChar {
            switch (c.unicode()) {
            case '(': return QLatin1Char(')'); case ')': return QLatin1Char('(');
            case '[': return QLatin1Char(']'); case ']': return QLatin1Char('[');
            case '{': return QLatin1Char('}'); case '}': return QLatin1Char('{');
            }
            return QChar();
        };
        QTextDocument *d = document();
        const int cur = textCursor().position();
        int bpos = -1;
        QChar bch;
        for (int cand : {cur, cur - 1}) { // caractere à direita do cursor, senão à esquerda
            if (cand < 0)
                continue;
            const QChar c = d->characterAt(cand);
            if (kind(c) != 0) { bpos = cand; bch = c; break; }
        }
        if (bpos >= 0) {
            const int dir = kind(bch);
            const QChar want = partner(bch);
            const int count = d->characterCount();
            int depth = 0, mpos = -1;
            for (int p = bpos; p >= 0 && p < count; p += dir) {
                const QChar c = d->characterAt(p);
                if (c == bch)
                    ++depth;
                else if (c == want && --depth == 0) { mpos = p; break; }
            }
            if (mpos >= 0) {
                const QColor bg = m_theme.matchHighlight;
                for (int p : {bpos, mpos}) {
                    QTextEdit::ExtraSelection sel;
                    QTextCursor c(d);
                    c.setPosition(p);
                    c.setPosition(p + 1, QTextCursor::KeepAnchor);
                    sel.cursor = c;
                    sel.format.setBackground(bg);
                    sel.format.setForeground(m_theme.text);
                    sel.format.setFontWeight(QFont::Bold);
                    extra.append(sel);
                    m_matchRanges.append({p, p + 1});
                }
            }
        }
    }

    // Realce do par de tags em HTML/XML: com o cursor dentro de <div …> (ou de
    // </div>), destaca o nome desta tag e o da tag correspondente (aninhamento).
    if (language().id == QLatin1String("html") || language().id == QLatin1String("xml")) {
        struct TagInfo { bool valid = false; bool close = false; bool self = false;
                         QString name; int nameStart = 0; int nameLen = 0; };
        const QString text = plainTextCached();
        const int len = text.size();
        auto parseTag = [&](int i, int j) -> TagInfo {
            TagInfo t;
            if (i < 0 || j <= i)
                return t;
            const QString in = text.mid(i + 1, j - i - 1);
            if (in.isEmpty() || in.startsWith(QLatin1Char('!')) || in.startsWith(QLatin1Char('?')))
                return t;
            int p = 0;
            t.close = in.startsWith(QLatin1Char('/'));
            if (t.close)
                p = 1;
            while (p < in.size() && in.at(p).isSpace())
                ++p;
            const int ns = p;
            if (ns >= in.size() || !in.at(ns).isLetter())
                return t;
            while (p < in.size() && (in.at(p).isLetterOrNumber() || in.at(p) == QLatin1Char('_')
                                     || in.at(p) == QLatin1Char(':') || in.at(p) == QLatin1Char('-')))
                ++p;
            t.name = in.mid(ns, p - ns);
            t.self = in.endsWith(QLatin1Char('/'));
            t.nameStart = i + 1 + ns;
            t.nameLen = p - ns;
            t.valid = true;
            return t;
        };
        const int pos = qBound(0, textCursor().position(), len);
        const int lt = text.lastIndexOf(QLatin1Char('<'), qMin(pos, len - 1));
        const int gt = lt >= 0 ? text.indexOf(QLatin1Char('>'), lt) : -1;
        if (lt >= 0 && gt >= 0 && pos >= lt && pos <= gt + 1) {
            const TagInfo cur = parseTag(lt, gt);
            if (cur.valid && !cur.self) {
                const Qt::CaseSensitivity cs = language().id == QLatin1String("html")
                                                   ? Qt::CaseInsensitive : Qt::CaseSensitive;
                auto same = [&](const QString &a, const QString &b) { return a.compare(b, cs) == 0; };
                TagInfo match;
                if (!cur.close) { // abertura → procura o fechamento adiante
                    int depth = 1;
                    for (int i = gt + 1; (i = text.indexOf(QLatin1Char('<'), i)) >= 0; ) {
                        const int j = text.indexOf(QLatin1Char('>'), i);
                        if (j < 0)
                            break;
                        const TagInfo t = parseTag(i, j);
                        if (t.valid && !t.self && same(t.name, cur.name)) {
                            if (t.close) { if (--depth == 0) { match = t; break; } }
                            else ++depth;
                        }
                        i = j + 1;
                    }
                } else { // fechamento → procura a abertura para trás
                    int depth = 1;
                    // Importante: só chamar lastIndexOf com índice >= 0. Com índice
                    // negativo o Qt procura a partir do FIM da string, o que faria
                    // o laço reiniciar no fim e nunca terminar.
                    for (int i = lt; i > 0; ) {
                        i = text.lastIndexOf(QLatin1Char('<'), i - 1);
                        if (i < 0)
                            break;
                        const int j = text.indexOf(QLatin1Char('>'), i);
                        if (j >= 0 && j < lt) {
                            const TagInfo t = parseTag(i, j);
                            if (t.valid && !t.self && same(t.name, cur.name)) {
                                if (!t.close) { if (--depth == 0) { match = t; break; } }
                                else ++depth;
                            }
                        }
                    }
                }
                if (match.valid) {
                    const QColor bg = m_theme.matchHighlight;
                    const TagInfo &matchRef = match;
                    for (const TagInfo *t : {&cur, &matchRef}) {
                        QTextEdit::ExtraSelection sel;
                        QTextCursor c(document());
                        c.setPosition(t->nameStart);
                        c.setPosition(t->nameStart + t->nameLen, QTextCursor::KeepAnchor);
                        sel.cursor = c;
                        sel.format.setBackground(bg);
                        sel.format.setForeground(m_theme.text);
                        sel.format.setFontWeight(QFont::Bold);
                        extra.append(sel);
                        m_matchRanges.append({t->nameStart, t->nameStart + t->nameLen});
                    }
                }
            }
        }
    }

    setExtraSelections(extra);
}

/**
 * Desenha um contorno arredondado sobre cada faixa destacada (par de bloco/tag
 * e ocorrências). O preenchimento já vem das ExtraSelections; o contorno, por
 * cima, deixa o destaque nítido mesmo em temas de baixo contraste (estilo VS
 * Code). Só faixas numa única linha (todas as nossas são) e visíveis.
 */
void CodeEditor::paintHighlightOutlines(QPainter &p)
{
    if (m_matchRanges.isEmpty() && m_occRanges.isEmpty())
        return;
    const QRect vp = viewport()->rect();
    p.save();
    p.setBrush(Qt::NoBrush);
    auto drawRanges = [&](const QList<QPair<int, int>> &ranges, const QColor &base) {
        QColor border = base;
        border.setAlpha(qBound(180, base.alpha() + 110, 255));
        p.setPen(QPen(border, 1.0));
        for (const auto &r : ranges) {
            QTextCursor a(document());
            a.setPosition(r.first);
            QTextCursor b(document());
            b.setPosition(r.second);
            const QRect ra = cursorRect(a);
            const QRect rb = cursorRect(b);
            if (ra.top() != rb.top() || rb.left() <= ra.left())
                continue; // só faixas numa única linha
            QRectF box(ra.left() - 0.5, ra.top() + 0.5, rb.left() - ra.left(), ra.height() - 1.5);
            if (!vp.intersects(box.toRect().adjusted(-1, -1, 1, 1)))
                continue;
            p.drawRoundedRect(box, 3, 3);
        }
    };
    drawRanges(m_occRanges, m_theme.occurrenceHighlight);
    drawRanges(m_matchRanges, m_theme.matchHighlight);
    p.restore();
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
        const QString diag = diagnosticTextAt(help->pos());
        if (!diag.isEmpty()) {
            QToolTip::showText(help->globalPos(), diag, viewport());
            return true;
        }
        // Sem diagnóstico aqui: documentação do servidor (hover), se houver e se
        // o cursor estiver sobre um identificador (não no espaço em branco).
        const QTextCursor c = cursorForPosition(help->pos());
        const QString blockText = c.block().text();
        const int col = c.positionInBlock();
        const bool onWord = (col < blockText.size()
                             && (blockText.at(col).isLetterOrNumber() || blockText.at(col) == QLatin1Char('_')))
                            || (col > 0
                                && (blockText.at(col - 1).isLetterOrNumber() || blockText.at(col - 1) == QLatin1Char('_')));
        if (!m_hoverProvider || !onWord) {
            QToolTip::hideText();
            return true;
        }
        const QPoint gp = help->globalPos();
        QPointer<CodeEditor> self(this);
        m_hoverProvider(c.blockNumber(), col, [self, gp](QString text) {
            if (!self)
                return;
            if (text.isEmpty()) {
                QToolTip::hideText();
                return;
            }
            // O texto vem em Markdown (rust-analyzer, clangd…); renderiza em HTML
            // para a dica, limitando o tamanho para não cobrir a tela.
            if (text.size() > 1500)
                text = text.left(1500) + QStringLiteral("…");
            QTextDocument md;
            md.setMarkdown(text);
            QToolTip::showText(gp, md.toHtml(), self->viewport());
        });
        return true;
    }
    return QTextEdit::viewportEvent(event);
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

/** Linha (já aparada) termina com a palavra w, respeitando limites de palavra. */
static bool endsWithWord(const QString &t, const QString &w)
{
    if (!t.endsWith(w))
        return false;
    const int i = t.size() - w.size() - 1;
    return i < 0 || !(t.at(i).isLetterOrNumber() || t.at(i) == QLatin1Char('_'));
}

static bool startsWithWord(const QString &t, const QString &w)
{
    if (!t.startsWith(w))
        return false;
    const int i = w.size();
    return i >= t.size() || !(t.at(i).isLetterOrNumber() || t.at(i) == QLatin1Char('_'));
}

bool CodeEditor::opensBlock(const QString &id, const QString &before)
{
    const QString t = before.trimmed();
    if (t.isEmpty())
        return false;
    // Delimitadores abertos: valem para quase toda linguagem com chaves/colchetes.
    if (t.endsWith(QLatin1Char('{')) || t.endsWith(QLatin1Char('[')) || t.endsWith(QLatin1Char('(')))
        return true;
    if (id == QLatin1String("python"))
        return t.endsWith(QLatin1Char(':'));
    if (id == QLatin1String("ruby")) {
        if (endsWithWord(t, QStringLiteral("end")))
            return false;
        if (endsWithWord(t, QStringLiteral("do")) || t.endsWith(QLatin1Char('|')))
            return true;
        for (const QLatin1String k : {QLatin1String("def"), QLatin1String("class"), QLatin1String("module"),
                                      QLatin1String("if"), QLatin1String("unless"), QLatin1String("while"),
                                      QLatin1String("until"), QLatin1String("for"), QLatin1String("begin"),
                                      QLatin1String("case"), QLatin1String("else"), QLatin1String("elsif"),
                                      QLatin1String("when"), QLatin1String("ensure"), QLatin1String("rescue")}) {
            if (startsWithWord(t, k))
                return !t.contains(QLatin1Char(';')) && !endsWithWord(t, QStringLiteral("end"));
        }
        return false;
    }
    if (id == QLatin1String("lua")) {
        if (endsWithWord(t, QStringLiteral("end")) || endsWithWord(t, QStringLiteral("until")))
            return false;
        return endsWithWord(t, QStringLiteral("then")) || endsWithWord(t, QStringLiteral("do"))
               || endsWithWord(t, QStringLiteral("repeat")) || endsWithWord(t, QStringLiteral("else"))
               || (startsWithWord(t, QStringLiteral("function")) && !endsWithWord(t, QStringLiteral("end")));
    }
    if (id == QLatin1String("bash")) {
        return endsWithWord(t, QStringLiteral("then")) || endsWithWord(t, QStringLiteral("do"))
               || endsWithWord(t, QStringLiteral("in")) || endsWithWord(t, QStringLiteral("else"))
               || endsWithWord(t, QStringLiteral("elif"));
    }
    if (id == QLatin1String("pascal")) {
        const QString l = t.toLower();
        return endsWithWord(l, QStringLiteral("begin")) || endsWithWord(l, QStringLiteral("do"))
               || endsWithWord(l, QStringLiteral("then")) || endsWithWord(l, QStringLiteral("else"))
               || endsWithWord(l, QStringLiteral("of")) || endsWithWord(l, QStringLiteral("repeat"))
               || startsWithWord(l, QStringLiteral("var")) || startsWithWord(l, QStringLiteral("type"))
               || startsWithWord(l, QStringLiteral("const"));
    }
    if (id == QLatin1String("fortran")) {
        const QString l = t.toLower();
        if (startsWithWord(l, QStringLiteral("end")))
            return false;
        return endsWithWord(l, QStringLiteral("then")) || startsWithWord(l, QStringLiteral("program"))
               || startsWithWord(l, QStringLiteral("subroutine")) || startsWithWord(l, QStringLiteral("function"))
               || startsWithWord(l, QStringLiteral("module")) || startsWithWord(l, QStringLiteral("do"))
               || startsWithWord(l, QStringLiteral("type")) || startsWithWord(l, QStringLiteral("interface"));
    }
    if (id == QLatin1String("haskell")) {
        return endsWithWord(t, QStringLiteral("where")) || endsWithWord(t, QStringLiteral("do"))
               || endsWithWord(t, QStringLiteral("of")) || endsWithWord(t, QStringLiteral("let"))
               || t.endsWith(QLatin1Char('='));
    }
    return false;
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
    // Usa increaseIndentPattern da linguagem quando disponível; fallback para opensBlock().
    const QRegularExpression &incPat = language().increaseIndentPattern;
    const bool opens = (incPat.isValid() && !incPat.pattern().isEmpty())
                           ? incPat.match(before).hasMatch()
                           : opensBlock(language().id, before);
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

    // Auto-fechar tags em HTML/XML: ao digitar ">" fechando <tag …>, insere </tag>.
    const QString langId = language().id;
    if (ch == QLatin1Char('>') && !c.hasSelection()
        && (langId == QLatin1String("html") || langId == QLatin1String("xml"))) {
        const QString before = line.left(col);
        const int lt = before.lastIndexOf(QLatin1Char('<'));
        if (lt >= 0) {
            const QString inside = before.mid(lt + 1);
            static const QRegularExpression nameRe(QStringLiteral("^([A-Za-z][\\w:-]*)"));
            const QRegularExpressionMatch m = nameRe.match(inside);
            const bool opening = m.hasMatch() && !inside.contains(QLatin1Char('>'))
                                 && !inside.endsWith(QLatin1Char('/'));
            if (opening) {
                const QString name = m.captured(1);
                static const QSet<QString> voids{QStringLiteral("area"), QStringLiteral("base"),
                    QStringLiteral("br"), QStringLiteral("col"), QStringLiteral("embed"),
                    QStringLiteral("hr"), QStringLiteral("img"), QStringLiteral("input"),
                    QStringLiteral("link"), QStringLiteral("meta"), QStringLiteral("param"),
                    QStringLiteral("source"), QStringLiteral("track"), QStringLiteral("wbr")};
                const bool isVoid = langId == QLatin1String("html") && voids.contains(name.toLower());
                if (!isVoid) {
                    c.insertText(QStringLiteral(">") + QStringLiteral("</") + name + QStringLiteral(">"));
                    c.movePosition(QTextCursor::Left, QTextCursor::MoveAnchor, name.length() + 3);
                    setTextCursor(c);
                    return true;
                }
            }
        }
    }

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
    // Sugestão inline (ghost text): Tab aceita, Esc dispensa.
    if (!m_ghostText.isEmpty() && m_ghostPos == textCursor().position() && !textCursor().hasSelection()) {
        if (event->key() == Qt::Key_Tab && event->modifiers() == Qt::NoModifier) {
            const QString g = m_ghostText;
            QTextCursor c = textCursor();
            c.insertText(g);
            setTextCursor(c);
            clearGhost();
            return;
        }
        if (event->key() == Qt::Key_Escape) {
            clearGhost();
            return;
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
            // Coluna visual: tabs contam como (tabW - col%tabW) colunas.
            const int tabW = qMax(1, m_indentSpaces);
            int col = 0;
            const QString lineText = c.block().text();
            for (int i = 0; i < c.positionInBlock() && i < lineText.size(); ++i) {
                if (lineText.at(i) == QLatin1Char('\t'))
                    col = (col / tabW + 1) * tabW;
                else
                    ++col;
            }
            c.insertText(m_insertSpaces ? QString(tabW - col % tabW, QLatin1Char(' '))
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

    // Auto-dedent ao fechar bloco: verifica decreaseIndentPattern ou } ] ) no início da linha.
    if (!c.hasSelection() && event->modifiers() == Qt::NoModifier) {
        const QString ch = event->text();
        if (!ch.isEmpty()) {
            const QString lineText = c.block().text();
            const QString before = lineText.left(c.positionInBlock());
            if (!before.isEmpty() && before.trimmed().isEmpty()) {
                // Hipótese: linha ficaria "recuo_atual + ch + resto".
                const QString hypothetical = before + ch + lineText.mid(c.positionInBlock());
                const QRegularExpression &decPat = language().decreaseIndentPattern;
                const bool shouldDedent =
                    (decPat.isValid() && !decPat.pattern().isEmpty())
                        ? decPat.match(hypothetical).hasMatch()
                        : (ch == QLatin1String("}") || ch == QLatin1String("]")
                           || ch == QLatin1String(")"));
                if (shouldDedent) {
                    const QString step = indentUnit();
                    const int stepLen = step.length();
                    if (before.endsWith(step)) {
                        QTextCursor bc(c.block());
                        bc.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
                        const QString newLine = before.left(before.length() - stepLen) + ch
                                               + lineText.mid(c.positionInBlock());
                        bc.insertText(newLine);
                        QTextCursor nc(c.block());
                        nc.movePosition(QTextCursor::Right, QTextCursor::MoveAnchor,
                                        before.length() - stepLen + 1);
                        setTextCursor(nc);
                        return;
                    }
                }
            }
        }
    }

    const bool manual = (event->modifiers() & Qt::ControlModifier) && event->key() == Qt::Key_Space;
    if (!manual)
        QTextEdit::keyPressEvent(event);

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
    } else if (event->key() == Qt::Key_Backspace && m_completionEnabled) {
        updateCompletion();
    } else if (!typed.isEmpty()) {
        clearGhost();
    }
}

void CodeEditor::setCompletionProvider(CompletionProvider provider, const QStringList &triggers)
{
    m_provider = std::move(provider);
    m_triggers = triggers;
}

void CodeEditor::goToDefinition()
{
    if (!m_definitionProvider)
        return;
    const QTextCursor c = textCursor();
    m_definitionProvider(c.blockNumber(), c.positionInBlock());
}

void CodeEditor::goToLineColumn(int line, int column)
{
    const QTextBlock b = document()->findBlockByNumber(line);
    if (!b.isValid())
        return;
    QTextCursor c(b);
    c.setPosition(b.position() + qBound(0, column, b.length() - 1));
    setTextCursor(c);
    centerCursorCompat();
    setFocus();
}

void CodeEditor::mousePressEvent(QMouseEvent *event)
{
    // Ctrl+clique com o botão esquerdo: ir para a definição sob o cursor.
    if (event->button() == Qt::LeftButton && (event->modifiers() & Qt::ControlModifier) && m_definitionProvider) {
        setTextCursor(cursorForPosition(event->pos()));
        goToDefinition();
        event->accept();
        return;
    }
    QTextEdit::mousePressEvent(event);
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
        clearGhost();
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
        auto it = wordRe.globalMatch(plainTextCached());
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
    m_provider(c.blockNumber(), c.positionInBlock(), [self, request, position, words](const QList<ServerItem> &items) {
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
        clearGhost();
        return;
    }
    // Sugere só no fim da palavra (não no meio dela).
    const QChar next = document()->characterAt(textCursor().position());
    if (next.isLetterOrNumber() || next == QLatin1Char('_')) {
        clearGhost();
        return;
    }
    // Melhor sugestão que começa pelo prefixo (mesma caixa): mostra o restante
    // após o que já foi digitado como ghost text (aceita com Tab).
    for (const QString &label : std::as_const(labels)) {
        const QString word = m_insertFor.value(label, label);
        if (word.size() > prefix.size() && word.startsWith(prefix)) {
            setGhost(word.mid(prefix.size()));
            return;
        }
    }
    clearGhost();
}

void CodeEditor::setGhost(const QString &text)
{
    m_ghostText = text;
    m_ghostPos = textCursor().position();
    viewport()->update();
}

void CodeEditor::clearGhost()
{
    if (m_ghostText.isEmpty() && m_ghostPos < 0)
        return;
    m_ghostText.clear();
    m_ghostPos = -1;
    viewport()->update();
}

void CodeEditor::paintGhostText(QPainter &p)
{
    if (m_ghostText.isEmpty() || m_ghostPos != textCursor().position() || textCursor().hasSelection())
        return;
    const QRect r = cursorRect();
    QColor c = m_theme.textSecondary;
    c.setAlpha(150);
    p.setPen(c);
    p.setFont(font());
    // Alinha pela linha de base do texto da linha atual.
    const QFontMetrics fm(font());
    p.drawText(r.left(), r.top() + fm.ascent(), m_ghostText);
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
void CodeEditor::setPrettySymbols(bool enabled, const QList<QPair<QString, QString>> &rules)
{
    if (m_prettySymbols == enabled && m_symbols == rules)
        return;
    m_prettySymbols = enabled;
    m_symbols = rules;
    viewport()->update();
}

/**
 * Desenha os "símbolos bonitos": onde o texto tem uma sequência configurada
 * (ex.: "<="), pinta por cima o símbolo (≤). O texto do documento não muda —
 * só a exibição. A sequência sob o cursor fica visível (para editar).
 */
void CodeEditor::paintPrettySymbols(QPainter &p)
{
    const QTextCursor tc = textCursor();
    const int cursorPos = tc.position();
    const int selStart = tc.selectionStart();
    const int selEnd = tc.selectionEnd();
    p.setFont(font());
    for (QTextBlock b = firstVisibleBlock(); b.isValid(); b = b.next()) {
        const QRectF geo = blockBoundingGeometry(b).translated(contentOffset());
        if (geo.top() > viewport()->height())
            break;
        if (!b.isVisible())
            continue;
        const QString text = b.text();
        const QList<QTextLayout::FormatRange> formats = b.layout()->formats();
        const bool currentLine = (b.blockNumber() == tc.blockNumber()) && m_highlightLine;
        for (int col = 0; col < text.size();) {
            bool matched = false;
            for (const auto &[seq, sym] : std::as_const(m_symbols)) {
                if (col + seq.size() > text.size() || QStringView(text).mid(col, seq.size()) != seq)
                    continue;
                const int len = seq.size();
                const int abs = b.position() + col;
                // Revela a sequência sob o cursor ou dentro de uma seleção.
                const bool nearCursor = cursorPos >= abs && cursorPos <= abs + len;
                const bool inSelection = selStart < abs + len && selEnd > abs;
                if (!nearCursor && !inSelection) {
                    const QTextLine line = b.layout()->lineForTextPosition(col);
                    if (line.isValid()) {
                        const qreal x0 = geo.left() + line.cursorToX(col);
                        const qreal x1 = geo.left() + line.cursorToX(col + len);
                        const QRectF rect(x0, geo.top() + line.y(), x1 - x0, line.height());
                        QColor fg = m_theme.text;
                        for (const QTextLayout::FormatRange &fr : formats) {
                            if (col >= fr.start && col < fr.start + fr.length
                                && fr.format.foreground().style() != Qt::NoBrush)
                                fg = fr.format.foreground().color();
                        }
                        p.fillRect(rect, currentLine ? m_theme.currentLine : m_theme.surface);
                        p.setPen(fg);
                        p.drawText(rect, Qt::AlignCenter, sym);
                    }
                }
                col += len;
                matched = true;
                break;
            }
            if (!matched)
                ++col;
        }
    }
}

/** Linhas verticais sutis de indentação, como no VSCode. */
void CodeEditor::paintIndentGuides(QPainter &p)
{
    const int tabW = qMax(1, m_indentSpaces);
    const qreal stepPx = tabW * fontMetrics().horizontalAdvance(QLatin1Char(' '));
    if (stepPx <= 0)
        return;
    QColor c = m_theme.textSecondary;
    c.setAlpha(55);
    p.setPen(QPen(c, 1));
    for (QTextBlock b = firstVisibleBlock(); b.isValid(); b = b.next()) {
        const QRectF geo = blockBoundingGeometry(b).translated(contentOffset());
        if (geo.top() > viewport()->height())
            break;
        if (!b.isVisible())
            continue;
        const QString text = b.text();
        // Colunas de indentação (tabs expandidos até a próxima parada).
        int cols = 0, i = 0;
        for (; i < text.size(); ++i) {
            const QChar ch = text.at(i);
            if (ch == QLatin1Char('\t'))
                cols += tabW - (cols % tabW);
            else if (ch == QLatin1Char(' '))
                ++cols;
            else
                break;
        }
        if (i >= text.size()) // linha vazia / só espaços: sem guias
            continue;
        const int levels = cols / tabW;
        for (int k = 0; k < levels; ++k) {
            const qreal x = geo.left() + k * stepPx + 0.5;
            p.drawLine(QPointF(x, geo.top()), QPointF(x, geo.bottom()));
        }
    }
}

void CodeEditor::paintEvent(QPaintEvent *event)
{
    QTextEdit::paintEvent(event);
    QPainter p(viewport());
    p.setRenderHint(QPainter::Antialiasing);
    if (!m_perfActive)
        paintIndentGuides(p);
    paintHighlightOutlines(p);
    if (m_prettySymbols && !m_symbols.isEmpty())
        paintPrettySymbols(p);
    paintGhostText(p);
    if (!m_doc->foldingEnabled())
        return;
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
    QTextEdit::wheelEvent(event);
}

// Arquivos locais arrastados de fora (gerenciador de arquivos, outra aba) devem
// abrir em novas abas, não inserir o caminho como texto. O QPlainTextEdit roteia
// o drop (e a colagem) pelo controle de texto, que chama insertFromMimeData —
// este é o ponto correto para interceptar. Qualquer outro conteúdo (texto
// arrastado/colado) mantém o comportamento padrão.
static QStringList localFilesFromMime(const QMimeData *mime)
{
    QStringList files;
    if (mime && mime->hasUrls()) {
        const QList<QUrl> urls = mime->urls();
        for (const QUrl &url : urls) {
            if (url.isLocalFile())
                files << url.toLocalFile();
        }
    }
    return files;
}

bool CodeEditor::canInsertFromMimeData(const QMimeData *source) const
{
    if (!localFilesFromMime(source).isEmpty())
        return true;
    return QTextEdit::canInsertFromMimeData(source);
}

void CodeEditor::insertFromMimeData(const QMimeData *source)
{
    const QStringList files = localFilesFromMime(source);
    if (!files.isEmpty()) {
        Q_EMIT filesDropped(files);
        return;
    }
    QTextEdit::insertFromMimeData(source);
}
