#pragma once

#include "Language.h"
#include "Theme.h"

#include <QObject>

class QTextDocument;
class SyntaxHighlighter;

/**
 * Um arquivo aberto: texto, caminho, linguagem e realce. Pode ser exibido por
 * várias visões (CodeEditor) ao mesmo tempo, em espaços de visão divididos.
 */
class Document : public QObject
{
    Q_OBJECT
public:
    explicit Document(QObject *parent = nullptr);

    QTextDocument *text() const { return m_text; }
    QString filePath() const { return m_filePath; }
    void setFilePath(const QString &path);
    QString displayName() const;
    bool isModified() const;
    bool isUntitledAndEmpty() const;

    const Language &language() const { return m_language; }
    void setLanguage(const Language &language);
    void setTheme(const Theme &theme);

    bool load(const QString &path, QString *error = nullptr);
    bool save(const QString &path, QString *error = nullptr);

Q_SIGNALS:
    /** Nome, caminho, linguagem ou estado de modificação mudaram. */
    void changed();

private:
    QTextDocument *m_text;
    SyntaxHighlighter *m_highlighter;
    Language m_language;
    QString m_filePath;
};
