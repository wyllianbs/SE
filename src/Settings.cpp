#include "Settings.h"
#include "LspManager.h"

#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSettings>

namespace {
const QString kSettingsKey = QStringLiteral("settings/json");
const QString kProfilesGroup = QStringLiteral("profiles");
const QString kActiveProfileKey = QStringLiteral("settings/activeProfile");
}

LanguageConfig AppSettings::defaultLanguageConfig(const QString &id)
{
    if (id == QLatin1String("c"))
        return {QStringLiteral("gcc"), QStringLiteral("-std=c23 -Wall -Wextra -Wl,--no-as-needed -lm"),
                QStringLiteral("c h")};
    if (id == QLatin1String("cpp"))
        return {QStringLiteral("g++"), QStringLiteral("-std=c++23 -Wall -Wextra -Wl,--no-as-needed -lm"),
                QStringLiteral("cpp cc cxx hpp hh hxx")};
    if (id == QLatin1String("python"))
        return {QStringLiteral("python3"), QString(), QStringLiteral("py pyw")};
    if (id == QLatin1String("rust"))
        return {QStringLiteral("rustc"), QStringLiteral("-O"), QStringLiteral("rs")};
    if (id == QLatin1String("go"))
        return {QStringLiteral("go"), QString(), QStringLiteral("go")};
    if (id == QLatin1String("javascript"))
        return {QStringLiteral("node"), QString(), QStringLiteral("js mjs cjs")};
    if (id == QLatin1String("java"))
        return {QStringLiteral("java"), QString(), QStringLiteral("java")};
    if (id == QLatin1String("bash"))
        return {QStringLiteral("bash"), QString(), QStringLiteral("sh bash")};
    return {};
}

AppSettings AppSettings::defaults()
{
    return AppSettings();
}

LanguageConfig AppSettings::languageConfig(const QString &id) const
{
    const LanguageConfig def = defaultLanguageConfig(id);
    LanguageConfig cfg = languages.value(id, def);
    if (cfg.tool.trimmed().isEmpty())
        cfg.tool = def.tool;
    if (cfg.extensions.trimmed().isEmpty())
        cfg.extensions = def.extensions;
    return cfg;
}

LspConfig AppSettings::lspConfig(const QString &id) const
{
    LspConfig cfg = lsp.value(id);
    if (cfg.command.trimmed().isEmpty()) {
        const QList<LspPreset> presets = LspManager::presets(id);
        if (!presets.isEmpty())
            cfg.command = presets.first().command;
    }
    return cfg;
}

QJsonObject AppSettings::toJson() const
{
    QJsonObject o;
    o[QStringLiteral("editorFont")] = editorFont;
    o[QStringLiteral("editorFontSize")] = editorFontSize;
    o[QStringLiteral("tabWidth")] = tabWidth;
    o[QStringLiteral("lineNumbers")] = lineNumbers;
    o[QStringLiteral("wordWrap")] = wordWrap;
    o[QStringLiteral("highlightCurrentLine")] = highlightCurrentLine;
    o[QStringLiteral("autoClosePairs")] = autoClosePairs;
    o[QStringLiteral("codeCompletion")] = codeCompletion;
    o[QStringLiteral("codeFolding")] = codeFolding;
    o[QStringLiteral("lspEnabled")] = lspEnabled;
    QJsonObject lspObj;
    for (auto it = lsp.cbegin(); it != lsp.cend(); ++it)
        lspObj[it.key()] = QJsonObject{{"enabled", it->enabled}, {"command", it->command}};
    o[QStringLiteral("lsp")] = lspObj;
    o[QStringLiteral("userThemes")] = userThemes;
    o[QStringLiteral("insertSpaces")] = insertSpaces;
    o[QStringLiteral("editorCursorBlink")] = editorCursorBlink;
    o[QStringLiteral("theme")] = theme;
    o[QStringLiteral("themeFollowSystem")] = themeFollowSystem;
    o[QStringLiteral("buttonScale")] = buttonScale;
    o[QStringLiteral("uiScale")] = uiScale;
    o[QStringLiteral("uiLanguage")] = uiLanguage;
    o[QStringLiteral("terminalFont")] = terminalFont;
    o[QStringLiteral("terminalFontSize")] = terminalFontSize;
    o[QStringLiteral("shell")] = shell;
    o[QStringLiteral("terminalCursorBlink")] = terminalCursorBlink;
    o[QStringLiteral("terminalTransparency")] = terminalTransparency;

    QJsonObject langs;
    for (auto it = languages.cbegin(); it != languages.cend(); ++it) {
        langs[it.key()] = QJsonObject{{"tool", it->tool}, {"flags", it->flags}, {"extensions", it->extensions}};
    }
    o[QStringLiteral("languages")] = langs;

    QJsonArray custom;
    for (const CustomLanguage &c : customLanguages) {
        custom.append(QJsonObject{{"name", c.name}, {"tool", c.tool}, {"flags", c.flags},
                                  {"extensions", c.extensions}});
    }
    o[QStringLiteral("customLanguages")] = custom;
    return o;
}

AppSettings AppSettings::fromJson(const QJsonObject &o)
{
    AppSettings s;
    auto str = [&o](const char *k, const QString &d) { return o.value(QLatin1String(k)).toString(d); };
    auto num = [&o](const char *k, int d) { return o.value(QLatin1String(k)).toInt(d); };
    auto flag = [&o](const char *k, bool d) { return o.value(QLatin1String(k)).toBool(d); };

    s.editorFont = str("editorFont", s.editorFont);
    s.editorFontSize = qBound(6, num("editorFontSize", s.editorFontSize), 48);
    s.tabWidth = qBound(1, num("tabWidth", s.tabWidth), 16);
    s.lineNumbers = flag("lineNumbers", s.lineNumbers);
    s.wordWrap = flag("wordWrap", s.wordWrap);
    s.highlightCurrentLine = flag("highlightCurrentLine", s.highlightCurrentLine);
    s.autoClosePairs = flag("autoClosePairs", s.autoClosePairs);
    s.codeCompletion = flag("codeCompletion", s.codeCompletion);
    s.codeFolding = flag("codeFolding", s.codeFolding);
    s.lspEnabled = flag("lspEnabled", s.lspEnabled);
    s.userThemes = o.value(QStringLiteral("userThemes")).toArray();
    const QJsonObject lspObj = o.value(QStringLiteral("lsp")).toObject();
    for (auto it = lspObj.begin(); it != lspObj.end(); ++it) {
        const QJsonObject l = it.value().toObject();
        s.lsp.insert(it.key(), {l.value("enabled").toBool(true), l.value("command").toString()});
    }
    s.insertSpaces = flag("insertSpaces", s.insertSpaces);
    s.editorCursorBlink = flag("editorCursorBlink", s.editorCursorBlink);
    s.theme = str("theme", s.theme);
    s.themeFollowSystem = flag("themeFollowSystem", s.themeFollowSystem);
    s.buttonScale = qBound(80, num("buttonScale", s.buttonScale), 200);
    s.uiScale = qBound(80, num("uiScale", s.uiScale), 200);
    s.uiLanguage = str("uiLanguage", s.uiLanguage);
    s.terminalFont = str("terminalFont", s.terminalFont);
    s.terminalFontSize = qBound(6, num("terminalFontSize", s.terminalFontSize), 48);
    s.shell = str("shell", s.shell);
    s.terminalCursorBlink = flag("terminalCursorBlink", s.terminalCursorBlink);
    s.terminalTransparency = qBound(0, num("terminalTransparency", s.terminalTransparency), 90);

    const QJsonObject langs = o.value(QStringLiteral("languages")).toObject();
    for (auto it = langs.begin(); it != langs.end(); ++it) {
        const QJsonObject l = it.value().toObject();
        s.languages.insert(it.key(), {l.value("tool").toString(), l.value("flags").toString(),
                                      l.value("extensions").toString()});
    }
    for (const QJsonValue &v : o.value(QStringLiteral("customLanguages")).toArray()) {
        const QJsonObject c = v.toObject();
        CustomLanguage cl{c.value("name").toString().trimmed(), c.value("tool").toString().trimmed(),
                          c.value("flags").toString(), c.value("extensions").toString()};
        if (!cl.name.isEmpty())
            s.customLanguages.append(cl);
    }
    return s;
}

AppSettings AppSettings::load()
{
    QSettings qs;
    const QByteArray raw = qs.value(kSettingsKey).toByteArray();
    if (raw.isEmpty()) {
        // Migração da versão 0.1: tema e fonte ficavam em chaves soltas.
        AppSettings s;
        s.theme = qs.value(QStringLiteral("ui/theme"), s.theme).toString();
        s.editorFontSize = qs.value(QStringLiteral("editor/fontSize"), s.editorFontSize).toInt();
        s.terminalFontSize = s.editorFontSize;
        return s;
    }
    return fromJson(QJsonDocument::fromJson(raw).object());
}

void AppSettings::save() const
{
    QSettings qs;
    qs.setValue(kSettingsKey, QJsonDocument(toJson()).toJson(QJsonDocument::Compact));
}

QStringList AppSettings::profileNames()
{
    QSettings qs;
    qs.beginGroup(kProfilesGroup);
    QStringList names;
    for (const QString &key : qs.childKeys())
        names << QByteArray::fromPercentEncoding(key.toUtf8());
    names.sort(Qt::CaseInsensitive);
    return names;
}

static QString profileKey(const QString &name)
{
    return kProfilesGroup + QLatin1Char('/') + QString::fromLatin1(name.toUtf8().toPercentEncoding());
}

void AppSettings::saveProfile(const QString &name, const AppSettings &settings)
{
    QSettings qs;
    qs.setValue(profileKey(name), QJsonDocument(settings.toJson()).toJson(QJsonDocument::Compact));
}

std::optional<AppSettings> AppSettings::loadProfile(const QString &name)
{
    QSettings qs;
    const QByteArray raw = qs.value(profileKey(name)).toByteArray();
    if (raw.isEmpty())
        return std::nullopt;
    return fromJson(QJsonDocument::fromJson(raw).object());
}

void AppSettings::deleteProfile(const QString &name)
{
    QSettings qs;
    qs.remove(profileKey(name));
    if (activeProfile() == name)
        setActiveProfile(QString());
}

QString AppSettings::activeProfile()
{
    return QSettings().value(kActiveProfileKey).toString();
}

void AppSettings::setActiveProfile(const QString &name)
{
    QSettings().setValue(kActiveProfileKey, name);
}
