#include "Document.h"
#include "SyntaxHighlighter.h"

#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
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
    // Mudanças no disco: o evento chega em rajadas (e salvar por renomeação o
    // repete), então espera um pouco antes de olhar o arquivo.
    m_watcher = new QFileSystemWatcher(this);
    m_changeTimer = new QTimer(this);
    m_changeTimer->setSingleShot(true);
    m_changeTimer->setInterval(250);
    connect(m_watcher, &QFileSystemWatcher::fileChanged, m_changeTimer, qOverload<>(&QTimer::start));
    connect(m_changeTimer, &QTimer::timeout, this, &Document::onFileChanged);
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
    // Sem layout explícito: o QTextDocument cria, sob demanda, o
    // QTextDocumentLayout (layout rico) usado pelo QTextEdit — que honra
    // margens/indentação de bloco (necessário para o wrap indentado).
    m_highlighter = new SyntaxHighlighter(m_text);
    // Rastreamos o estado modificado sem chamar QTextDocument::setModified(false),
    // pois setModified(false) apaga o histórico de desfazer/refazer do Qt.
    connect(m_text, &QTextDocument::contentsChanged, this, [this]() {
        // Verificação rápida de tamanho evita toPlainText() caro na maioria
        // dos casos; só compara o texto completo quando os tamanhos batem.
        const bool couldMatch = (m_text->characterCount() == m_cleanText.size() + 1);
        const bool nowModified = !couldMatch || (m_text->toPlainText() != m_cleanText);
        if (nowModified != m_modified) {
            m_modified = nowModified;
            Q_EMIT changed();
        }
    });
}

void Document::setFilePath(const QString &path)
{
    m_filePath = path;
    watch();
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
    return m_modified;
}

void Document::markClean()
{
    m_cleanText = m_text->toPlainText();
    // Não chama setModified(false): isso limparia o histórico de desfazer.
    if (m_modified) {
        m_modified = false;
        Q_EMIT changed();
    }
}

bool Document::isUntitledAndEmpty() const
{
    return m_filePath.isEmpty() && !m_text->isModified() && m_text->isEmpty();
}

void Document::setLanguage(const Language &language)
{
    if (m_language.id == language.id && m_language.extensions == language.extensions
        && m_language.keywords == language.keywords && m_language.types == language.types)
        return;
    m_language = language;
    m_highlighter->setLanguage(language);
    if (!Language::debuggable(language.id))
        clearBreakpoints();
    m_folded.clear();
    recomputeFolds();
    Q_EMIT changed();
}

bool Document::exceedsLimits(qint64 bytes, int lines, int longestLine) const
{
    // Linha enorme (JS/JSON minificado): o layout dela custa como um arquivo grande.
    constexpr int kLongLine = 10000; // mesmo limite do VS Code (editor.stopRenderingLineAfter)
    return m_perfMode == 2
           || (m_perfMode == 1 && (lines > m_perfLines || bytes > m_perfBytes || longestLine > kLongLine));
}

void Document::setLargeFileLimits(int mode, int lines, qint64 bytes)
{
    if (m_perfMode == mode && m_perfLines == lines && m_perfBytes == bytes)
        return;
    m_perfMode = mode;
    m_perfLines = lines;
    m_perfBytes = bytes;
    // Já aberto: reavalia com o tamanho atual (aprox. em bytes = caracteres).
    int longest = 0;
    for (QTextBlock b = m_text->begin(); b.isValid(); b = b.next())
        longest = qMax(longest, b.length());
    setLargeFile(exceedsLimits(m_text->characterCount(), m_text->blockCount(), longest));
}

void Document::setLargeFile(bool large)
{
    if (m_largeFile == large)
        return;
    m_largeFile = large;
    m_highlighter->setDocument(large ? nullptr : m_text);
    if (large)
        m_folded.clear();
    recomputeFolds();
    Q_EMIT largeFileChanged(large);
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

namespace {
/** Bloco mais interno que contém a linha (cabeçalho incluído), ou nullptr. */
const FoldRegion *innermost(const QList<FoldRegion> &regions, int line)
{
    const FoldRegion *best = nullptr;
    for (const FoldRegion &r : regions) {
        if (line >= r.headerLine && line <= r.lastLine && (!best || r.headerLine > best->headerLine))
            best = &r;
    }
    return best;
}

bool isInside(const FoldRegion &inner, const FoldRegion &outer)
{
    return inner.headerLine > outer.headerLine && inner.lastLine <= outer.lastLine;
}
} // namespace

void Document::toggleCurrentNode(int line)
{
    if (const FoldRegion *r = innermost(m_regions, line))
        toggleFold(r->headerLine);
}

void Document::toggleContainedNodes(int line)
{
    const FoldRegion *outer = innermost(m_regions, line);
    if (!outer)
        return;
    QList<int> inner;
    bool anyOpen = false;
    for (const FoldRegion &r : std::as_const(m_regions)) {
        if (isInside(r, *outer)) {
            inner << r.headerLine;
            anyOpen |= !m_folded.contains(r.headerLine);
        }
    }
    if (inner.isEmpty())
        return;
    for (int h : std::as_const(inner)) {
        if (anyOpen)
            m_folded.insert(h);
        else
            m_folded.remove(h);
    }
    applyFolding();
}

void Document::setToplevelFolded(bool folded)
{
    for (const FoldRegion &r : std::as_const(m_regions)) {
        bool toplevel = true;
        for (const FoldRegion &o : std::as_const(m_regions))
            toplevel = toplevel && !isInside(r, o);
        if (!toplevel)
            continue;
        if (folded)
            m_folded.insert(r.headerLine);
        else
            m_folded.remove(r.headerLine);
    }
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
    m_regions = m_foldingEnabled && !m_largeFile ? Folding::detect(m_text->toPlainText(), m_language.id) : QList<FoldRegion>();
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
    if (!Language::debuggable(m_language.id))
        return; // sem depurador para esta linguagem: ponto de parada não serve

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

void Document::watch()
{
    const QStringList files = m_watcher->files();
    if (!files.isEmpty())
        m_watcher->removePaths(files);
    if (!m_filePath.isEmpty() && QFileInfo::exists(m_filePath))
        m_watcher->addPath(m_filePath);
}

void Document::rememberDiskState()
{
    const QFileInfo info(m_filePath);
    m_diskSize = info.exists() ? info.size() : -1;
    m_diskTime = info.exists() ? info.lastModified() : QDateTime();
}

void Document::acknowledgeDiskChange()
{
    rememberDiskState();
    watch();
}

void Document::onFileChanged()
{
    if (m_filePath.isEmpty())
        return;
    const QFileInfo info(m_filePath);
    // Salvar por renomeação (o nosso e o de muitos editores) tira o arquivo
    // da vigilância: volta a vigiar o novo.
    if (info.exists() && !m_watcher->files().contains(m_filePath))
        m_watcher->addPath(m_filePath);
    if (!info.exists()) {
        Q_EMIT changedOnDisk(true);
        return;
    }
    if (info.size() == m_diskSize && info.lastModified() == m_diskTime)
        return; // nada mudou de fato (ou foi o nosso próprio salvamento)
    if (m_autoReload && !isModified()) {
        if (reload())
            return;
    }
    Q_EMIT changedOnDisk(false);
}

bool Document::reload(QString *error)
{
    if (m_filePath.isEmpty())
        return false;
    Q_EMIT aboutToReload();
    const bool bom = m_bom;
    const bool ok = load(m_filePath, error, m_encoding);
    if (ok && bom)
        m_bom = true;
    Q_EMIT reloaded();
    return ok;
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
    // Final de linha: o editor usa LF; o salvar devolve CRLF se ele predominar,
    // para não reescrever o arquivo inteiro (diff de todas as linhas no git).
    const qsizetype crlf = text.count(QLatin1String("\r\n"));
    const qsizetype lf = text.count(QLatin1Char('\n'));
    text.replace(QLatin1String("\r\n"), QLatin1String("\n"));

    m_encoding = name;
    m_bom = bom;
    m_crlf = crlf > 0 && crlf * 2 >= lf;
    // Antes do setPlainText: sem realce, o arquivo grande não é colorido inteiro ao abrir.
    int lines = 1, longest = 0;
    for (qsizetype i = 0, start = 0; i <= text.size(); ++i) {
        if (i == text.size() || text.at(i) == QLatin1Char('\n')) {
            longest = qMax(longest, int(i - start));
            start = i + 1;
            if (i < text.size())
                ++lines;
        }
    }
    setLargeFile(exceedsLimits(data.size(), lines, longest));
    m_text->setPlainText(text);
    markClean();
    setFilePath(path);
    rememberDiskState();
    return true;
}

QString Document::diskText() const
{
    QString text = m_text->toPlainText();
    if (m_crlf)
        text.replace(QLatin1Char('\n'), QLatin1String("\r\n"));
    return text;
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
    const QByteArray data = encoder.encode(diskText());
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
    markClean();
    setFilePath(path);
    rememberDiskState(); // o próprio salvamento não conta como mudança externa
    return true;
}
