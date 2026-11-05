#pragma once

#include <QRegularExpression>
#include <QString>
#include <QStringList>

/** Linguagem suportada: realce de sintaxe e comando de execução. */
struct Language
{
    QString id;           //< "c", "cpp", "python"…
    QString displayName;
    QStringList extensions;
    QStringList keywords;
    QStringList types;
    QString lineComment;
    QString blockCommentStart;
    QString blockCommentEnd;
    /**
     * Modelo do comando de execução. Placeholders: {tool}, {flags}, %f arquivo,
     * %b nome sem extensão, %d diretório. Vazio = não executável.
     */
    QString runCommand;
    /** Execução interativa (REPL após o programa); vazio = igual a runCommand. */
    QString interactiveCommand;
    /** Programa "Hello, World!" de um arquivo novo. */
    QString helloWorld;
    /** Nome sugerido para um arquivo novo ("hello.c", "Hello.java"…). */
    QString newFileName;
    /** Ferramentas além da {tool} que precisam existir (ex.: javac para Java). */
    QStringList requiredTools;
    /**
     * Padrões de recuo inspirados nas indentationRules do VSCode/VSCodium.
     * increaseIndentPattern: casado contra a linha atual → próxima linha recebe +1 nível.
     * decreaseIndentPattern: casado contra a linha atual (incluindo o caractere recém-digitado)
     *   → a linha é automaticamente recuada -1 nível ao ser digitada.
     */
    QRegularExpression increaseIndentPattern;
    QRegularExpression decreaseIndentPattern;

    static const QList<Language> &all();
    static const Language &plainText();
    static const Language &forFileName(const QString &fileName);
    static const Language &byId(const QString &id);
    /** O depurador embutido suporta esta linguagem (Python, C, C++)? */
    static bool debuggable(const QString &id);

    /** Arquivos reconhecidos pelo nome (CMakeLists.txt, Makefile…): id da linguagem, ou vazio. */
    static QString idForSpecialFileName(const QString &fileName);
    bool isCustom() const { return id.startsWith(QLatin1String("custom:")); }

    /** A linguagem compila um binário em /tmp/slayer_out (C/C++, Rust, Pascal…)? */
    bool isCompiled() const;
    /** Monta o comando para um arquivo, já com aspas de shell. runAfterBuild=false
     * nas compiladas: só compila, sem executar o binário. */
    QString commandFor(const QString &filePath, const QString &tool, const QString &flags,
                       bool interactive = false, bool runAfterBuild = true) const;

    static Language custom(const QString &name, const QStringList &extensions, bool compiled = false);
    /** Hello, World! para um interpretador/compilador pelo nome (linguagens personalizadas). */
    static QString helloWorldForTool(const QString &tool);
    static QStringList splitExtensions(const QString &text);
};
