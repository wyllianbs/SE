#include "SyntaxHighlighter.h"

SyntaxHighlighter::SyntaxHighlighter(QTextDocument *document)
    : QSyntaxHighlighter(document)
    , m_language(Language::plainText())
    , m_theme(Theme::all().first())
{
}

void SyntaxHighlighter::setLanguage(const Language &language)
{
    m_language = language;
    rebuildRules();
}

void SyntaxHighlighter::setTheme(const Theme &theme)
{
    m_theme = theme;
    rebuildRules();
}

static QTextCharFormat fmt(const QColor &color, bool bold = false, bool italic = false)
{
    QTextCharFormat f;
    f.setForeground(color);
    if (bold)
        f.setFontWeight(QFont::Bold);
    f.setFontItalic(italic);
    return f;
}

static QString wordAlternation(const QStringList &words)
{
    QStringList escaped;
    for (const QString &w : words)
        escaped << QRegularExpression::escape(w);
    return QStringLiteral("\\b(?:") + escaped.join(QLatin1Char('|')) + QStringLiteral(")\\b");
}

void SyntaxHighlighter::rebuildRules()
{
    m_rules.clear();
    m_blocks.clear();

    if (m_language.id == QLatin1String("text")) {
        rehighlight();
        return;
    }

    // A ordem importa: regras posteriores sobrescrevem as anteriores.
    m_rules.append({QRegularExpression(QStringLiteral("\\b[A-Za-z_]\\w*(?=\\s*\\()")),
                    fmt(m_theme.function)});
    m_rules.append({QRegularExpression(
                        QStringLiteral("\\b(?:0[xX][0-9a-fA-F_]+|0[bB][01_]+|\\d[\\d_]*(?:\\.\\d+)?(?:[eE][+-]?\\d+)?)[uUlLfF]*\\b")),
                    fmt(m_theme.number)});
    if (!m_language.types.isEmpty())
        m_rules.append({QRegularExpression(wordAlternation(m_language.types)), fmt(m_theme.type)});
    if (!m_language.keywords.isEmpty())
        m_rules.append({QRegularExpression(wordAlternation(m_language.keywords)),
                        fmt(m_theme.keyword, true)});
    if (m_language.id == QLatin1String("c") || m_language.id == QLatin1String("cpp"))
        m_rules.append({QRegularExpression(QStringLiteral("^\\s*#\\s*\\w+.*$")),
                        fmt(m_theme.preprocessor)});
    if (m_language.id == QLatin1String("python"))
        m_rules.append({QRegularExpression(QStringLiteral("^\\s*@[\\w.]+")), fmt(m_theme.preprocessor)});
    if (m_language.id == QLatin1String("css"))
        m_rules.append({QRegularExpression(QStringLiteral("@[\\w-]+")), fmt(m_theme.preprocessor)}); // @media, @import…
    if (m_language.id == QLatin1String("bash"))
        m_rules.append({QRegularExpression(QStringLiteral("\\$\\{?[A-Za-z_]\\w*\\}?")),
                        fmt(m_theme.preprocessor)});
    m_rules.append({QRegularExpression(QStringLiteral("\"(?:[^\"\\\\]|\\\\.)*\"|'(?:[^'\\\\]|\\\\.)*'")),
                    fmt(m_theme.string)});
    if (m_language.id == QLatin1String("javascript") || m_language.id == QLatin1String("go")
        || m_language.id == QLatin1String("typescript"))
        m_rules.append({QRegularExpression(QStringLiteral("`[^`]*`")), fmt(m_theme.string)});
    if (m_language.id == QLatin1String("markdown")) {
        // Realce básico de Markdown (sem keywords/strings no sentido usual).
        m_rules.append({QRegularExpression(QStringLiteral("`[^`]+`")), fmt(m_theme.string)});
        m_rules.append({QRegularExpression(QStringLiteral("(?:\\*\\*|__)(?=\\S)(.+?)(?:\\*\\*|__)")),
                        fmt(m_theme.type, true)});
        m_rules.append({QRegularExpression(QStringLiteral("\\[[^\\]]+\\]\\([^)]+\\)")), fmt(m_theme.function)});
        m_rules.append({QRegularExpression(QStringLiteral("^#{1,6}\\s.*$")), fmt(m_theme.keyword, true)});
    }

    m_commentFormat = fmt(m_theme.comment, false, true);
    if (!m_language.blockCommentStart.isEmpty())
        m_blocks.append({m_language.blockCommentStart, m_language.blockCommentEnd});
    // Python: docstrings com ''' também são decoradas como comentário.
    if (m_language.id == QLatin1String("python"))
        m_blocks.append({QStringLiteral("'''"), QStringLiteral("'''")});
    rehighlight();
}

bool SyntaxHighlighter::inString(int pos) const
{
    return format(pos).foreground().color() == m_theme.string;
}

void SyntaxHighlighter::highlightBlock(const QString &text)
{
    for (const Rule &rule : std::as_const(m_rules)) {
        auto it = rule.pattern.globalMatch(text);
        while (it.hasNext()) {
            const auto match = it.next();
            setFormat(match.capturedStart(), match.capturedLength(), rule.format);
        }
    }

    // Comentário de linha, ignorando o marcador quando ele está dentro de string.
    // (Busca posição a posição: um regex ".*$" consumiria a linha já no
    // primeiro marcador dentro de string, como em "http://…".)
    const QString &marker = m_language.lineComment;
    if (!marker.isEmpty()) {
        for (int pos = text.indexOf(marker); pos >= 0; pos = text.indexOf(marker, pos + 1)) {
            if (!inString(pos)) {
                setFormat(pos, text.length() - pos, m_commentFormat);
                break;
            }
        }
    }

    // Comentários de bloco (multilinha). Estado 1 + i = dentro do par i, para
    // que um bloco aberto com ''' só feche com '''.
    setCurrentBlockState(0);
    if (m_blocks.isEmpty())
        return;
    // Aspas triplas do Python são, elas mesmas, aspas: o teste de "dentro de
    // string" não se aplica a elas.
    const bool quoteDelimiters = (m_language.id == QLatin1String("python"));

    int pos = 0;
    int open = previousBlockState() - 1; // par aberto vindo da linha anterior
    if (open < 0 || open >= m_blocks.size())
        open = -1;
    while (pos <= text.length()) {
        int start = 0;
        if (open < 0) {
            // Próximo delimitador de abertura, o mais à esquerda. Não conta
            // se estiver dentro de string ou de um comentário de linha.
            int bestPos = -1;
            for (int i = 0; i < m_blocks.size(); ++i) {
                int p = text.indexOf(m_blocks[i].first, pos);
                while (p >= 0 && ((!quoteDelimiters && inString(p)) || format(p) == m_commentFormat))
                    p = text.indexOf(m_blocks[i].first, p + 1);
                if (p >= 0 && (bestPos < 0 || p < bestPos)) {
                    open = i;
                    bestPos = p;
                }
            }
            if (bestPos < 0)
                break;
            start = bestPos;
            pos = bestPos + m_blocks[open].first.length();
        }
        const int close = text.indexOf(m_blocks[open].second, pos);
        if (close < 0) {
            setFormat(start, text.length() - start, m_commentFormat);
            setCurrentBlockState(1 + open);
            return;
        }
        const int stop = close + m_blocks[open].second.length();
        setFormat(start, stop - start, m_commentFormat);
        pos = stop;
        open = -1;
    }
}
