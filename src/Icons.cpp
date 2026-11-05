#include "Icons.h"

#include <QBuffer>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QHash>
#include <QPainter>
#include <QPixmap>
#include <QUrl>

namespace {
QString s_symbols = QStringLiteral("Symbols Nerd Font Mono");
QString s_mono = QStringLiteral("Hack"); //< padrão do editor e do terminal (pacote fonts-hack)

struct IconKey {
    Icons::Glyph glyph;
    QRgb color;
    int size;
    bool operator==(const IconKey &o) const
    { return glyph == o.glyph && color == o.color && size == o.size; }
};
size_t qHash(const IconKey &k, size_t seed = 0)
{
    return qHashMulti(seed, quint32(k.glyph), k.color, k.size);
}
QHash<IconKey, QIcon> &iconCache()
{
    static QHash<IconKey, QIcon> cache;
    return cache;
}
}

void Icons::loadFonts()
{
    // Só a Symbols Nerd Font é embutida (desenha os ícones da interface); não
    // está no Debian. As fontes de texto vêm do sistema: Hack (fonts-hack, o
    // padrão) e JetBrains Mono (fonts-jetbrains-mono, opcional nas Configurações).
    // O Qt usa a Symbols como fallback para glifos ausentes nelas.
    const int id = QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/SymbolsNerdFontMono-Regular.ttf"));
    const QStringList families = QFontDatabase::applicationFontFamilies(id);
    if (!families.isEmpty())
        s_symbols = families.first();
}

QString Icons::symbolFamily() { return s_symbols; }

QFont Icons::editorFont(const QString &family, int pointSize)
{
    QFont f(family, pointSize);
    f.setStyleHint(QFont::Monospace);
    // Desliga as ligaduras OpenType (contextuais e padrão): cada caractere
    // digitado é mostrado como é, sem virar outro símbolo.
    f.setFeature("calt", 0);
    f.setFeature("liga", 0);
    f.setFeature("clig", 0);
    f.setFeature("dlig", 0);
    return f;
}
QString Icons::monoFamily() { return s_mono; }

QString Icons::glyphText(Glyph glyph)
{
    const char32_t cp = glyph;
    return QString::fromUcs4(&cp, 1);
}

static QPixmap render(Icons::Glyph glyph, const QColor &color, int size, qreal dpr)
{
    QPixmap pm(QSize(size, size) * dpr);
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::TextAntialiasing);
    QFont f(Icons::symbolFamily());
    f.setPixelSize(qRound(size * 0.9));
    p.setFont(f);
    p.setPen(color);
    p.drawText(QRect(0, 0, size, size), Qt::AlignCenter, Icons::glyphText(glyph));
    return pm;
}

QIcon Icons::icon(Glyph glyph, const QColor &color, int size)
{
    const IconKey key{glyph, color.rgba(), size};
    auto &cache = iconCache();
    if (const auto it = cache.constFind(key); it != cache.cend())
        return it.value();

    const qreal dpr = qMax<qreal>(1.0, qApp->devicePixelRatio());
    QIcon ic;
    ic.addPixmap(render(glyph, color, size, dpr), QIcon::Normal);
    QColor disabled = color;
    disabled.setAlphaF(0.35);
    ic.addPixmap(render(glyph, disabled, size, dpr), QIcon::Disabled);
    cache.insert(key, ic);
    return ic;
}

Icons::Glyph Icons::forLanguage(const QString &id)
{
    if (id == QLatin1String("c")) return LangC;
    if (id == QLatin1String("cpp")) return LangCpp;
    if (id == QLatin1String("python")) return LangPython;
    if (id == QLatin1String("rust")) return LangRust;
    if (id == QLatin1String("go")) return LangGo;
    if (id == QLatin1String("javascript")) return LangJs;
    if (id == QLatin1String("java")) return LangJava;
    if (id == QLatin1String("bash")) return LangShell;
    if (id == QLatin1String("ruby")) return LangRuby;
    if (id == QLatin1String("perl")) return LangPerl;
    if (id == QLatin1String("lua")) return LangLua;
    if (id == QLatin1String("php")) return LangPhp;
    if (id == QLatin1String("r")) return LangR;
    if (id == QLatin1String("julia")) return LangJulia;
    if (id == QLatin1String("haskell")) return LangHaskell;
    if (id == QLatin1String("fortran")) return LangFortran;
    if (id == QLatin1String("cmake")) return LangCMake;
    if (id == QLatin1String("makefile")) return LangMakefile;
    if (id == QLatin1String("markdown")) return LangMarkdown;
    if (id == QLatin1String("json")) return LangJson;
    if (id == QLatin1String("html")) return LangHtml;
    if (id == QLatin1String("css")) return LangCss;
    if (id == QLatin1String("xml")) return LangXml;
    if (id == QLatin1String("toml")) return LangToml;
    if (id == QLatin1String("sql")) return LangSql;
    return File;
}

QColor Icons::colorForLanguage(const QString &id)
{
    if (id == QLatin1String("c")) return QColor("#599eff");
    if (id == QLatin1String("cpp")) return QColor("#f34b7d");
    if (id == QLatin1String("python")) return QColor("#ffbc03");
    if (id == QLatin1String("rust")) return QColor("#dea584");
    if (id == QLatin1String("go")) return QColor("#00add8");
    if (id == QLatin1String("javascript")) return QColor("#f1e05a");
    if (id == QLatin1String("java")) return QColor("#e76f00");
    if (id == QLatin1String("bash")) return QColor("#89e051");
    if (id == QLatin1String("ruby")) return QColor("#e0115f");
    if (id == QLatin1String("perl")) return QColor("#39457e");
    if (id == QLatin1String("lua")) return QColor("#51a0cf");
    if (id == QLatin1String("php")) return QColor("#8892bf");
    if (id == QLatin1String("r")) return QColor("#276dc3");
    if (id == QLatin1String("julia")) return QColor("#9558b2");
    if (id == QLatin1String("haskell")) return QColor("#a074c4");
    if (id == QLatin1String("fortran")) return QColor("#734f96");
    if (id == QLatin1String("pascal")) return QColor("#e3f171");
    if (id == QLatin1String("cmake")) return QColor("#3aa0d8");
    if (id == QLatin1String("makefile")) return QColor("#e37933");
    if (id == QLatin1String("markdown")) return QColor("#519aba");
    if (id == QLatin1String("json")) return QColor("#cbcb41");
    if (id == QLatin1String("html")) return QColor("#e34c26");
    if (id == QLatin1String("css")) return QColor("#42a5f5");
    if (id == QLatin1String("xml")) return QColor("#e37933");
    if (id == QLatin1String("toml")) return QColor("#d1a26b");
    if (id == QLatin1String("sql")) return QColor("#dad8d8");
    return QColor("#8b949e");
}

const QList<Icons::Badge> &Icons::badgeLibrary()
{
    // Codepoints conferidos contra o cmap da Symbols Nerd Font embutida (nomes
    // de glifo verificados). Inclui linguagens fora do catálogo do editor.
    static const QList<Badge> lib = {
        // Catálogo do editor (para reusar em uma linguagem personalizada parecida).
        {QStringLiteral("c"), 0xE61E, QStringLiteral("C"), QColor("#599eff")},
        {QStringLiteral("cpp"), 0xE61D, QStringLiteral("C++"), QColor("#f34b7d")},
        {QStringLiteral("python"), 0xE606, QStringLiteral("Python"), QColor("#ffbc03")},
        {QStringLiteral("rust"), 0xE7A8, QStringLiteral("Rust"), QColor("#dea584")},
        {QStringLiteral("go"), 0xE626, QStringLiteral("Go"), QColor("#00add8")},
        {QStringLiteral("javascript"), 0xE60C, QStringLiteral("JavaScript"), QColor("#f1e05a")},
        {QStringLiteral("java"), 0xE738, QStringLiteral("Java"), QColor("#e76f00")},
        {QStringLiteral("shell"), 0xE795, QStringLiteral("Shell"), QColor("#89e051")},
        {QStringLiteral("ruby"), 0xE739, QStringLiteral("Ruby"), QColor("#e0115f")},
        {QStringLiteral("perl"), 0xE769, QStringLiteral("Perl"), QColor("#39457e")},
        {QStringLiteral("lua"), 0xE620, QStringLiteral("Lua"), QColor("#51a0cf")},
        {QStringLiteral("php"), 0xE73D, QStringLiteral("PHP"), QColor("#8892bf")},
        {QStringLiteral("r"), 0xE68A, QStringLiteral("R"), QColor("#276dc3")},
        {QStringLiteral("julia"), 0xE624, QStringLiteral("Julia"), QColor("#9558b2")},
        {QStringLiteral("haskell"), 0xE777, QStringLiteral("Haskell"), QColor("#a074c4")},
        {QStringLiteral("fortran"), 0xF121A, QStringLiteral("Fortran"), QColor("#734f96")},
        // Além do catálogo.
        {QStringLiteral("typescript"), 0xE628, QStringLiteral("TypeScript"), QColor("#3178c6")},
        {QStringLiteral("csharp"), 0xE648, QStringLiteral("C#"), QColor("#953dac")},
        {QStringLiteral("swift"), 0xE699, QStringLiteral("Swift"), QColor("#f05138")},
        {QStringLiteral("kotlin"), 0xE634, QStringLiteral("Kotlin"), QColor("#a97bff")},
        {QStringLiteral("dart"), 0xE798, QStringLiteral("Dart"), QColor("#00b4ab")},
        {QStringLiteral("zig"), 0xE6A9, QStringLiteral("Zig"), QColor("#f7a41d")},
        {QStringLiteral("elixir"), 0xE62D, QStringLiteral("Elixir"), QColor("#6e4a7e")},
        {QStringLiteral("erlang"), 0xE7B1, QStringLiteral("Erlang"), QColor("#a90533")},
        {QStringLiteral("scala"), 0xE68E, QStringLiteral("Scala"), QColor("#dc322f")},
        {QStringLiteral("clojure"), 0xE642, QStringLiteral("Clojure"), QColor("#5881d8")},
        {QStringLiteral("ocaml"), 0xE67A, QStringLiteral("OCaml"), QColor("#ef7a08")},
        {QStringLiteral("fsharp"), 0xE7A7, QStringLiteral("F#"), QColor("#378bba")},
        {QStringLiteral("nim"), 0xE677, QStringLiteral("Nim"), QColor("#f3d400")},
        {QStringLiteral("crystal"), 0xE62F, QStringLiteral("Crystal"), QColor("#cccccc")},
        {QStringLiteral("dlang"), 0xE7AF, QStringLiteral("D"), QColor("#ba595e")},
        {QStringLiteral("llvm"), 0xE823, QStringLiteral("LLVM"), QColor("#4e80b4")},
        {QStringLiteral("asm"), 0xE637, QStringLiteral("Assembly"), QColor("#6e4c13")},
        {QStringLiteral("lisp"), 0xE6B0, QStringLiteral("Lisp"), QColor("#3fb68b")},
        {QStringLiteral("scheme"), 0xE6B1, QStringLiteral("Scheme"), QColor("#1e4aec")},
        {QStringLiteral("vlang"), 0xE6AC, QStringLiteral("V"), QColor("#5d87bd")},
        {QStringLiteral("ada"), 0xE6B5, QStringLiteral("Ada"), QColor("#02f88c")},
        {QStringLiteral("apl"), 0xE730, QStringLiteral("APL"), QColor("#5a8164")},
        {QStringLiteral("prolog"), 0xE685, QStringLiteral("Prolog"), QColor("#74283c")},
        {QStringLiteral("purescript"), 0xE630, QStringLiteral("PureScript"), QColor("#1d222d")},
        {QStringLiteral("reasonml"), 0xE687, QStringLiteral("ReasonML"), QColor("#dd4b39")},
        {QStringLiteral("elm"), 0xE62C, QStringLiteral("Elm"), QColor("#60b5cc")},
        {QStringLiteral("groovy"), 0xE775, QStringLiteral("Groovy"), QColor("#4298b8")},
        {QStringLiteral("coffeescript"), 0xE61B, QStringLiteral("CoffeeScript"), QColor("#244776")},
        {QStringLiteral("objectivec"), 0xE84D, QStringLiteral("Objective-C"), QColor("#438eff")},
        {QStringLiteral("powershell"), 0xE683, QStringLiteral("PowerShell"), QColor("#4a7ebb")},
        {QStringLiteral("matlab"), 0xE82A, QStringLiteral("MATLAB"), QColor("#e16737")},
        {QStringLiteral("tex"), 0xE69B, QStringLiteral("TeX / LaTeX"), QColor("#3d6117")},
        {QStringLiteral("vue"), 0xE6A0, QStringLiteral("Vue"), QColor("#41b883")},
        {QStringLiteral("svelte"), 0xE697, QStringLiteral("Svelte"), QColor("#ff3e00")},
        {QStringLiteral("react"), 0xE625, QStringLiteral("React / JSX"), QColor("#61dafb")},
        {QStringLiteral("graphql"), 0xE662, QStringLiteral("GraphQL"), QColor("#e10098")},
        {QStringLiteral("wasm"), 0xE6A1, QStringLiteral("WebAssembly"), QColor("#654ff0")},
        {QStringLiteral("nodejs"), 0xE719, QStringLiteral("Node.js"), QColor("#539e43")},
        {QStringLiteral("deno"), 0xE7C0, QStringLiteral("Deno"), QColor("#70ffaf")},
        {QStringLiteral("docker"), 0xE650, QStringLiteral("Docker"), QColor("#2496ed")},
        {QStringLiteral("nix"), 0xE843, QStringLiteral("Nix"), QColor("#7e7eff")},
        {QStringLiteral("hack"), 0xE663, QStringLiteral("Hack"), QColor("#878787")},
        {QStringLiteral("binary"), 0xEAE8, QStringLiteral("Binary (generic)"), QColor("#8b949e")},
        {QStringLiteral("chip"), 0xF2DB, QStringLiteral("Chip (low-level)"), QColor("#8b949e")},
        {QStringLiteral("terminal"), 0xE795, QStringLiteral("Terminal / script"), QColor("#4eaa25")},
    };
    return lib;
}

QIcon Icons::badgeIcon(const QString &spec, const QString &languageId, int size)
{
    if (spec.startsWith(QLatin1String("glyph:"))) {
        const QString key = spec.mid(6);
        for (const Badge &b : badgeLibrary())
            if (b.key == key)
                return icon(Glyph(b.glyph), b.color, size);
        // chave desconhecida: cai no padrão
    } else if (!spec.isEmpty()) {
        QIcon ic(spec); // caminho de arquivo (PNG, SVG…)
        if (!ic.isNull() && !ic.availableSizes().isEmpty())
            return ic;
    }
    return icon(forLanguage(languageId), colorForLanguage(languageId), size);
}

QString Icons::badgeHtml(const QString &spec, const QString &languageId, int size)
{
    if (!spec.isEmpty() && !spec.startsWith(QLatin1String("glyph:"))) {
        // Arquivo de imagem: QLabel com texto rico aceita <img src="file:…">.
        const QUrl url = QUrl::fromLocalFile(spec);
        return QStringLiteral("<img src=\"%1\" width=\"%2\" height=\"%2\">").arg(url.toString()).arg(size);
    }
    // Glifo (padrão da linguagem ou da biblioteca): span na fonte de símbolos.
    char32_t cp = forLanguage(languageId);
    QColor color = colorForLanguage(languageId);
    if (spec.startsWith(QLatin1String("glyph:"))) {
        const QString key = spec.mid(6);
        for (const Badge &b : badgeLibrary())
            if (b.key == key) { cp = b.glyph; color = b.color; break; }
    }
    return QStringLiteral("<span style=\"font-family:'%1'; color:%2\">%3</span>")
        .arg(symbolFamily(), color.name(), glyphText(Glyph(cp)));
}

QString Icons::glyphUrl(Glyph glyph, const QColor &color, int size)
{
    static QHash<IconKey, QString> urlCache;
    const IconKey key{glyph, color.rgba(), size};
    if (const auto it = urlCache.constFind(key); it != urlCache.cend())
        return it.value();

    // Densidade 2: o Qt trata data-URLs PNG pelo tamanho em pixels.
    QByteArray png;
    {
        QBuffer buf(&png);
        buf.open(QIODevice::WriteOnly);
        render(glyph, color, size, 2.0).save(&buf, "PNG");
    }
    const QString url = QStringLiteral("data:image/png;base64,")
                        + QString::fromLatin1(png.toBase64(QByteArray::Base64Encoding));
    urlCache.insert(key, url);
    return url;
}
