#include "Document.h"
#include "SyntaxHighlighter.h"

#include <QFile>
#include <QFileInfo>
#include <QPlainTextDocumentLayout>
#include <QSaveFile>
#include <QTextDocument>
#include <QTextBlock>
#include <QTextStream>
#include <QTimer>

Document::Document(QObject *parent)
    : QObject(parent)
    , m_text(new QTextDocument(this))
    , m_highlighter(nullptr)
    , m_language(Language::plainText())
    , m_foldTimer(new QTimer(this))
{
    m_foldTimer->setSingleShot(true);
    m_foldTimer->setInterval(200);
    connect(m_foldTimer, &QTimer::timeout, this, &Document::recomputeFolds);
    connect(m_text, &QTextDocument::contentsChange, this, [this](int pos, int, int) {
        // Linhas inseridas/removidas deslocam os cabeçalhos dobrados abaixo delas.
        if (!m_folded.isEmpty()) {
            const int line = m_text->findBlock(pos).blockNumber();
            const int delta = m_text->blockCount() - m_lastBlockCount;
            if (delta != 0) {
                QSet<int> moved;
                for (int h : std::as_const(m_folded))
                    moved.insert(h > line ? h + delta : h);
                m_folded = moved;
            }
        }
        m_lastBlockCount = m_text->blockCount();
        m_foldTimer->start();
    });
    // Necessário para o documento ser compartilhado por QPlainTextEdits.
    m_text->setDocumentLayout(new QPlainTextDocumentLayout(m_text));
    m_highlighter = new SyntaxHighlighter(m_text);
    connect(m_text, &QTextDocument::modificationChanged, this, &Document::changed);
}

void Document::setFilePath(const QString &path)
{
    m_filePath = path;
    Q_EMIT changed();
}

QString Document::displayName() const
{
    return m_filePath.isEmpty() ? tr("Untitled") : QFileInfo(m_filePath).fileName();
}

bool Document::isModified() const
{
    return m_text->isModified();
}

bool Document::isUntitledAndEmpty() const
{
    return m_filePath.isEmpty() && !m_text->isModified() && m_text->isEmpty();
}

void Document::setLanguage(const Language &language)
{
    m_language = language;
    m_highlighter->setLanguage(language);
    m_folded.clear();
    recomputeFolds();
    Q_EMIT changed();
}

void Document::setFoldingEnabled(bool enabled)
{
    if (m_foldingEnabled == enabled)
        return;
    m_foldingEnabled = enabled;
    if (!enabled)
        m_folded.clear();
    recomputeFolds();
}

const FoldRegion *Document::regionAt(int headerLine) const
{
    for (const FoldRegion &r : m_regions) {
        if (r.headerLine == headerLine)
            return &r;
    }
    return nullptr;
}

void Document::toggleFold(int headerLine)
{
    setFolded(headerLine, !isFolded(headerLine));
}

void Document::setFolded(int headerLine, bool folded)
{
    if (!regionAt(headerLine) || isFolded(headerLine) == folded)
        return;
    if (folded)
        m_folded.insert(headerLine);
    else
        m_folded.remove(headerLine);
    applyFolding();
}

void Document::foldAll()
{
    for (const FoldRegion &r : std::as_const(m_regions))
        m_folded.insert(r.headerLine);
    applyFolding();
}

void Document::unfoldAll()
{
    if (m_folded.isEmpty())
        return;
    m_folded.clear();
    applyFolding();
}

void Document::revealLine(int line)
{
    bool changed = false;
    for (const FoldRegion &r : std::as_const(m_regions)) {
        if (m_folded.contains(r.headerLine) && line > r.headerLine && line <= r.lastLine) {
            m_folded.remove(r.headerLine);
            changed = true;
        }
    }
    if (changed)
        applyFolding();
}

void Document::recomputeFolds()
{
    m_regions = m_foldingEnabled ? Folding::detect(m_text->toPlainText(), m_language.id) : QList<FoldRegion>();
    // Mantém só as dobras cujo cabeçalho ainda abre um bloco.
    QSet<int> kept;
    for (int h : std::as_const(m_folded)) {
        if (regionAt(h))
            kept.insert(h);
    }
    m_folded = kept;
    applyFolding();
}

void Document::applyFolding()
{
    QList<bool> hidden(m_text->blockCount(), false);
    for (const FoldRegion &r : std::as_const(m_regions)) {
        if (!m_folded.contains(r.headerLine))
            continue;
        for (int l = r.headerLine + 1; l <= r.lastLine && l < hidden.size(); ++l)
            hidden[l] = true;
    }
    bool any = false;
    for (QTextBlock b = m_text->begin(); b.isValid(); b = b.next()) {
        const bool visible = !hidden.value(b.blockNumber());
        if (b.isVisible() != visible) {
            b.setVisible(visible);
            b.setLineCount(visible ? qMax(1, b.layout()->lineCount()) : 0);
            any = true;
        }
    }
    if (any)
        m_text->markContentsDirty(0, m_text->characterCount());
    Q_EMIT foldingChanged();
}

void Document::setTheme(const Theme &theme)
{
    m_highlighter->setTheme(theme);
}

bool Document::load(const QString &path, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error)
            *error = file.errorString();
        return false;
    }
    QTextStream in(&file);
    m_text->setPlainText(in.readAll());
    m_text->setModified(false);
    setFilePath(path);
    return true;
}

bool Document::save(const QString &path, QString *error)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error)
            *error = file.errorString();
        return false;
    }
    QTextStream out(&file);
    out << m_text->toPlainText();
    out.flush();
    if (!file.commit()) {
        if (error)
            *error = file.errorString();
        return false;
    }
    m_text->setModified(false);
    setFilePath(path);
    return true;
}
