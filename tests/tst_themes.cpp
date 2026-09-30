#include "../src/Theme.h"

#include <QTest>

/** Contraste WCAG entre duas cores (1 a 21). */
static double luminance(const QColor &c)
{
    auto ch = [](double v) { return v <= 0.03928 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4); };
    return 0.2126 * ch(c.redF()) + 0.7152 * ch(c.greenF()) + 0.0722 * ch(c.blueF());
}
static double contrast(const QColor &a, const QColor &b)
{
    const double la = luminance(a), lb = luminance(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

class TestThemes : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void defaultIsMocha() { QCOMPARE(Theme::builtIns().first().id, QStringLiteral("catppuccin-mocha")); }

    void catalogHasRequestedFamilies()
    {
        QStringList ids;
        for (const Theme &t : Theme::builtIns())
            ids << t.id;
        for (const char *id : {"catppuccin-latte", "catppuccin-frappe", "catppuccin-macchiato", "catppuccin-mocha",
                               "ayu-dark", "ayu-mirage", "ayu-light", "high-contrast-dark", "high-contrast-light"})
            QVERIFY2(ids.contains(QLatin1String(id)), id);
        QCOMPARE(ids.size(), QSet<QString>(ids.begin(), ids.end()).size()); // ids únicos
    }

    void everyThemeHasOppositeVariant()
    {
        for (const Theme &t : Theme::builtIns()) {
            QVERIFY2(!t.counterpart.isEmpty(), qPrintable(t.id));
            const Theme &c = Theme::variant(t.id, !t.dark);
            QVERIFY2(c.id == t.counterpart, qPrintable(t.id + " → " + t.counterpart));
            QVERIFY2(c.dark != t.dark, qPrintable(t.id));
            // A volta leva de novo a um tema da mesma claridade do original.
            QCOMPARE(Theme::variant(c.id, t.dark).dark, t.dark);
        }
    }

    void catppuccinMatchesOfficialPalette()
    {
        const Theme &mocha = Theme::byId("catppuccin-mocha");
        QCOMPARE(mocha.surface.name(), QStringLiteral("#1e1e2e")); // base
        QCOMPARE(mocha.window.name(), QStringLiteral("#181825"));  // mantle
        QCOMPARE(mocha.keyword.name(), QStringLiteral("#cba6f7")); // mauve
        const Theme &latte = Theme::byId("catppuccin-latte");
        QCOMPARE(latte.surface.name(), QStringLiteral("#eff1f5"));
        QCOMPARE(latte.primary.name(), QStringLiteral("#1e66f5")); // blue
    }

    void readableContrast_data()
    {
        QTest::addColumn<QString>("id");
        for (const Theme &t : Theme::builtIns())
            QTest::newRow(qPrintable(t.id)) << t.id;
    }
    void readableContrast()
    {
        QFETCH(QString, id);
        const Theme &t = Theme::byId(id);
        const bool hc = id.startsWith(QLatin1String("high-contrast"));
        // As paletas oficiais (Catppuccin Latte, Ayu Light, Solarized…) são
        // mantidas como são; os limites abaixo são os que todas cumprem. Os
        // temas de alto contraste exigem 7:1 (WCAG AAA) em tudo.
        const double textMin = hc ? 7.0 : 4.0;
        const double syntaxMin = hc ? 7.0 : 2.0;
        auto check = [&](const QColor &fg, const QColor &bg, double min, const char *what) {
            const double c = contrast(fg, bg);
            QVERIFY2(c >= min, qPrintable(QStringLiteral("%1: %2 %3 on %4 = %5 < %6")
                                              .arg(id, what, fg.name(), bg.name()).arg(c, 0, 'f', 2).arg(min)));
        };
        check(t.text, t.surface, textMin, "text");
        check(t.text, t.terminalBackground, textMin, "terminal text");
        check(t.text, t.currentLine, textMin, "text on current line");
        for (auto [c, name] : {std::pair{t.keyword, "keyword"}, {t.type, "type"}, {t.function, "function"},
                               {t.string, "string"}, {t.number, "number"}, {t.preprocessor, "preprocessor"}})
            check(c, t.surface, syntaxMin, name);
        check(t.comment, t.surface, hc ? 7.0 : 1.9, "comment");
    }
};

QTEST_APPLESS_MAIN(TestThemes)
#include "tst_themes.moc"
