#include "Sandbox.h"

#include <QFileInfo>
#include <QHash>
#include <QProcess>
#include <QStandardPaths>

#include <unistd.h>

namespace {

const QString kSpawn = QStringLiteral("flatpak-spawn");

bool detectFlatpak()
{
    // O runtime do Flatpak cria /.flatpak-info e define FLATPAK_ID.
    return QFileInfo::exists(QStringLiteral("/.flatpak-info"))
           || !qEnvironmentVariable("FLATPAK_ID").isEmpty();
}

QHash<QString, QString> &executableCache()
{
    static QHash<QString, QString> cache;
    return cache;
}

} // namespace

bool Sandbox::inFlatpak()
{
    static const bool value = detectFlatpak();
    return value;
}

QPair<QString, QStringList> Sandbox::command(const QString &program, const QStringList &args)
{
    if (!inFlatpak())
        return {program, args};
    // --watch-bus encerra o processo no hospedeiro se o app fechar.
    QStringList full{QStringLiteral("--host"), QStringLiteral("--watch-bus"), program};
    full += args;
    return {kSpawn, full};
}

QStringList Sandbox::wrap(const QString &program, const QStringList &args)
{
    const auto [p, a] = command(program, args);
    return QStringList{p} + a;
}

QString Sandbox::findExecutable(const QString &name)
{
    if (name.isEmpty())
        return QString();
    if (!inFlatpak())
        return QStandardPaths::findExecutable(name);

    auto &cache = executableCache();
    const auto it = cache.constFind(name);
    if (it != cache.cend())
        return it.value();

    // "command -v" no shell do hospedeiro usa o PATH de lá.
    QProcess probe;
    probe.start(kSpawn, {QStringLiteral("--host"), QStringLiteral("sh"), QStringLiteral("-c"),
                         QStringLiteral("command -v %1").arg(QString(name).replace(QLatin1Char('\''), QString()))});
    QString path;
    if (probe.waitForFinished(5000) && probe.exitStatus() == QProcess::NormalExit && probe.exitCode() == 0)
        path = QString::fromLocal8Bit(probe.readAllStandardOutput()).trimmed().section(QLatin1Char('\n'), 0, 0);
    cache.insert(name, path);
    return path;
}

bool Sandbox::hasExecutable(const QString &name)
{
    return !findExecutable(name).isEmpty();
}

QPair<QString, QStringList> Sandbox::hostTerminalCommand(const QString &shell)
{
    if (!inFlatpak())
        return {shell, {}};
    // flatpak-spawn --host não dá terminal de controle ao processo: um shell
    // interativo avisa e perde o job control. "script" aloca um pty no
    // hospedeiro, devolvendo um shell completo (Ctrl+Z, fg/bg funcionam).
    const QString sh = shell.isEmpty() ? hostLoginShell() : shell;
    if (!hasExecutable(QStringLiteral("script")))
        return command(sh); // sem util-linux: shell direto (com o aviso)
    QStringList args{QStringLiteral("--host"), QStringLiteral("--watch-bus"),
                     QStringLiteral("--env=TERM=xterm-256color"), QStringLiteral("script"),
                     QStringLiteral("-qec"), sh + QStringLiteral(" -l"), QStringLiteral("/dev/null")};
    return {kSpawn, args};
}

QString Sandbox::hostLoginShell()
{
    if (!inFlatpak())
        return QString();
    static const QString shell = []() -> QString {
        // Shell de login do usuário no hospedeiro (campo 7 do passwd).
        QProcess probe;
        probe.start(kSpawn, {QStringLiteral("--host"), QStringLiteral("getent"), QStringLiteral("passwd"),
                             QString::number(::getuid())});
        if (probe.waitForFinished(5000) && probe.exitCode() == 0) {
            const QString line = QString::fromLocal8Bit(probe.readAllStandardOutput()).trimmed();
            const QString sh = line.section(QLatin1Char(':'), 6, 6).trimmed();
            if (!sh.isEmpty())
                return sh;
        }
        return QStringLiteral("/bin/bash");
    }();
    return shell;
}
