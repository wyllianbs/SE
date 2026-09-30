#pragma once

#include "Folding.h"
#include "Language.h"
#include "Theme.h"

#include <QObject>
#include <QSet>

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

    // Dobramento: compartilhado por todas as visões do documento.
    void setFoldingEnabled(bool enabled);
    bool foldingEnabled() const { return m_foldingEnabled; }
    const QList<FoldRegion> &foldRegions() const { return m_regions; }
    /** Região cujo cabeçalho é a linha dada, ou nullptr. */
    const FoldRegion *regionAt(int headerLine) const;
    bool isFolded(int headerLine) const { return m_folded.contains(headerLine); }
    void toggleFold(int headerLine);
    void setFolded(int headerLine, bool folded);
    void foldAll();
    void unfoldAll();
    /** Desdobra o que esconder a linha (o cursor entrou nela). */
    void revealLine(int line);

    /** Nome da codificação ("UTF-8", "ISO-8859-15"…) e se o arquivo leva BOM. */
    QString encoding() const { return m_encoding; }
    bool hasBom() const { return m_bom; }
    void setEncoding(const QString &encoding, bool bom);
    QString encodingLabel() const;

    /** encoding vazio = detectar (BOM, UTF-8 válido, senão Windows-1252). */
    bool load(const QString &path, QString *error = nullptr, const QString &encoding = QString());
    bool save(const QString &path, QString *error = nullptr);

Q_SIGNALS:
    /** Nome, caminho, linguagem ou estado de modificação mudaram. */
    void changed();
    void foldingChanged();

private:
    QTextDocument *m_text;
    SyntaxHighlighter *m_highlighter;
    Language m_language;
    QString m_filePath;
    QString m_encoding = QStringLiteral("UTF-8");
    bool m_bom = false;
    void recomputeFolds();
    void applyFolding();
    QList<FoldRegion> m_regions;
    QSet<int> m_folded;
    bool m_foldingEnabled = true;
    int m_lastBlockCount = 1;
    class QTimer *m_foldTimer;
};
