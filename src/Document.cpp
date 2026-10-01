#include "Document.h"
#include "SyntaxHighlighter.h"

#include <QFile>
#include <QFileInfo>
#include <QPlainTextDocumentLayout>
#include <QSaveFile>
#include <QStringConverter>
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
        // Linhas inseridas/removidas deslocam dobras e pontos de parada abaixo delas.
        const int delta = m_text->blockCount() - m_lastBlockCount;
        if (delta != 0) {
            const int line = m_text->findBlock(pos).blockNumber();
            auto shift = [line, delta](const QSet<int> &set) {
                QSet<int> moved;
                for (int h : set) {
                    if (h <= line)
                        moved.insert(h);
                    else if (h + delta > line)
                        moved.insert(h + delta); // linhas apagadas levam o ponto junto
                }
                return moved;
            };
            m_folded = shift(m_folded);
            if (!m_breakpoints.isEmpty()) {
                m_breakpoints = shift(m_breakpoints);
                Q_EMIT breakpointsChanged();
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
    if (!m_filePath.isEmpty())
        return QFileInfo(m_filePath).fileName();
    return m_suggestedName.isEmpty() ? tr("Untitled") : m_suggestedName;
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

void Document::toggleBreakpoint(int line)
{
    if (!m_breakpoints.remove(line))
        m_breakpoints.insert(line);
    Q_EMIT breakpointsChanged();
}

void Document::clearBreakpoints()
{
    if (m_breakpoints.isEmpty())
        return;
    m_breakpoints.clear();
    Q_EMIT breakpointsChanged();
}

void Document::setExecutionLine(int line)
{
    if (m_executionLine == line)
        return;
    m_executionLine = line;
    Q_EMIT executionLineChanged();
}

void Document::setDiagnostics(const QList<Diagnostic> &diagnostics)
{
    m_diagnostics = diagnostics;
    Q_EMIT diagnosticsChanged();
}

QList<Diagnostic> Document::diagnosticsAt(int line) const
{
    QList<Diagnostic> out;
    for (const Diagnostic &d : m_diagnostics) {
        if (line >= d.line && line <= d.endLine)
            out << d;
    }
    std::stable_sort(out.begin(), out.end(), [](const Diagnostic &a, const Diagnostic &b) { return a.severity < b.severity; });
    return out;
}

void Document::setEncoding(const QString &encoding, bool bom)
{
    m_encoding = encoding;
    m_bom = bom;
    Q_EMIT changed();
}

QString Document::encodingLabel() const
{
    return m_bom ? m_encoding + QStringLiteral(" (BOM)") : m_encoding;
}

bool Document::load(const QString &path, QString *error, const QString &encoding)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = file.errorString();
        return false;
    }
    const QByteArray data = file.readAll();

    QString name = encoding;
    bool bom = false;
    if (const auto detected = QStringConverter::encodingForData(data); detected) {
        // BOM presente: ele manda (UTF-8/16/32).
        bom = true;
        if (name.isEmpty())
            name = QString::fromLatin1(QStringConverter::nameForEncoding(*detected));
    }
    QString text;
    if (name.isEmpty()) {
        QStringDecoder utf8(QStringConverter::Utf8);
        text = utf8.decode(data);
        name = QStringLiteral("UTF-8");
        if (utf8.hasError()) {
            // Não é UTF-8 válido: arquivos antigos costumam ser Windows-1252/Latin-1.
            QStringDecoder legacy("windows-1252");
            text = legacy.decode(data);
            name = QStringLiteral("windows-1252");
        }
    } else {
        QStringDecoder decoder(name.toLatin1().constData(),
                               bom ? QStringConverter::Flag::Default : QStringConverter::Flag::Stateless);
        if (!decoder.isValid()) {
            if (error)
                *error = tr("Unknown encoding: %1").arg(name);
            return false;
        }
        text = decoder.decode(data);
    }
    if (text.startsWith(QChar(0xFEFF)))
        text.remove(0, 1);
    text.replace(QLatin1String("\r\n"), QLatin1String("\n"));

    m_encoding = name;
    m_bom = bom;
    m_text->setPlainText(text);
    m_text->setModified(false);
    setFilePath(path);
    return true;
}

bool Document::save(const QString &path, QString *error)
{
    QStringEncoder encoder(m_encoding.toLatin1().constData(),
                           m_bom ? QStringConverter::Flag::WriteBom : QStringConverter::Flag::Default);
    if (!encoder.isValid()) {
        if (error)
            *error = tr("Unknown encoding: %1").arg(m_encoding);
        return false;
    }
    const QByteArray data = encoder.encode(m_text->toPlainText());
    if (encoder.hasError()) {
        if (error)
            *error = tr("Some characters cannot be represented in %1.").arg(m_encoding);
        return false;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error)
            *error = file.errorString();
        return false;
    }
    file.write(data);
    if (!file.commit()) {
        if (error)
            *error = file.errorString();
        return false;
    }
    m_text->setModified(false);
    setFilePath(path);
    return true;
}
