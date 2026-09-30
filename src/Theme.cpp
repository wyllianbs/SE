#include "Theme.h"

#include <QCoreApplication>
#include <QDir>
#include <QJsonArray>
#include <QFile>
#include <QTextStream>

namespace {

QColor mix(const QColor &a, const QColor &b, qreal t)
{
    return QColor::fromRgbF(a.redF() + (b.redF() - a.redF()) * t, a.greenF() + (b.greenF() - a.greenF()) * t,
                            a.blueF() + (b.blueF() - a.blueF()) * t);
}

/** Paleta de 8 cores ANSI; as "brilhantes" clareiam (escuro) ou escurecem (claro) um pouco. */
void setAnsi(Theme &t, std::initializer_list<const char *> normal, std::initializer_list<const char *> bright = {})
{
    int i = 0;
    for (const char *c : normal)
        t.ansi[i++] = QColor(c);
    if (bright.size() == 8) {
        for (const char *c : bright)
            t.ansi[i++] = QColor(c);
    } else {
        for (int k = 0; k < 8; ++k)
            t.ansi[8 + k] = t.dark ? t.ansi[k].lighter(118) : t.ansi[k].darker(118);
    }
}

/** Sabores do Catppuccin, com as cores oficiais de catppuccin.com/palette. */
struct Flavor
{
    const char *id, *name, *counterpart;
    bool dark;
    const char *rosewater, *flamingo, *pink, *mauve, *red, *maroon, *peach, *yellow, *green, *teal, *sky,
        *sapphire, *blue, *lavender, *text, *subtext1, *subtext0, *overlay2, *overlay1, *overlay0, *surface2,
        *surface1, *surface0, *base, *mantle, *crust;
};

Theme catppuccin(const Flavor &f)
{
    Theme t;
    t.id = QString::fromLatin1(f.id);
    t.name = QString::fromUtf8(f.name);
    t.counterpart = QString::fromLatin1(f.counterpart);
    t.dark = f.dark;
    t.primary = QColor(f.blue);
    t.window = QColor(f.mantle);
    t.surface = QColor(f.base);
    t.text = QColor(f.text);
    t.textSecondary = QColor(f.subtext0);
    t.divider = QColor(f.surface0);
    t.currentLine = mix(QColor(f.base), QColor(f.surface0), 0.55);
    t.selection = QColor(f.surface1);
    // Guia de estilo do Catppuccin para código.
    t.keyword = QColor(f.mauve);
    t.type = QColor(f.yellow);
    t.function = QColor(f.blue);
    t.string = QColor(f.green);
    t.number = QColor(f.peach);
    t.comment = QColor(f.overlay2);
    t.preprocessor = QColor(f.pink);
    t.terminalBackground = QColor(f.crust);
    // Terminal oficial: preto = surface1/subtext1, branco = subtext0/surface2 (conforme claro/escuro).
    if (f.dark)
        setAnsi(t, {f.surface1, f.red, f.green, f.yellow, f.blue, f.pink, f.teal, f.subtext1},
                {f.surface2, f.red, f.green, f.yellow, f.blue, f.pink, f.teal, f.subtext0});
    else
        setAnsi(t, {f.subtext1, f.red, f.green, f.yellow, f.blue, f.pink, f.teal, f.surface2},
                {f.subtext0, f.red, f.green, f.yellow, f.blue, f.pink, f.teal, f.surface1});
    return t;
}

/**
 * Tema a partir de uma lista compacta: interface (destaque, janela, editor,
 * texto, texto secundário, bordas, linha atual, seleção), sintaxe (palavra-
 * chave, tipo, função, string, número, comentário, pré-processador) e fundo
 * do terminal.
 */
Theme theme(const char *id, const char *name, const char *counterpart, bool dark,
            std::initializer_list<const char *> ui, std::initializer_list<const char *> syntax, const char *termBg)
{
    Theme t;
    t.id = QString::fromLatin1(id);
    t.name = QString::fromUtf8(name);
    t.counterpart = QString::fromLatin1(counterpart);
    t.dark = dark;
    auto u = ui.begin();
    t.primary = QColor(*u++);
    t.window = QColor(*u++);
    t.surface = QColor(*u++);
    t.text = QColor(*u++);
    t.textSecondary = QColor(*u++);
    t.divider = QColor(*u++);
    t.currentLine = QColor(*u++);
    t.selection = QColor(*u++);
    auto sy = syntax.begin();
    t.keyword = QColor(*sy++);
    t.type = QColor(*sy++);
    t.function = QColor(*sy++);
    t.string = QColor(*sy++);
    t.number = QColor(*sy++);
    t.comment = QColor(*sy++);
    t.preprocessor = QColor(*sy++);
    t.terminalBackground = QColor(termBg);
    return t;
}

QList<Theme> buildThemes()
{
    QList<Theme> list;

    // ── Catppuccin (padrão: Mocha) ──
    const Flavor flavors[] = {
        {"catppuccin-mocha", "Catppuccin Mocha", "catppuccin-latte", true,
         "#f5e0dc", "#f2cdcd", "#f5c2e7", "#cba6f7", "#f38ba8", "#eba0ac", "#fab387", "#f9e2af", "#a6e3a1",
         "#94e2d5", "#89dceb", "#74c7ec", "#89b4fa", "#b4befe", "#cdd6f4", "#bac2de", "#a6adc8", "#9399b2",
         "#7f849c", "#6c7086", "#585b70", "#45475a", "#313244", "#1e1e2e", "#181825", "#11111b"},
        {"catppuccin-macchiato", "Catppuccin Macchiato", "catppuccin-latte", true,
         "#f4dbd6", "#f0c6c6", "#f5bde6", "#c6a0f6", "#ed8796", "#ee99a0", "#f5a97f", "#eed49f", "#a6da95",
         "#8bd5ca", "#91d7e3", "#7dc4e4", "#8aadf4", "#b7bdf8", "#cad3f5", "#b8c0e0", "#a5adcb", "#939ab7",
         "#8087a2", "#6e738d", "#5b6078", "#494d64", "#363a4f", "#24273a", "#1e2030", "#181926"},
        {"catppuccin-frappe", "Catppuccin Frappé", "catppuccin-latte", true,
         "#f2d5cf", "#eebebe", "#f4b8e4", "#ca9ee6", "#e78284", "#ea999c", "#ef9f76", "#e5c890", "#a6d189",
         "#81c8be", "#99d1db", "#85c1dc", "#8caaee", "#babbf1", "#c6d0f5", "#b5bfe2", "#a5adce", "#949cbb",
         "#838ba7", "#737994", "#626880", "#51576d", "#414559", "#303446", "#292c3c", "#232634"},
        {"catppuccin-latte", "Catppuccin Latte", "catppuccin-mocha", false,
         "#dc8a78", "#dd7878", "#ea76cb", "#8839ef", "#d20f39", "#e64553", "#fe640b", "#df8e1d", "#40a02b",
         "#179299", "#04a5e5", "#209fb5", "#1e66f5", "#7287fd", "#4c4f69", "#5c5f77", "#6c6f85", "#7c7f93",
         "#8c8fa1", "#9ca0b0", "#acb0be", "#bcc0cc", "#ccd0da", "#eff1f5", "#e6e9ef", "#dce0e8"},
    };
    for (const Flavor &f : flavors)
        list << catppuccin(f);

    // ── Ayu (ayu-theme: Dark, Mirage, Light) ──
    Theme t = theme("ayu-dark", "Ayu Dark", "ayu-light", true,
                    {"#e6b450", "#0d1017", "#0b0e14", "#bfbdb6", "#6c7380", "#1b1f29", "#131721", "#273747"},
                    {"#ff8f40", "#59c2ff", "#ffb454", "#aad94c", "#d2a6ff", "#646b73", "#e6b673"}, "#0b0e14");
    setAnsi(t, {"#1b1f29", "#f07178", "#aad94c", "#e6b450", "#59c2ff", "#d2a6ff", "#95e6cb", "#bfbdb6"});
    list << t;
    t = theme("ayu-mirage", "Ayu Mirage", "ayu-light", true,
              {"#ffcc66", "#1f2430", "#242936", "#cccac2", "#707a8c", "#171b24", "#1a1f29", "#34455a"},
              {"#ffad66", "#73d0ff", "#ffd173", "#d5ff80", "#dfbfff", "#6c7a8b", "#ffdfb3"}, "#1f2430");
    setAnsi(t, {"#171b24", "#f28779", "#d5ff80", "#ffcc66", "#73d0ff", "#dfbfff", "#95e6cb", "#cccac2"});
    list << t;
    t = theme("ayu-light", "Ayu Light", "ayu-dark", false,
              {"#fa8d3e", "#f3f4f5", "#fcfcfc", "#5c6166", "#8a9199", "#e7e8e9", "#f2f2f2", "#d4e3f5"},
              {"#fa8d3e", "#399ee6", "#eba400", "#86b300", "#a37acc", "#9da2a6", "#e59645"}, "#f3f4f5");
    setAnsi(t, {"#5c6166", "#e65050", "#6cbf43", "#eba400", "#399ee6", "#a37acc", "#4cbf99", "#e7e8e9"});
    list << t;

    // ── Alto contraste (legibilidade máxima; cores com contraste ≥ 7:1) ──
    t = theme("high-contrast-dark", "High Contrast Dark", "high-contrast-light", true,
              {"#ffd400", "#000000", "#000000", "#ffffff", "#e0e0e0", "#6fc3df", "#1c1c1c", "#264f9e"},
              {"#6cc7ff", "#4ef0c8", "#ffff7a", "#ffc36b", "#c3ff9e", "#9ee38f", "#ff8cff"}, "#000000");
    setAnsi(t, {"#545454", "#ff6b6b", "#6bff6b", "#ffff66", "#6cb6ff", "#ff7aff", "#66ffff", "#ffffff"});
    list << t;
    t = theme("high-contrast-light", "High Contrast Light", "high-contrast-dark", false,
              {"#0f4a85", "#f2f2f2", "#ffffff", "#000000", "#1f1f1f", "#0f4a85", "#e3ebf7", "#a8cdff"},
              {"#0000c8", "#00574a", "#5a3500", "#a31515", "#6a0098", "#0b5d0b", "#a3005c"}, "#ffffff");
    setAnsi(t, {"#000000", "#a31515", "#0b5d0b", "#5a3500", "#0000c8", "#6a0098", "#00574a", "#595959"});
    list << t;

    // ── Tokyo Night / Day ──
    t = theme("tokyo-night", "Tokyo Night", "tokyo-night-day", true,
              {"#7aa2f7", "#16161e", "#1a1b26", "#c0caf5", "#a9b1d6", "#292e42", "#232433", "#33467c"},
              {"#bb9af7", "#2ac3de", "#7aa2f7", "#9ece6a", "#ff9e64", "#565f89", "#7dcfff"}, "#16161e");
    setAnsi(t, {"#15161e", "#f7768e", "#9ece6a", "#e0af68", "#7aa2f7", "#bb9af7", "#7dcfff", "#a9b1d6"},
            {"#414868", "#f7768e", "#9ece6a", "#e0af68", "#7aa2f7", "#bb9af7", "#7dcfff", "#c0caf5"});
    list << t;
    t = theme("tokyo-night-day", "Tokyo Night Day", "tokyo-night", false,
              {"#2e7de9", "#d0d5e3", "#e1e2e7", "#3760bf", "#6172b0", "#c4c8da", "#d5d6db", "#b7c1e3"},
              {"#9854f1", "#118c74", "#2e7de9", "#587539", "#b15c00", "#848cb5", "#007197"}, "#e9e9ed");
    setAnsi(t, {"#b4b5b9", "#f52a65", "#587539", "#8c6c3e", "#2e7de9", "#9854f1", "#007197", "#6172b0"},
            {"#a1a6c5", "#f52a65", "#587539", "#8c6c3e", "#2e7de9", "#9854f1", "#007197", "#3760bf"});
    list << t;

    // ── GitHub Dark / Light ──
    t = theme("github-dark", "GitHub Dark", "github-light", true,
              {"#58a6ff", "#010409", "#0d1117", "#e6edf3", "#8b949e", "#30363d", "#161b22", "#264f78"},
              {"#ff7b72", "#ffa657", "#d2a8ff", "#a5d6ff", "#79c0ff", "#8b949e", "#ff7b72"}, "#010409");
    setAnsi(t, {"#484f58", "#ff7b72", "#3fb950", "#d29922", "#58a6ff", "#bc8cff", "#39c5cf", "#b1bac4"},
            {"#6e7681", "#ffa198", "#56d364", "#e3b341", "#79c0ff", "#d2a8ff", "#56d4dd", "#ffffff"});
    list << t;
    t = theme("github-light", "GitHub Light", "github-dark", false,
              {"#0969da", "#f6f8fa", "#ffffff", "#1f2328", "#656d76", "#d0d7de", "#f6f8fa", "#b6e3ff"},
              {"#cf222e", "#953800", "#8250df", "#0a3069", "#0550ae", "#6e7781", "#cf222e"}, "#f6f8fa");
    setAnsi(t, {"#24292f", "#cf222e", "#116329", "#4d2d00", "#0969da", "#8250df", "#1b7c83", "#6e7781"},
            {"#57606a", "#a40e26", "#1a7f37", "#633c01", "#218bff", "#a475f9", "#3192aa", "#8c959f"});
    list << t;

    // ── Monokai / Monokai Light ──
    t = theme("monokai", "Monokai", "monokai-light", true,
              {"#66d9ef", "#1e1f1c", "#272822", "#f8f8f2", "#cfcfc2", "#3e3d32", "#3e3d32", "#49483e"},
              {"#f92672", "#66d9ef", "#a6e22e", "#e6db74", "#ae81ff", "#75715e", "#f92672"}, "#1e1f1c");
    setAnsi(t, {"#272822", "#f92672", "#a6e22e", "#f4bf75", "#66d9ef", "#ae81ff", "#a1efe4", "#f8f8f2"},
            {"#75715e", "#f92672", "#a6e22e", "#f4bf75", "#66d9ef", "#ae81ff", "#a1efe4", "#f9f8f5"});
    list << t;
    t = theme("monokai-light", "Monokai Light", "monokai", false,
              {"#0089b3", "#efefe9", "#fafaf8", "#272822", "#75715e", "#dcdcd0", "#f0f0e8", "#d8d8c8"},
              {"#d0165c", "#0089b3", "#56a502", "#998f00", "#7c4dff", "#9a9784", "#d0165c"}, "#f0f0ea");
    setAnsi(t, {"#272822", "#d0165c", "#56a502", "#b8860b", "#0089b3", "#7c4dff", "#138a88", "#75715e"});
    list << t;

    // ── Nord / Nord Light ──
    t = theme("nord", "Nord", "nord-light", true,
              {"#88c0d0", "#242933", "#2e3440", "#eceff4", "#d8dee9", "#3b4252", "#3b4252", "#434c5e"},
              {"#81a1c1", "#8fbcbb", "#88c0d0", "#a3be8c", "#b48ead", "#616e88", "#5e81ac"}, "#242933");
    setAnsi(t, {"#3b4252", "#bf616a", "#a3be8c", "#ebcb8b", "#81a1c1", "#b48ead", "#88c0d0", "#e5e9f0"},
            {"#4c566a", "#bf616a", "#a3be8c", "#ebcb8b", "#81a1c1", "#b48ead", "#8fbcbb", "#eceff4"});
    list << t;
    t = theme("nord-light", "Nord Light", "nord", false,
              {"#5e81ac", "#e5e9f0", "#eceff4", "#2e3440", "#4c566a", "#d8dee9", "#e5e9f0", "#d8dee9"},
              {"#5e81ac", "#3b7d7a", "#4c7ea0", "#58803a", "#9d6897", "#7b88a1", "#81608c"}, "#e5e9f0");
    setAnsi(t, {"#2e3440", "#bf616a", "#58803a", "#a67f22", "#5e81ac", "#9d6897", "#3b7d7a", "#4c566a"});
    list << t;

    // ── Solarized Dark / Light ──
    t = theme("solarized-dark", "Solarized Dark", "solarized-light", true,
              {"#268bd2", "#00212b", "#002b36", "#93a1a1", "#839496", "#073642", "#073642", "#0d4a5a"},
              {"#859900", "#b58900", "#268bd2", "#2aa198", "#d33682", "#586e75", "#cb4b16"}, "#00212b");
    setAnsi(t, {"#073642", "#dc322f", "#859900", "#b58900", "#268bd2", "#d33682", "#2aa198", "#eee8d5"},
            {"#002b36", "#cb4b16", "#586e75", "#657b83", "#839496", "#6c71c4", "#93a1a1", "#fdf6e3"});
    list << t;
    t = theme("solarized-light", "Solarized Light", "solarized-dark", false,
              {"#268bd2", "#eee8d5", "#fdf6e3", "#586e75", "#657b83", "#e0dbc7", "#eee8d5", "#e0dbc7"},
              {"#859900", "#b58900", "#268bd2", "#2aa198", "#d33682", "#93a1a1", "#cb4b16"}, "#eee8d5");
    setAnsi(t, {"#073642", "#dc322f", "#859900", "#b58900", "#268bd2", "#d33682", "#2aa198", "#eee8d5"},
            {"#002b36", "#cb4b16", "#586e75", "#657b83", "#839496", "#6c71c4", "#93a1a1", "#fdf6e3"});
    list << t;

    // ── Gruvbox Dark / Light ──
    t = theme("gruvbox-dark", "Gruvbox Dark", "gruvbox-light", true,
              {"#fe8019", "#1d2021", "#282828", "#ebdbb2", "#a89984", "#3c3836", "#32302f", "#504945"},
              {"#fb4934", "#fabd2f", "#b8bb26", "#b8bb26", "#d3869b", "#928374", "#8ec07c"}, "#1d2021");
    setAnsi(t, {"#282828", "#cc241d", "#98971a", "#d79921", "#458588", "#b16286", "#689d6a", "#a89984"},
            {"#928374", "#fb4934", "#b8bb26", "#fabd2f", "#83a598", "#d3869b", "#8ec07c", "#ebdbb2"});
    list << t;
    t = theme("gruvbox-light", "Gruvbox Light", "gruvbox-dark", false,
              {"#af3a03", "#f2e5bc", "#fbf1c7", "#3c3836", "#7c6f64", "#ebdbb2", "#f2e5bc", "#d5c4a1"},
              {"#9d0006", "#b57614", "#79740e", "#79740e", "#8f3f71", "#928374", "#427b58"}, "#f9f5d7");
    setAnsi(t, {"#fbf1c7", "#cc241d", "#98971a", "#d79921", "#458588", "#b16286", "#689d6a", "#7c6f64"},
            {"#928374", "#9d0006", "#79740e", "#b57614", "#076678", "#8f3f71", "#427b58", "#3c3836"});
    list << t;

    // ── One Dark / One Light ──
    t = theme("one-dark", "One Dark", "one-light", true,
              {"#61afef", "#21252b", "#282c34", "#abb2bf", "#7f848e", "#3e4451", "#2c313c", "#3e4451"},
              {"#c678dd", "#e5c07b", "#61afef", "#98c379", "#d19a66", "#5c6370", "#e06c75"}, "#21252b");
    setAnsi(t, {"#3f4451", "#e06c75", "#98c379", "#e5c07b", "#61afef", "#c678dd", "#56b6c2", "#d7dae0"},
            {"#4f5666", "#be5046", "#98c379", "#d19a66", "#61afef", "#c678dd", "#56b6c2", "#e6e6e6"});
    list << t;
    t = theme("one-light", "One Light", "one-dark", false,
              {"#4078f2", "#f0f0f0", "#fafafa", "#383a42", "#696c77", "#dbdbdc", "#f2f2f2", "#e5e5e6"},
              {"#a626a4", "#c18401", "#4078f2", "#50a14f", "#986801", "#a0a1a7", "#e45649"}, "#f0f0f0");
    setAnsi(t, {"#383a42", "#e45649", "#50a14f", "#c18401", "#4078f2", "#a626a4", "#0184bc", "#696c77"});
    list << t;

    // ── Dracula / Alucard (variante clara oficial) ──
    t = theme("dracula", "Dracula", "alucard", true,
              {"#bd93f9", "#21222c", "#282a36", "#f8f8f2", "#bfbfbf", "#44475a", "#343746", "#44475a"},
              {"#ff79c6", "#8be9fd", "#50fa7b", "#f1fa8c", "#bd93f9", "#6272a4", "#ff79c6"}, "#21222c");
    setAnsi(t, {"#21222c", "#ff5555", "#50fa7b", "#f1fa8c", "#bd93f9", "#ff79c6", "#8be9fd", "#f8f8f2"},
            {"#6272a4", "#ff6e6e", "#69ff94", "#ffffa5", "#d6acff", "#ff92df", "#a4ffff", "#ffffff"});
    list << t;
    t = theme("alucard", "Alucard", "dracula", false,
              {"#644ac9", "#f5f0dc", "#fffbeb", "#1f1f1f", "#635d97", "#e8e3cf", "#f5f0dc", "#cfcfde"},
              {"#a3144d", "#036a96", "#14710a", "#846e15", "#644ac9", "#635d97", "#a34d14"}, "#f5f0dc");
    setAnsi(t, {"#1f1f1f", "#cb3a2a", "#14710a", "#846e15", "#644ac9", "#a3144d", "#036a96", "#635d97"});
    list << t;

    return list;
}

QString rgb(const QColor &c)
{
    return QStringLiteral("%1,%2,%3").arg(c.red()).arg(c.green()).arg(c.blue());
}

QList<Theme> &activeThemes()
{
    static QList<Theme> themes = Theme::builtIns();
    return themes;
}

} // namespace

const QList<Theme> &Theme::builtIns()
{
    static const QList<Theme> themes = buildThemes();
    return themes;
}

bool Theme::isBuiltIn(const QString &id)
{
    for (const Theme &t : builtIns()) {
        if (t.id == id)
            return true;
    }
    return false;
}

QList<Theme> Theme::compose(const QList<Theme> &userThemes)
{
    QList<Theme> out = builtIns();
    for (const Theme &u : userThemes) {
        bool replaced = false;
        for (Theme &t : out) {
            if (t.id == u.id) {
                t = u; // personalização de um tema do app
                replaced = true;
            }
        }
        if (!replaced)
            out.append(u);
    }
    return out;
}

void Theme::setUserThemes(const QList<Theme> &themes)
{
    activeThemes() = compose(themes);
}

const QList<Theme> &Theme::all()
{
    return activeThemes();
}

const Theme &Theme::byId(const QString &id)
{
    for (const Theme &t : all()) {
        if (t.id == id)
            return t;
    }
    return all().first();
}

const Theme &Theme::variant(const QString &id, bool dark)
{
    const Theme &t = byId(id);
    if (t.dark == dark || t.counterpart.isEmpty())
        return t;
    for (const Theme &c : all()) {
        if (c.id == t.counterpart)
            return c;
    }
    return t;
}

const QList<Theme::Field> &Theme::fields()
{
    static const QList<Field> list{
        {"primary", QT_TRANSLATE_NOOP("Theme", "Accent"), QT_TRANSLATE_NOOP("Theme", "Interface"), &Theme::primary},
        {"window", QT_TRANSLATE_NOOP("Theme", "Window"), QT_TRANSLATE_NOOP("Theme", "Interface"), &Theme::window},
        {"surface", QT_TRANSLATE_NOOP("Theme", "Editor background"), QT_TRANSLATE_NOOP("Theme", "Interface"), &Theme::surface},
        {"text", QT_TRANSLATE_NOOP("Theme", "Text"), QT_TRANSLATE_NOOP("Theme", "Interface"), &Theme::text},
        {"textSecondary", QT_TRANSLATE_NOOP("Theme", "Secondary text"), QT_TRANSLATE_NOOP("Theme", "Interface"), &Theme::textSecondary},
        {"divider", QT_TRANSLATE_NOOP("Theme", "Borders"), QT_TRANSLATE_NOOP("Theme", "Interface"), &Theme::divider},
        {"currentLine", QT_TRANSLATE_NOOP("Theme", "Current line"), QT_TRANSLATE_NOOP("Theme", "Interface"), &Theme::currentLine},
        {"selection", QT_TRANSLATE_NOOP("Theme", "Selection"), QT_TRANSLATE_NOOP("Theme", "Interface"), &Theme::selection},
        {"keyword", QT_TRANSLATE_NOOP("Theme", "Keywords"), QT_TRANSLATE_NOOP("Theme", "Syntax"), &Theme::keyword},
        {"type", QT_TRANSLATE_NOOP("Theme", "Types"), QT_TRANSLATE_NOOP("Theme", "Syntax"), &Theme::type},
        {"function", QT_TRANSLATE_NOOP("Theme", "Functions"), QT_TRANSLATE_NOOP("Theme", "Syntax"), &Theme::function},
        {"string", QT_TRANSLATE_NOOP("Theme", "Strings"), QT_TRANSLATE_NOOP("Theme", "Syntax"), &Theme::string},
        {"number", QT_TRANSLATE_NOOP("Theme", "Numbers"), QT_TRANSLATE_NOOP("Theme", "Syntax"), &Theme::number},
        {"comment", QT_TRANSLATE_NOOP("Theme", "Comments"), QT_TRANSLATE_NOOP("Theme", "Syntax"), &Theme::comment},
        {"preprocessor", QT_TRANSLATE_NOOP("Theme", "Preprocessor"), QT_TRANSLATE_NOOP("Theme", "Syntax"), &Theme::preprocessor},
        {"terminalBackground", QT_TRANSLATE_NOOP("Theme", "Background"), QT_TRANSLATE_NOOP("Theme", "Terminal"), &Theme::terminalBackground},
    };
    return list;
}

QJsonObject Theme::toJson() const
{
    QJsonObject colors;
    for (const Field &f : fields())
        colors[QLatin1String(f.key)] = (this->*f.member).name();
    QJsonArray ansiArray;
    for (const QColor &c : ansi)
        ansiArray.append(c.name());
    return QJsonObject{{"id", id}, {"name", name}, {"dark", dark}, {"counterpart", counterpart},
                       {"colors", colors}, {"ansi", ansiArray}};
}

Theme Theme::fromJson(const QJsonObject &o)
{
    // Parte do tema do app de mesmo id (ou do primeiro): campos ausentes ficam válidos.
    Theme t = builtIns().first();
    for (const Theme &b : builtIns()) {
        if (b.id == o.value("id").toString())
            t = b;
    }
    t.id = o.value("id").toString(t.id);
    t.name = o.value("name").toString(t.name);
    t.dark = o.value("dark").toBool(t.dark);
    t.counterpart = o.value("counterpart").toString(t.counterpart);
    const QJsonObject colors = o.value("colors").toObject();
    for (const Field &f : fields()) {
        const QColor c(colors.value(QLatin1String(f.key)).toString());
        if (c.isValid())
            t.*f.member = c;
    }
    const QJsonArray a = o.value("ansi").toArray();
    for (int i = 0; i < 16 && i < a.size(); ++i) {
        const QColor c(a.at(i).toString());
        if (c.isValid())
            t.ansi[i] = c;
    }
    return t;
}

QString Theme::writeTerminalScheme(const QString &dir, const QString &name) const
{
    QDir().mkpath(dir);
    const QString schemeName = name.isEmpty() ? QStringLiteral("SlayerEditor-") + id : name;
    QFile file(dir + QLatin1Char('/') + schemeName + QStringLiteral(".colorscheme"));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
        return QString();

    QTextStream out(&file);
    out << "[General]\nDescription=" << schemeName << "\nOpacity=1\n\n";
    out << "[Background]\nColor=" << rgb(terminalBackground) << "\n\n";
    out << "[BackgroundIntense]\nColor=" << rgb(terminalBackground) << "\n\n";
    out << "[Foreground]\nColor=" << rgb(text) << "\n\n";
    out << "[ForegroundIntense]\nColor=" << rgb(text) << "\nBold=true\n\n";
    for (int i = 0; i < 8; ++i) {
        out << "[Color" << i << "]\nColor=" << rgb(ansi[i]) << "\n\n";
        out << "[Color" << i << "Intense]\nColor=" << rgb(ansi[i + 8]) << "\n\n";
    }
    return schemeName;
}
