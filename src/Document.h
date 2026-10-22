#pragma once

#include "Folding.h"
#include "Language.h"
#include "Theme.h"

#include <QDateTime>
#include <QObject>
#include <QSet>

class QTextDocument;
class QTimer;

/** Diagnóstico do servidor LSP (posições em unidades UTF-16, base 0). */
struct Diagnostic
{
    int line = 0;
    int column = 0;
    int endLine = 0;
    int endColumn = 0;
    int severity = 1; //< 1 erro, 2 aviso, 3 informação, 4 dica
    QString message;
    QString source;
};
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
    const QString &filePath() const { return m_filePath; }
    void setFilePath(const QString &path);
    QString displayName() const;
    /** Nome proposto a um arquivo novo (ainda sem caminho), ex.: "hello.c". */
    const QString &suggestedName() const { return m_suggestedName; }
    void setSuggestedName(const QString &name) { m_suggestedName = name; Q_EMIT changed(); }
    bool isModified() const;
    /** O texto atual passa a ser o "salvo" (sem marca de alterado). */
    void markClean();
    bool isUntitledAndEmpty() const;

    /**
     * Modo desempenho: 0 = desligado, 1 = automático (arquivo com mais de `lines`
     * linhas, `bytes` bytes ou uma linha de mais de 10 000 caracteres ao abrir), 2 = sempre. Em vigor, desliga o realce e
     * o dobramento (e os editores, os recursos visuais caros). Como no VS Code,
     * a decisão vale até o arquivo ser reaberto.
     */
    void setLargeFileLimits(int mode, int lines, qint64 bytes);
    bool isLargeFile() const { return m_largeFile; }

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
    // Comandos do Kate (Exibir → Dobramento de código).
    /** Dobra/desdobra o bloco mais interno que contém a linha. */
    void toggleCurrentNode(int line);
    /** Alterna os blocos dentro do bloco atual (todos dobram, ou todos abrem). */
    void toggleContainedNodes(int line);
    /** Blocos que não estão dentro de outro (funções, classes…). */
    void setToplevelFolded(bool folded);
    /** Desdobra o que esconder a linha (o cursor entrou nela). */
    void revealLine(int line);

    // Depuração: pontos de parada (linhas base 0) e linha em execução (-1 = nenhuma).
    const QSet<int> &breakpoints() const { return m_breakpoints; }
    void toggleBreakpoint(int line);
    void clearBreakpoints();
    int executionLine() const { return m_executionLine; }
    void setExecutionLine(int line);

    const QList<Diagnostic> &diagnostics() const { return m_diagnostics; }
    void setDiagnostics(const QList<Diagnostic> &diagnostics);
    /** Diagnósticos que tocam a linha, do mais grave para o menos. */
    QList<Diagnostic> diagnosticsAt(int line) const;

    /** Nome da codificação ("UTF-8", "ISO-8859-15"…) e se o arquivo leva BOM. */
    const QString &encoding() const { return m_encoding; }
    bool hasBom() const { return m_bom; }
    /** O arquivo usa CRLF (Windows)? O editor trabalha com LF; ao salvar, volta a CRLF. */
    bool usesCrlf() const { return m_crlf; }
    /** Texto como vai para o disco (com o final de linha original). */
    QString diskText() const;
    void setEncoding(const QString &encoding, bool bom);
    QString encodingLabel() const;

    /** encoding vazio = detectar (BOM, UTF-8 válido, senão Windows-1252). */
    bool load(const QString &path, QString *error = nullptr, const QString &encoding = QString());

    // Alterações feitas por outros programas no arquivo.
    /** Com auto, um arquivo sem alterações locais é relido sozinho. */
    void setAutoReload(bool enabled) { m_autoReload = enabled; }
    /** Relê do disco na mesma codificação (as visões mantêm cursor e rolagem). */
    bool reload(QString *error = nullptr);
    /** Aceita a versão do disco como "vista" (botão Ignorar). */
    void acknowledgeDiskChange();
    bool save(const QString &path, QString *error = nullptr);

Q_SIGNALS:
    void largeFileChanged(bool large);
    /** Nome, caminho, linguagem ou estado de modificação mudaram. */
    void changed();
    void foldingChanged();
    void diagnosticsChanged();
    void breakpointsChanged();
    void executionLineChanged();
    /** O arquivo mudou no disco e não foi relido (alterações locais, ou auto desligado). */
    void changedOnDisk(bool deleted);
    void aboutToReload();
    void reloaded();

private:
    void setLargeFile(bool large);
    bool exceedsLimits(qint64 bytes, int lines, int longestLine) const;
    int m_perfMode = 0;
    int m_perfLines = 50000;
    qint64 m_perfBytes = 5 * 1024 * 1024;
    bool m_largeFile = false;
    QTextDocument *m_text;
    SyntaxHighlighter *m_highlighter;
    Language m_language;
    QString m_filePath;
    QString m_suggestedName;
    QString m_cleanText;  //< texto do último salvamento/carregamento
    bool m_modified = false; //< rastreado sem usar QTextDocument::setModified
    QString m_encoding = QStringLiteral("UTF-8");
    bool m_bom = false;
    bool m_crlf = false;
    QList<Diagnostic> m_diagnostics;
    QSet<int> m_breakpoints;
    int m_executionLine = -1;
    void watch();
    void onFileChanged();
    void rememberDiskState();
    class QFileSystemWatcher *m_watcher = nullptr;
    QTimer *m_changeTimer = nullptr;
    qint64 m_diskSize = -1;
    QDateTime m_diskTime;
    bool m_autoReload = false;
    void recomputeFolds();
    void applyFolding();
    QList<FoldRegion> m_regions;
    QSet<int> m_folded;
    bool m_foldingEnabled = true;
    int m_lastBlockCount = 1;
    QTimer *m_foldTimer;
};
