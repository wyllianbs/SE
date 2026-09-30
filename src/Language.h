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
     * Comando de execução. Placeholders: %f arquivo, %b nome sem extensão,
     * %d diretório. Vazio = não executável.
     */
    QString runCommand;

    static const QList<Language> &all();
    static const Language &plainText();
    static const Language &forFileName(const QString &fileName);
    static const Language &byId(const QString &id);

    /** Monta o comando para um arquivo, já com aspas de shell. */
    QString commandFor(const QString &filePath) const;
};
