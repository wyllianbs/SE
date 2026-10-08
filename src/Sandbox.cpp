#include "Sandbox.h"

#include <QFileInfo>
#include <QHash>
#include <QProcess>
#include <QStandardPaths>

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
