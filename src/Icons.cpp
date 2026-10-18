#include "Icons.h"

#include <QBuffer>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QHash>
#include <QPainter>
#include <QPixmap>

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
