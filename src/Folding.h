#pragma once

#include <QList>
#include <QString>

/** Bloco dobrável: headerLine continua visível; headerLine+1..lastLine somem (base 0). */
struct FoldRegion
{
    int headerLine = 0;
    int lastLine = 0;
    bool operator==(const FoldRegion &o) const { return headerLine == o.headerLine && lastLine == o.lastLine; }
};

/** Detecção de blocos dobráveis (lógica pura, como a do Slayer Editor Android). */
namespace Folding {

bool supports(const QString &languageId);
/** Um bloco por linha de cabeçalho, ordenado pelo cabeçalho. */
QList<FoldRegion> detect(const QString &text, const QString &languageId);

} // namespace Folding
