#pragma once

#include <QPair>
#include <QString>
#include <QStringList>

/**
 * Execução de processos ciente do sandbox Flatpak. Dentro de um Flatpak, as
 * ferramentas do usuário (shell, compiladores, gdb, servidores LSP) não estão
 * no sandbox: são executadas no sistema do hospedeiro via "flatpak-spawn
 * --host". Fora do Flatpak, tudo passa direto, sem alteração.
 */
namespace Sandbox {

/** Rodando dentro de um Flatpak? (detecção em cache) */
bool inFlatpak();

/**
 * (programa, argumentos) para rodar o comando no hospedeiro quando em Flatpak.
 * Fora do sandbox, devolve o próprio (program, args).
 */
QPair<QString, QStringList> command(const QString &program, const QStringList &args = {});

/** Só o argv achatado, útil para quem monta a linha de comando como texto. */
QStringList wrap(const QString &program, const QStringList &args = {});

/**
 * Caminho do executável, procurando no PATH do hospedeiro quando em Flatpak
 * (resultado em cache). Vazio se não existir.
 */
QString findExecutable(const QString &name);
bool hasExecutable(const QString &name);

/** Shell de login do usuário no hospedeiro (só em Flatpak; vazio fora dele). */
QString hostLoginShell();

/**
 * (programa, args) para abrir o shell no terminal. Em Flatpak, usa "script"
 * no hospedeiro para ter um pty de verdade (job control, sem avisos). Fora do
 * Flatpak, devolve o próprio shell.
 */
QPair<QString, QStringList> hostTerminalCommand(const QString &shell);

} // namespace Sandbox
