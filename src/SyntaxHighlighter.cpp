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
    m_lineComment = QRegularExpression();
    m_blockStart = QRegularExpression();
    m_blockEnd = QRegularExpression();

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
    if (m_language.id == QLatin1String("bash"))
        m_rules.append({QRegularExpression(QStringLiteral("\\$\\{?[A-Za-z_]\\w*\\}?")),
                        fmt(m_theme.preprocessor)});
    m_rules.append({QRegularExpression(QStringLiteral("\"(?:[^\"\\\\]|\\\\.)*\"|'(?:[^'\\\\]|\\\\.)*'")),
                    fmt(m_theme.string)});
    if (m_language.id == QLatin1String("javascript") || m_language.id == QLatin1String("go"))
        m_rules.append({QRegularExpression(QStringLiteral("`[^`]*`")), fmt(m_theme.string)});

    m_commentFormat = fmt(m_theme.comment, false, true);
    if (!m_language.lineComment.isEmpty())
        m_lineComment = QRegularExpression(QRegularExpression::escape(m_language.lineComment) + QStringLiteral(".*$"));
    if (!m_language.blockCommentStart.isEmpty()) {
        m_blockStart = QRegularExpression(QRegularExpression::escape(m_language.blockCommentStart));
        m_blockEnd = QRegularExpression(QRegularExpression::escape(m_language.blockCommentEnd));
    }
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
    if (m_lineComment.isValid() && !m_lineComment.pattern().isEmpty()) {
        auto it = m_lineComment.globalMatch(text);
        while (it.hasNext()) {
            const auto match = it.next();
            const int pos = match.capturedStart();
            if (!inString(pos)) {
                setFormat(pos, text.length() - pos, m_commentFormat);
                break;
            }
        }
    }

    // Comentários de bloco (multilinha): estado 1 = dentro de comentário.
    setCurrentBlockState(0);
    if (m_blockStart.pattern().isEmpty())
        return;

    int start = 0;
    if (previousBlockState() != 1) {
        const auto m = m_blockStart.match(text);
        start = m.hasMatch() ? m.capturedStart() : -1;
        // Marcador dentro de string não abre comentário.
        while (start >= 0 && inString(start) && m_language.id != QLatin1String("python")) {
            const auto next = m_blockStart.match(text, start + 1);
            start = next.hasMatch() ? next.capturedStart() : -1;
        }
    }
    const int openLen = m_language.blockCommentStart.length();
    while (start >= 0) {
        const int searchFrom = (previousBlockState() == 1 && start == 0) ? 0 : start + openLen;
        const auto end = m_blockEnd.match(text, searchFrom);
        int length;
        if (!end.hasMatch()) {
            setCurrentBlockState(1);
            length = text.length() - start;
        } else {
            length = end.capturedEnd() - start;
        }
        setFormat(start, length, m_commentFormat);
        const auto next = m_blockStart.match(text, start + length);
        start = next.hasMatch() ? next.capturedStart() : -1;
    }
}
