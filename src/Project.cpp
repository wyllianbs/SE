#include "Project.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QThread>

#include <algorithm>
#include <memory>

namespace {

/** Converte um glob do .gitignore numa expressão regular (sem âncoras). */
QString globToRegex(const QString &glob)
{
    QString re;
    for (int i = 0; i < glob.size(); ++i) {
        const QChar c = glob.at(i);
        if (c == QLatin1Char('*')) {
            if (i + 1 < glob.size() && glob.at(i + 1) == QLatin1Char('*')) {
                ++i;
                // "**/" casa zero ou mais pastas; "**" sozinho, qualquer coisa.
                if (i + 1 < glob.size() && glob.at(i + 1) == QLatin1Char('/')) {
                    ++i;
                    re += QStringLiteral("(?:.*/)?");
                } else {
                    re += QStringLiteral(".*");
                }
            } else {
                re += QStringLiteral("[^/]*");
            }
        } else if (c == QLatin1Char('?')) {
            re += QStringLiteral("[^/]");
        } else if (c == QLatin1Char('[')) {
            const int end = glob.indexOf(QLatin1Char(']'), i + 1);
            if (end < 0) {
                re += QStringLiteral("\\[");
                continue;
            }
            QString set = glob.mid(i + 1, end - i - 1);
            if (set.startsWith(QLatin1Char('!')))
                set[0] = QLatin1Char('^');
            re += QLatin1Char('[') + set + QLatin1Char(']');
            i = end;
        } else if (c == QLatin1Char('\\') && i + 1 < glob.size()) {
            re += QRegularExpression::escape(glob.at(++i));
        } else {
            re += QRegularExpression::escape(c);
        }
    }
    return re;
}

} // namespace

void IgnoreRules::addPattern(const QString &line)
{
    QString p = line;
    // Espaços à direita não contam (salvo escapados, raro: ignorado aqui).
    while (p.endsWith(QLatin1Char(' ')) || p.endsWith(QLatin1Char('\t')) || p.endsWith(QLatin1Char('\r')))
        p.chop(1);
    if (p.isEmpty() || p.startsWith(QLatin1Char('#')))
        return;
    Rule r;
    if (p.startsWith(QLatin1Char('!'))) {
        r.negate = true;
        p.remove(0, 1);
    } else if (p.startsWith(QStringLiteral("\\!")) || p.startsWith(QStringLiteral("\\#"))) {
        p.remove(0, 1);
    }
    if (p.endsWith(QLatin1Char('/'))) {
        r.dirOnly = true;
        p.chop(1);
    }
    if (p.startsWith(QLatin1Char('/'))) {
        r.anchored = true;
        p.remove(0, 1);
    } else if (p.contains(QLatin1Char('/'))) {
        r.anchored = true;
    }
    if (p.isEmpty())
        return;
    r.re = QRegularExpression(QStringLiteral("\\A") + globToRegex(p) + QStringLiteral("\\z"));
    if (!r.re.isValid())
        return;
    m_rules.append(r);
}

void IgnoreRules::addPatterns(const QStringList &patterns)
{
    for (const QString &p : patterns)
        addPattern(p);
}

void IgnoreRules::loadFile(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return;
    const QStringList lines = QString::fromUtf8(f.readAll()).split(QLatin1Char('\n'));
    addPatterns(lines);
}

bool IgnoreRules::matches(const QString &relPath, bool isDir) const
{
    const QString name = relPath.section(QLatin1Char('/'), -1);
    bool ignored = false;
    for (const Rule &r : m_rules) {
        if (r.dirOnly && !isDir)
            continue;
        if (r.negate != ignored) // só uma regra que mudaria o resultado precisa ser testada
            continue;
        if (r.re.match(r.anchored ? relPath : name).hasMatch())
            ignored = !r.negate;
    }
    return ignored;
}

Project::Project(const QString &root, const QStringList &excludes, QObject *parent)
    : QObject(parent)
    , m_root(QDir(root).absolutePath())
{
    m_hidden.addPatterns(excludes);
    m_ignored.addPatterns(excludes);
    m_ignored.loadFile(QDir(m_root).filePath(QStringLiteral(".gitignore")));
}

Project::~Project() = default;

QString Project::name() const
{
    return QFileInfo(m_root).fileName();
}

bool Project::contains(const QString &absPath) const
{
    const QString p = QDir::cleanPath(QFileInfo(absPath).absoluteFilePath());
    return p == m_root || p.startsWith(m_root + QLatin1Char('/'));
}

QString Project::relativePath(const QString &absPath) const
{
    return QDir(m_root).relativeFilePath(absPath);
}

QString Project::absolutePath(const QString &relPath) const
{
    return QDir(m_root).filePath(relPath);
}

bool Project::isHidden(const QString &absPath, bool isDir) const
{
    if (!contains(absPath))
        return false;
    const QString rel = relativePath(absPath);
    return rel != QLatin1String(".") && m_hidden.matches(rel, isDir);
}

bool Project::isIgnored(const QString &relPath, bool isDir) const
{
    return m_ignored.matches(relPath, isDir);
}

QStringList Project::listFiles(const QString &root, const IgnoreRules &hidden, const IgnoreRules &ignored,
                               int limit, bool *truncated)
{
    QStringList out;
    if (truncated)
        *truncated = false;
    // Pilha de pastas (relativas); a poda evita descer em .git, node_modules…
    QStringList pending{QString()};
    const QDir base(root);
    while (!pending.isEmpty()) {
        const QString rel = pending.takeLast();
        const QDir dir(rel.isEmpty() ? root : base.filePath(rel));
        const QFileInfoList entries = dir.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot
                                                        | QDir::Hidden | QDir::System);
        for (const QFileInfo &fi : entries) {
            const QString childRel = rel.isEmpty() ? fi.fileName() : rel + QLatin1Char('/') + fi.fileName();
            const bool isDir = fi.isDir();
            if (hidden.matches(childRel, isDir) || ignored.matches(childRel, isDir))
                continue;
            if (isDir) {
                if (!fi.isSymLink()) // links de pasta podem formar ciclos
                    pending.append(childRel);
            } else {
                if (out.size() >= limit) {
                    if (truncated)
                        *truncated = true;
                    std::sort(out.begin(), out.end());
                    return out;
                }
                out.append(childRel);
            }
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

void Project::refreshFiles()
{
    const int generation = ++m_generation;
    const QString root = m_root;
    const IgnoreRules hidden = m_hidden;
    const IgnoreRules ignored = m_ignored;
    auto result = std::make_shared<std::pair<QStringList, bool>>();
    QThread *thread = QThread::create([root, hidden, ignored, result]() {
        result->first = listFiles(root, hidden, ignored, kMaxFiles, &result->second);
    });
    // A thread se apaga sozinha; o resultado só vale se o projeto ainda existe
    // e se não houve outra listagem depois desta.
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    connect(thread, &QThread::finished, this, [this, result, generation]() {
        if (generation != m_generation)
            return;
        m_files = result->first;
        m_truncated = result->second;
        m_filesReady = true;
        Q_EMIT filesListed();
    });
    thread->start(QThread::LowPriority);
}

int Project::fuzzyScore(const QString &pattern, const QString &candidate)
{
    if (pattern.isEmpty())
        return 0;
    // Cada caractere do padrão, em ordem, no caminho; pontua sequências,
    // inícios de palavra e acertos no nome do arquivo (como no VS Code).
    const int nameStart = candidate.lastIndexOf(QLatin1Char('/')) + 1;
    int score = 0, pos = 0, prev = -2;
    for (const QChar pc : pattern) {
        if (pc.isSpace())
            continue;
        const int i = candidate.indexOf(pc, pos, Qt::CaseInsensitive);
        if (i < 0)
            return -1;
        int s = 1;
        if (i == prev + 1)
            s += 10;
        if (i == 0 || i == nameStart || (QStringLiteral("/_-. ").contains(candidate.at(i - 1))))
            s += 8;
        if (i >= nameStart)
            s += 2;
        if (candidate.at(i) == pc)
            s += 1;
        score += s;
        prev = i;
        pos = i + 1;
    }
    return score * 100 / (100 + candidate.size()) + 1; // caminhos curtos ganham no empate
}

QList<Project::Match> Project::search(const QString &root, const QStringList &files, const QString &pattern,
                                      SearchOptions options, int limit, bool *truncated)
{
    QList<Match> out;
    if (truncated)
        *truncated = false;
    if (pattern.isEmpty())
        return out;
    QString re = options.regex ? pattern : QRegularExpression::escape(pattern);
    if (options.wholeWord)
        re = QStringLiteral("\\b(?:") + re + QStringLiteral(")\\b");
    const QRegularExpression rx(re, options.caseSensitive ? QRegularExpression::NoPatternOption
                                                          : QRegularExpression::CaseInsensitiveOption);
    if (!rx.isValid())
        return out;
    const QDir base(root);
    for (const QString &rel : files) {
        QFile f(base.filePath(rel));
        if (f.size() > kMaxSearchFileSize || !f.open(QIODevice::ReadOnly))
            continue;
        const QByteArray data = f.readAll();
        if (data.left(8192).contains('\0')) // binário
            continue;
        const QStringList lines = QString::fromUtf8(data).split(QLatin1Char('\n'));
        for (int ln = 0; ln < lines.size(); ++ln) {
            QString line = lines.at(ln);
            if (line.endsWith(QLatin1Char('\r')))
                line.chop(1);
            auto it = rx.globalMatch(line);
            while (it.hasNext()) {
                const QRegularExpressionMatch m = it.next();
                if (m.capturedLength() == 0)
                    continue;
                if (out.size() >= limit) {
                    if (truncated)
                        *truncated = true;
                    return out;
                }
                out.append({rel, ln, int(m.capturedStart()), int(m.capturedLength()), line});
            }
        }
    }
    return out;
}
