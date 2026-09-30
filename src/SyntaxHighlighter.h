#pragma once

#include "Language.h"
#include "Theme.h"

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

protected:
    void highlightBlock(const QString &text) override;

private:
    struct Rule {
        QRegularExpression pattern;
        QTextCharFormat format;
    };

    void rebuildRules();
    bool inString(int pos) const;

    Language m_language;
    Theme m_theme;
    QList<Rule> m_rules;
    QTextCharFormat m_commentFormat;
    QRegularExpression m_lineComment;
    QRegularExpression m_blockStart;
    QRegularExpression m_blockEnd;
};
