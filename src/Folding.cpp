#include "Folding.h"

#include <QMap>
#include <QStringList>

namespace {

bool isBraceLanguage(const QString &id)
{
    static const QStringList ids{"c", "cpp", "javascript", "java", "rust", "go"};
    return ids.contains(id);
}

/** Índice logo após a string iniciada em start; string sem fechamento para no fim da linha. */
int skipString(const QString &text, int start, bool multiline)
{
    const QChar quote = text.at(start);
    int j = start + 1;
    while (j < text.size()) {
        const QChar ch = text.at(j);
        if (ch == QLatin1Char('\\')) {
            j += 2;
            continue;
        }
        if (ch == quote)
            return j + 1;
        if (ch == QLatin1Char('\n') && !multiline)
            return j;
        ++j;
    }
    return text.size();
}

QList<FoldRegion> detectBraces(const QString &text, bool backtickStrings, bool charLiterals)
{
    QList<int> openLines;
    QMap<int, FoldRegion> byHeader;
    int line = 0;
    const int n = text.size();
    int i = 0;
    while (i < n) {
        const QChar c = text.at(i);
        const QChar next = i + 1 < n ? text.at(i + 1) : QChar();
        if (c == QLatin1Char('\n')) {
            ++line;
        } else if (c == QLatin1Char('/') && next == QLatin1Char('/')) {
            const int e = text.indexOf(QLatin1Char('\n'), i);
            i = e < 0 ? n : e;
            continue;
        } else if (c == QLatin1Char('/') && next == QLatin1Char('*')) {
            const int e = text.indexOf(QLatin1String("*/"), i + 2);
            const int end = e < 0 ? n : e + 2;
            line += text.mid(i, end - i).count(QLatin1Char('\n'));
            i = end;
            continue;
        } else if (c == QLatin1Char('"') || (charLiterals && c == QLatin1Char('\''))
                   || (backtickStrings && c == QLatin1Char('`'))) {
            const int end = skipString(text, i, c == QLatin1Char('`'));
            line += text.mid(i, end - i).count(QLatin1Char('\n'));
            i = end;
            continue;
        } else if (c == QLatin1Char('{')) {
            openLines.append(line);
        } else if (c == QLatin1Char('}') && !openLines.isEmpty()) {
            const int header = openLines.takeLast();
            // O "}" fica visível numa linha própria só se houver algo antes dele;
            // o bloco dobra até a linha anterior ao "}" quando ele inicia a linha.
            int last = line;
            int k = i - 1;
            while (k >= 0 && (text.at(k) == QLatin1Char(' ') || text.at(k) == QLatin1Char('\t')))
                --k;
            if (k >= 0 && text.at(k) == QLatin1Char('\n'))
                --last;
            if (last > header) {
                const FoldRegion region{header, last};
                // Dois blocos abrindo na mesma linha: fica o externo.
                if (!byHeader.contains(header) || byHeader[header].lastLine < last)
                    byHeader[header] = region;
            }
        }
        ++i;
    }
    return byHeader.values();
}

int indentOf(const QString &line)
{
    int n = 0;
    for (QChar c : line) {
        if (c == QLatin1Char(' '))
            ++n;
        else if (c == QLatin1Char('\t'))
            n += 4;
        else
            break;
    }
    return n;
}

QList<FoldRegion> detectIndent(const QString &text)
{
    const QStringList lines = text.split(QLatin1Char('\n'));
    QList<FoldRegion> regions;
    for (int h = 0; h < lines.size(); ++h) {
        QString code = lines[h];
        const int hash = code.indexOf(QLatin1Char('#'));
        if (hash >= 0)
            code = code.left(hash);
        code = code.trimmed();
        if (!code.endsWith(QLatin1Char(':')))
            continue;
        const int base = indentOf(lines[h]);
        int last = h;
        for (int j = h + 1; j < lines.size(); ++j) {
            if (lines[j].trimmed().isEmpty())
                continue;
            if (indentOf(lines[j]) <= base)
                break;
            last = j;
        }
        if (last > h)
            regions.append({h, last});
    }
    return regions;
}

} // namespace

bool Folding::supports(const QString &id)
{
    return isBraceLanguage(id) || id == QLatin1String("python");
}

QList<FoldRegion> Folding::detect(const QString &text, const QString &id)
{
    if (id == QLatin1String("python"))
        return detectIndent(text);
    if (isBraceLanguage(id))
        return detectBraces(text, id == QLatin1String("javascript") || id == QLatin1String("go"),
                            id != QLatin1String("rust")); // em Rust, ' também marca lifetimes
    return {};
}
