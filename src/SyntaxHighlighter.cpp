#include "SyntaxHighlighter.h"

#include <QTextBlock>
#include <QTextDocument>
#include <QTextLayout>

SyntaxHighlighter::SyntaxHighlighter(QTextDocument *document)
    : QSyntaxHighlighter(document)
    , m_language(Language::plainText())
    , m_theme(Theme::all().first())
{
}

void SyntaxHighlighter::setLanguage(const Language &language)
{
    if (m_language.id == language.id && m_language.keywords == language.keywords
        && m_language.types == language.types
        && m_language.lineComment == language.lineComment
        && m_language.blockCommentStart == language.blockCommentStart
        && m_language.blockCommentEnd == language.blockCommentEnd)
        return;
    m_language = language;
    rebuildRules(true);
}

// Propriedade que marca o papel (keyword, string…) de cada formato: na troca
// de tema basta trocar a cor, sem reexecutar as regras.
static constexpr int RoleProperty = QTextFormat::UserProperty + 1;

static QColor roleColor(const Theme &t, SyntaxHighlighter::Role role)
{
    using R = SyntaxHighlighter::Role;
    switch (role) {
    case R::Function: return t.function;
    case R::Number: return t.number;
    case R::Type: return t.type;
    case R::Keyword: return t.keyword;
    case R::Preprocessor: return t.preprocessor;
    case R::String: return t.string;
    case R::Comment: return t.comment;
    case R::None: break;
    }
    return t.text;
}

void SyntaxHighlighter::setTheme(const Theme &theme)
{
    if (m_theme.id == theme.id && m_theme.keyword == theme.keyword && m_theme.type == theme.type
        && m_theme.comment == theme.comment && m_theme.string == theme.string
        && m_theme.number == theme.number && m_theme.function == theme.function
        && m_theme.preprocessor == theme.preprocessor && m_theme.text == theme.text)
        return;
    m_theme = theme;
    rebuildRules(false);
    recolor();
}

void SyntaxHighlighter::recolor()
{
    // Só as cores mudam. rehighlight() reexecutaria as regras e marcaria cada
    // bloco sujo isoladamente — no QTextDocumentLayout isso relayouta o
    // documento N vezes (segundos de freeze). Aqui: troca a cor nos formatos
    // já aplicados e invalida o documento uma única vez.
    QTextDocument *doc = document();
    if (!doc)
        return;
    for (QTextBlock b = doc->firstBlock(); b.isValid(); b = b.next()) {
        QTextLayout *layout = b.layout();
        QList<QTextLayout::FormatRange> ranges = layout->formats();
        bool changed = false;
        for (QTextLayout::FormatRange &r : ranges) {
            const int role = r.format.intProperty(RoleProperty);
            if (role != int(Role::None)) {
                r.format.setForeground(roleColor(m_theme, Role(role)));
                changed = true;
            }
        }
        if (changed)
            layout->setFormats(ranges);
    }
    doc->markContentsDirty(0, doc->characterCount());
}

static QTextCharFormat fmt(const Theme &theme, SyntaxHighlighter::Role role, bool bold = false,
                           bool italic = false)
{
    QTextCharFormat f;
    f.setForeground(roleColor(theme, role));
    f.setProperty(RoleProperty, int(role));
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

void SyntaxHighlighter::rebuildRules(bool rehighlightNow)
{
    m_rules.clear();
    m_blocks.clear();

    if (m_language.id == QLatin1String("text")) {
        if (rehighlightNow)
            rehighlight();
        return;
    }

    // A ordem importa: regras posteriores sobrescrevem as anteriores.
    // Chamadas de função: palavra seguida de "(". Não se aplica a markup/prosa
    // (HTML/XML/Markdown), onde "palavra (" é só texto. UseUnicodeProperties faz
    // \w/\b reconhecerem letras acentuadas (ex.: "Parâmetros" é uma palavra só).
    if (m_language.id != QLatin1String("html") && m_language.id != QLatin1String("xml")
        && m_language.id != QLatin1String("markdown")) {
        QRegularExpression fnRe(QStringLiteral("\\b[^\\W\\d]\\w*(?=\\s*\\()"),
                                QRegularExpression::UseUnicodePropertiesOption);
        m_rules.append({fnRe, fmt(m_theme, Role::Function)});
    }
    m_rules.append({QRegularExpression(
                        QStringLiteral("\\b(?:0[xX][0-9a-fA-F_]+|0[bB][01_]+|\\d[\\d_]*(?:\\.\\d+)?(?:[eE][+-]?\\d+)?)[uUlLfF]*\\b")),
                    fmt(m_theme, Role::Number)});
    // \b com UseUnicodeProperties: palavras acentuadas não casam parcialmente.
    const auto uni = QRegularExpression::UseUnicodePropertiesOption;
    if (!m_language.types.isEmpty())
        m_rules.append({QRegularExpression(wordAlternation(m_language.types), uni), fmt(m_theme, Role::Type)});
    if (!m_language.keywords.isEmpty())
        m_rules.append({QRegularExpression(wordAlternation(m_language.keywords), uni),
                        fmt(m_theme, Role::Keyword, true)});
    if (m_language.id == QLatin1String("c") || m_language.id == QLatin1String("cpp"))
        m_rules.append({QRegularExpression(QStringLiteral("^\\s*#\\s*\\w+.*$")),
                        fmt(m_theme, Role::Preprocessor)});
    if (m_language.id == QLatin1String("python"))
        m_rules.append({QRegularExpression(QStringLiteral("^\\s*@[\\w.]+")), fmt(m_theme, Role::Preprocessor)});
    if (m_language.id == QLatin1String("css"))
        m_rules.append({QRegularExpression(QStringLiteral("@[\\w-]+")), fmt(m_theme, Role::Preprocessor)}); // @media, @import…
    if (m_language.id == QLatin1String("cmake") || m_language.id == QLatin1String("makefile"))
        m_rules.append({QRegularExpression(QStringLiteral("\\$[({][A-Za-z_@<^*%?][\\w.-]*[)}]|\\$[@<^*%?]")),
                        fmt(m_theme, Role::Preprocessor)});
    if (m_language.id == QLatin1String("makefile")) // alvos: "nome:" no início da linha
        m_rules.append({QRegularExpression(QStringLiteral("^[\\w./%-]+(?=\\s*::?(?!=))")), fmt(m_theme, Role::Function)});
    if (m_language.id == QLatin1String("bash"))
        m_rules.append({QRegularExpression(QStringLiteral("\\$\\{?[A-Za-z_]\\w*\\}?")),
                        fmt(m_theme, Role::Preprocessor)});
    m_rules.append({QRegularExpression(QStringLiteral("\"(?:[^\"\\\\]|\\\\.)*\"|'(?:[^'\\\\]|\\\\.)*'")),
                    fmt(m_theme, Role::String)});
    if (m_language.id == QLatin1String("javascript") || m_language.id == QLatin1String("go")
        || m_language.id == QLatin1String("typescript"))
        m_rules.append({QRegularExpression(QStringLiteral("`[^`]*`")), fmt(m_theme, Role::String)});
    if (m_language.id == QLatin1String("markdown")) {
        // Realce básico de Markdown (sem keywords/strings no sentido usual).
        m_rules.append({QRegularExpression(QStringLiteral("`[^`]+`")), fmt(m_theme, Role::String)});
        m_rules.append({QRegularExpression(QStringLiteral("(?:\\*\\*|__)(?=\\S)(.+?)(?:\\*\\*|__)")),
                        fmt(m_theme, Role::Type, true)});
        m_rules.append({QRegularExpression(QStringLiteral("\\[[^\\]]+\\]\\([^)]+\\)")), fmt(m_theme, Role::Function)});
        m_rules.append({QRegularExpression(QStringLiteral("^#{1,6}\\s.*$")), fmt(m_theme, Role::Keyword, true)});
    }

    m_commentFormat = fmt(m_theme, Role::Comment, false, true);
    if (!m_language.blockCommentStart.isEmpty())
        m_blocks.append({m_language.blockCommentStart, m_language.blockCommentEnd});
    // Python: docstrings com ''' também são decoradas como comentário.
    if (m_language.id == QLatin1String("python"))
        m_blocks.append({QStringLiteral("'''"), QStringLiteral("'''")});
    if (rehighlightNow)
        rehighlight();
}

bool SyntaxHighlighter::inString(int pos) const
{
    return format(pos).intProperty(RoleProperty) == int(Role::String);
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
