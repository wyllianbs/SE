#include "Settings.h"
#include <algorithm>
#include "Language.h"
#include "LspManager.h"
#include "Sandbox.h"

#include <QProcess>
#include <QStandardPaths>

#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSettings>

namespace {
const QString kSettingsKey = QStringLiteral("settings/json");
const QString kProfilesGroup = QStringLiteral("profiles");
const QString kActiveProfileKey = QStringLiteral("settings/activeProfile");
}

QList<SymbolRule> AppSettings::defaultSymbols()
{
    // Operadores comuns; o usuário liga/desliga cada um e acrescenta os seus.
    return {
        {QStringLiteral("<="), QString::fromUtf8("\u2264"), true},   // ≤
        {QStringLiteral(">="), QString::fromUtf8("\u2265"), true},   // ≥
        {QStringLiteral("!="), QString::fromUtf8("\u2260"), true},   // ≠
        {QStringLiteral("=="), QString::fromUtf8("\u2A75"), true},   // ⩵
        {QStringLiteral("->"), QString::fromUtf8("\u2192"), true},   // →
        {QStringLiteral("<-"), QString::fromUtf8("\u2190"), true},   // ←
        {QStringLiteral("=>"), QString::fromUtf8("\u21D2"), true},   // ⇒
        {QStringLiteral("<=>"), QString::fromUtf8("\u21D4"), true},  // ⇔
        {QStringLiteral("::"), QString::fromUtf8("\u2237"), true},   // ∷
        {QStringLiteral("&&"), QString::fromUtf8("\u2227"), true},   // ∧
        {QStringLiteral("||"), QString::fromUtf8("\u2228"), true},   // ∨
        {QStringLiteral(">>"), QString::fromUtf8("\u226B"), true},   // ≫
        {QStringLiteral("<<"), QString::fromUtf8("\u226A"), true},   // ≪
        {QStringLiteral("..."), QString::fromUtf8("\u2026"), true},  // …
        {QStringLiteral("/="), QString::fromUtf8("\u2260"), true},   // ≠ (Haskell/Lua)
        {QStringLiteral("~="), QString::fromUtf8("\u2260"), true},   // ≠ (Lua)
    };
}

QList<SymbolRule> AppSettings::effectiveSymbols() const
{
    QList<SymbolRule> rules = symbols.isEmpty() ? defaultSymbols() : symbols;
    rules.removeIf([](const SymbolRule &r) { return !r.enabled || r.sequence.isEmpty() || r.symbol.isEmpty(); });
    // Mais longa primeiro: "<=>" casa antes de "<=".
    std::stable_sort(rules.begin(), rules.end(),
                     [](const SymbolRule &a, const SymbolRule &b) { return a.sequence.size() > b.sequence.size(); });
    return rules;
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
    if (id == QLatin1String("ruby"))
        return {QStringLiteral("ruby"), QString(), QStringLiteral("rb")};
    if (id == QLatin1String("perl"))
        return {QStringLiteral("perl"), QString(), QStringLiteral("pl pm")};
    if (id == QLatin1String("lua"))
        return {QStringLiteral("lua"), QString(), QStringLiteral("lua")};
    if (id == QLatin1String("php"))
        return {QStringLiteral("php"), QString(), QStringLiteral("php")};
    if (id == QLatin1String("r"))
        return {QStringLiteral("Rscript"), QString(), QStringLiteral("r R")};
    if (id == QLatin1String("julia"))
        return {QStringLiteral("julia"), QString(), QStringLiteral("jl")};
    if (id == QLatin1String("haskell"))
        return {QStringLiteral("runghc"), QString(), QStringLiteral("hs")};
    if (id == QLatin1String("fortran"))
        return {QStringLiteral("gfortran"), QStringLiteral("-Wall"), QStringLiteral("f90 f95 f03 f08 f")};
    if (id == QLatin1String("pascal"))
        return {QStringLiteral("fpc"), QStringLiteral("-O2"), QStringLiteral("pas pp")};
    // Linguagens sem toolchain (html, css, json, xml, markdown…): sem ferramenta,
    // mas herdam as extensões do registro de linguagens para serem reconhecidas.
    return {QString(), QString(), Language::byId(id).extensions.join(QLatin1Char(' '))};
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

bool AppSettings::languageInstalled(const QString &id) const
{
    const Language &lang = Language::byId(id);
    if (lang.id != id)
        return false;
    const QStringList tool = QProcess::splitCommand(languageConfig(id).tool);
    if (tool.isEmpty())
        return true; // linguagem só de realce (HTML, CSS, JSON, Markdown…): sempre disponível
    if (!Sandbox::hasExecutable(tool.first()))
        return false;
    for (const QString &r : lang.requires) {
        if (!Sandbox::hasExecutable(r))
            return false;
    }
    return true;
}

QStringList AppSettings::installedLanguages() const
{
    QStringList ids;
    for (const Language &lang : Language::all()) {
        if (languageInstalled(lang.id))
            ids << lang.id;
    }
    return ids;
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
    o[QStringLiteral("wrapIndent")] = wrapIndent;
    o[QStringLiteral("wrapIndentExtra")] = wrapIndentExtra;
    o[QStringLiteral("highlightCurrentLine")] = highlightCurrentLine;
    o[QStringLiteral("minimap")] = minimap;
    o[QStringLiteral("minimapWidth")] = minimapWidth;
    o[QStringLiteral("autoClosePairs")] = autoClosePairs;
    o[QStringLiteral("codeCompletion")] = codeCompletion;
    o[QStringLiteral("codeFolding")] = codeFolding;
    o[QStringLiteral("autoReload")] = autoReload;
    o[QStringLiteral("autoSave")] = autoSave;
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
    o[QStringLiteral("toolbarIconSize")] = toolbarIconSize;
    o[QStringLiteral("toolbarStyle")] = toolbarStyle;
    o[QStringLiteral("toolbarLocked")] = toolbarLocked;
    o[QStringLiteral("toolbarActions")] = QJsonArray::fromStringList(toolbarActions);
    o[QStringLiteral("prettySymbols")] = prettySymbols;
    QJsonArray symbolsArray;
    for (const SymbolRule &r : symbols)
        symbolsArray.append(QJsonObject{{"sequence", r.sequence}, {"symbol", r.symbol}, {"enabled", r.enabled}});
    o[QStringLiteral("symbols")] = symbolsArray;
    o[QStringLiteral("uiScale")] = uiScale;
    o[QStringLiteral("uiLanguage")] = uiLanguage;
    o[QStringLiteral("terminalFont")] = terminalFont;
    o[QStringLiteral("terminalFontSize")] = terminalFontSize;
    o[QStringLiteral("shell")] = shell;
    o[QStringLiteral("terminalCursorBlink")] = terminalCursorBlink;
    o[QStringLiteral("terminalTransparency")] = terminalTransparency;

    o[QStringLiteral("previewCommand")] = previewCommand;
    o[QStringLiteral("previewHost")] = previewHost;
    o[QStringLiteral("previewPort")] = previewPort;
    o[QStringLiteral("previewBrowser")] = previewBrowser;
    o[QStringLiteral("previewReload")] = previewReload;
    o[QStringLiteral("previewReloadInterval")] = previewReloadInterval;
    o[QStringLiteral("previewCrossOriginIsolation")] = previewCrossOriginIsolation;

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
    s.wrapIndent = flag("wrapIndent", s.wrapIndent);
    s.wrapIndentExtra = qBound(0, num("wrapIndentExtra", s.wrapIndentExtra), 32);
    s.highlightCurrentLine = flag("highlightCurrentLine", s.highlightCurrentLine);
    s.minimap = flag("minimap", s.minimap);
    s.minimapWidth = qBound(20, num("minimapWidth", s.minimapWidth), 400);
    s.autoClosePairs = flag("autoClosePairs", s.autoClosePairs);
    s.codeCompletion = flag("codeCompletion", s.codeCompletion);
    s.codeFolding = flag("codeFolding", s.codeFolding);
    s.autoReload = flag("autoReload", s.autoReload);
    s.autoSave = flag("autoSave", s.autoSave);
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
    // Configurações antigas: escala dos botões vira o tamanho mais próximo.
    const int legacy = qRound(18 * s.buttonScale / 100.0);
    const int fallbackSize = legacy <= 19 ? 16 : legacy <= 27 ? 22 : legacy <= 40 ? 32 : 48;
    s.toolbarIconSize = num("toolbarIconSize", o.contains(QLatin1String("buttonScale")) ? fallbackSize : s.toolbarIconSize);
    if (!QList<int>{16, 22, 32, 48}.contains(s.toolbarIconSize))
        s.toolbarIconSize = 22;
    s.toolbarStyle = qBound(0, num("toolbarStyle", s.toolbarStyle), 3);
    s.toolbarLocked = flag("toolbarLocked", s.toolbarLocked);
    for (const QJsonValue &v : o.value(QStringLiteral("toolbarActions")).toArray())
        s.toolbarActions << v.toString();
    s.prettySymbols = flag("prettySymbols", s.prettySymbols);
    if (o.contains(QLatin1String("symbols"))) {
        for (const QJsonValue &v : o.value(QStringLiteral("symbols")).toArray()) {
            const QJsonObject r = v.toObject();
            s.symbols.append({r.value("sequence").toString(), r.value("symbol").toString(),
                              r.value("enabled").toBool(true)});
        }
    }
    s.uiScale = qBound(80, num("uiScale", s.uiScale), 200);
    s.uiLanguage = str("uiLanguage", s.uiLanguage);
    s.terminalFont = str("terminalFont", s.terminalFont);
    s.terminalFontSize = qBound(6, num("terminalFontSize", s.terminalFontSize), 48);
    s.shell = str("shell", s.shell);
    s.terminalCursorBlink = flag("terminalCursorBlink", s.terminalCursorBlink);
    s.terminalTransparency = qBound(0, num("terminalTransparency", s.terminalTransparency), 90);

    s.previewCommand = str("previewCommand", s.previewCommand);
    s.previewHost = str("previewHost", s.previewHost);
    s.previewPort = qBound(1, num("previewPort", s.previewPort), 65535);
    s.previewBrowser = str("previewBrowser", s.previewBrowser);
    s.previewReload = qBound(0, num("previewReload", s.previewReload), 2);
    s.previewReloadInterval = qBound(1, num("previewReloadInterval", s.previewReloadInterval), 3600);
    s.previewCrossOriginIsolation = flag("previewCrossOriginIsolation", s.previewCrossOriginIsolation);

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
