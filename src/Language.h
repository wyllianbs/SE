#pragma once

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
    QStringList requires;

    static const QList<Language> &all();
    static const Language &plainText();
    static const Language &forFileName(const QString &fileName);
    static const Language &byId(const QString &id);
    /** O depurador embutido suporta esta linguagem (Python, C, C++)? */
    static bool debuggable(const QString &id);

    bool isCustom() const { return id.startsWith(QLatin1String("custom:")); }

    /** Monta o comando para um arquivo, já com aspas de shell. */
    QString commandFor(const QString &filePath, const QString &tool, const QString &flags,
                       bool interactive = false) const;

    static Language custom(const QString &name, const QStringList &extensions);
    /** Hello, World! para um interpretador/compilador pelo nome (linguagens personalizadas). */
    static QString helloWorldForTool(const QString &tool);
    static QStringList splitExtensions(const QString &text);
};
