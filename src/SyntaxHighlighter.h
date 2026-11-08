#pragma once

#include "Language.h"
#include "Theme.h"

#include <QHash>
#include <QList>
#include <QPair>
#include <QRegularExpression>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>

class SyntaxHighlighter : public QSyntaxHighlighter
{
    Q_OBJECT
public:
    explicit SyntaxHighlighter(QTextDocument *document);

    void setLanguage(const Language &language);
    void setTheme(const Theme &theme);
    /** Dicas inline (inlay hints) por bloco: (coluna, texto). Reserva o espaço
     * inline via letter-spacing, sem alterar o texto do documento. O CodeEditor
     * desenha o texto da dica nessa folga. */
    void setInlayCells(const QHash<int, QList<QPair<int, QString>>> &cellsByBlock);

    enum class Role { None, Function, Number, Type, Keyword, Preprocessor, String, Comment };

protected:
    void highlightBlock(const QString &text) override;

private:
    struct Rule {
        QRegularExpression pattern;
        QTextCharFormat format;
        // Grupo de captura usado para determinar o intervalo colorido (0 = match inteiro).
        int captureGroup = 0;
    };

    void rebuildRules(bool rehighlightNow);
    void recolor();
    bool inString(int pos) const;
    void applyInlaySpacing(const QString &text);

    Language m_language;
    Theme m_theme;
    QList<Rule> m_rules;
    QTextCharFormat m_commentFormat;
    /** Delimitadores de comentário de bloco (abre, fecha). */
    QList<QPair<QString, QString>> m_blocks;
    /** Dicas inline por número de bloco: (coluna, texto exibido). */
    QHash<int, QList<QPair<int, QString>>> m_inlayCells;
};
