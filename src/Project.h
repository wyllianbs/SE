#pragma once

#include <QList>
#include <QObject>
#include <QRegularExpression>
#include <QString>
#include <QStringList>

/**
 * Padrões no estilo do .gitignore (subconjunto): "#" comenta, "!" nega,
 * "/" no fim casa só pastas, "/" no meio ou no início ancora na raiz;
 * sem "/", o padrão casa o nome em qualquer nível. Curingas: * ? ** [...].
 * A última regra que casa decide (como no git).
 */
class IgnoreRules
{
public:
    void addPattern(const QString &pattern);
    void addPatterns(const QStringList &patterns);
    /** Lê um arquivo .gitignore (ausente = nada). */
    void loadFile(const QString &path);
    bool isEmpty() const { return m_rules.isEmpty(); }
    /** relPath: relativo à raiz, com "/"; testa só a própria entrada (as pastas pais são podadas antes). */
    bool matches(const QString &relPath, bool isDir) const;

private:
    struct Rule
    {
        QRegularExpression re;
        bool negate = false;
        bool dirOnly = false;
        bool anchored = false;
    };
    QList<Rule> m_rules;
};

/**
 * Projeto = uma pasta raiz aberta (como no VS Code / Kate). Sabe o que
 * esconder na árvore (lista de exclusões das configurações) e o que ignorar
 * no Quick Open e na busca (exclusões + .gitignore da raiz), e mantém em
 * cache a lista de arquivos, montada numa thread.
 */
class Project : public QObject
{
    Q_OBJECT
public:
    /** Limite da listagem: projetos maiores são truncados (e avisados). */
    static constexpr int kMaxFiles = 200000;

    Project(const QString &root, const QStringList &excludes, QObject *parent = nullptr);
    ~Project() override;

    QString root() const { return m_root; }
    QString name() const;
    bool contains(const QString &absPath) const;
    QString relativePath(const QString &absPath) const;
    QString absolutePath(const QString &relPath) const;

    /** Escondido na árvore: só a lista de exclusões. */
    bool isHidden(const QString &absPath, bool isDir) const;
    /** Fora do Quick Open e da busca: exclusões + .gitignore da raiz. */
    bool isIgnored(const QString &relPath, bool isDir) const;

    /** Relista os arquivos em segundo plano; emite filesListed() ao terminar. */
    void refreshFiles();
    bool filesReady() const { return m_filesReady; }
    bool filesTruncated() const { return m_truncated; }
    /** Arquivos (caminhos relativos, ordenados) da última listagem. */
    QStringList files() const { return m_files; }

    /** Varre a pasta (podando o que for ignorado). Usado na thread e nos testes. */
    static QStringList listFiles(const QString &root, const IgnoreRules &hidden, const IgnoreRules &ignored,
                                 int limit, bool *truncated = nullptr);

    /** Pontuação do Quick Open (busca aproximada por subsequência); -1 = não casa. */
    static int fuzzyScore(const QString &pattern, const QString &candidate);

    struct Match
    {
        QString file;   //< relativo à raiz
        int line = 0;   //< base 0
        int column = 0; //< base 0
        int length = 0;
        QString text;   //< a linha inteira
    };
    struct SearchOptions
    {
        bool caseSensitive = false;
        bool regex = false;
        bool wholeWord = false;
    };
    /** Busca o padrão nos arquivos (pula binários e arquivos grandes). Roda em qualquer thread. */
    static QList<Match> search(const QString &root, const QStringList &files, const QString &pattern,
                               SearchOptions options, int limit, bool *truncated = nullptr);
    static constexpr qint64 kMaxSearchFileSize = 4 * 1024 * 1024;

    /** Comandos de compilar/executar detectados pelo arquivo na raiz (CMake, Make, Cargo…). */
    struct BuildCommands
    {
        QString system; //< "CMake", "Make", "Cargo"…; vazio = nenhum detectado
        QString build;  //< vazio = não há passo de compilação
        QString run;    //< vazio = não se sabe o que executar
    };
    static BuildCommands detectBuild(const QString &root);

Q_SIGNALS:
    void filesListed();

private:
    QString m_root;
    IgnoreRules m_hidden;
    IgnoreRules m_ignored;
    QStringList m_files;
    bool m_filesReady = false;
    bool m_truncated = false;
    int m_generation = 0;
};
