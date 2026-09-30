#include "Theme.h"

#include <QDir>
#include <QFile>
#include <QTextStream>

namespace {

Theme make(const QString &id, const QString &name, bool dark,
           std::initializer_list<const char *> ui,
           std::initializer_list<const char *> syntax,
           const char *termBg,
           std::initializer_list<const char *> ansi)
{
    Theme t;
    t.id = id;
    t.name = name;
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
    auto s = syntax.begin();
    t.keyword = QColor(*s++);
    t.type = QColor(*s++);
    t.comment = QColor(*s++);
    t.string = QColor(*s++);
    t.number = QColor(*s++);
    t.function = QColor(*s++);
    t.preprocessor = QColor(*s++);
    t.terminalBackground = QColor(termBg);
    int i = 0;
    for (const char *c : ansi)
        t.ansi[i++] = QColor(c);
    return t;
}

QList<Theme> buildThemes()
{
    return {
        make("catppuccin-mocha", "Catppuccin Mocha", true,
             {"#89b4fa", "#11111b", "#1e1e2e", "#cdd6f4", "#bac2de", "#313244", "#2a2b3c", "#45475a"},
             {"#cba6f7", "#f9e2af", "#6c7086", "#a6e3a1", "#fab387", "#89b4fa", "#f5c2e7"},
             "#11111b",
             {"#45475a", "#f38ba8", "#a6e3a1", "#f9e2af", "#89b4fa", "#cba6f7", "#94e2d5", "#bac2de",
              "#585b70", "#f38ba8", "#a6e3a1", "#f9e2af", "#89b4fa", "#cba6f7", "#94e2d5", "#a6adc8"}),
        make("catppuccin-latte", "Catppuccin Latte", false,
             {"#1e66f5", "#e6e9ef", "#eff1f5", "#4c4f69", "#6c6f85", "#ccd0da", "#e6e9ef", "#bcc0cc"},
             {"#8839ef", "#df8e1d", "#9ca0b0", "#40a02b", "#fe640b", "#1e66f5", "#ea76cb"},
             "#dce0e8",
             {"#5c5f77", "#d20f39", "#40a02b", "#df8e1d", "#1e66f5", "#8839ef", "#179299", "#acb0be",
              "#6c6f85", "#d20f39", "#40a02b", "#df8e1d", "#1e66f5", "#8839ef", "#179299", "#bcc0cc"}),
        make("tokyo-night", "Tokyo Night", true,
             {"#7aa2f7", "#16161e", "#1a1b26", "#c0caf5", "#a9b1d6", "#292e42", "#232433", "#33467c"},
             {"#bb9af7", "#2ac3de", "#565f89", "#9ece6a", "#ff9e64", "#7aa2f7", "#7dcfff"},
             "#16161e",
             {"#15161e", "#f7768e", "#9ece6a", "#e0af68", "#7aa2f7", "#bb9af7", "#7dcfff", "#a9b1d6",
              "#414868", "#f7768e", "#9ece6a", "#e0af68", "#7aa2f7", "#bb9af7", "#7dcfff", "#c0caf5"}),
        make("github-dark", "GitHub Dark", true,
             {"#58a6ff", "#010409", "#0d1117", "#e6edf3", "#8b949e", "#30363d", "#161b22", "#264f78"},
             {"#ff7b72", "#ffa657", "#8b949e", "#a5d6ff", "#79c0ff", "#d2a8ff", "#ff7b72"},
             "#010409",
             {"#484f58", "#ff7b72", "#3fb950", "#d29922", "#58a6ff", "#bc8cff", "#39c5cf", "#b1bac4",
              "#6e7681", "#ffa198", "#56d364", "#e3b341", "#79c0ff", "#d2a8ff", "#56d4dd", "#ffffff"}),
        make("monokai", "Monokai", true,
             {"#66d9ef", "#1e1f1c", "#272822", "#f8f8f2", "#cfcfc2", "#3e3d32", "#3e3d32", "#49483e"},
             {"#f92672", "#66d9ef", "#75715e", "#e6db74", "#ae81ff", "#a6e22e", "#f92672"},
             "#1e1f1c",
             {"#272822", "#f92672", "#a6e22e", "#f4bf75", "#66d9ef", "#ae81ff", "#a1efe4", "#f8f8f2",
              "#75715e", "#f92672", "#a6e22e", "#f4bf75", "#66d9ef", "#ae81ff", "#a1efe4", "#f9f8f5"}),
        make("nord", "Nord", true,
             {"#88c0d0", "#242933", "#2e3440", "#eceff4", "#d8dee9", "#3b4252", "#3b4252", "#434c5e"},
             {"#81a1c1", "#8fbcbb", "#616e88", "#a3be8c", "#b48ead", "#88c0d0", "#5e81ac"},
             "#242933",
             {"#3b4252", "#bf616a", "#a3be8c", "#ebcb8b", "#81a1c1", "#b48ead", "#88c0d0", "#e5e9f0",
              "#4c566a", "#bf616a", "#a3be8c", "#ebcb8b", "#81a1c1", "#b48ead", "#8fbcbb", "#eceff4"}),
        make("solarized-light", "Solarized Light", false,
             {"#268bd2", "#eee8d5", "#fdf6e3", "#586e75", "#657b83", "#e0dbc7", "#eee8d5", "#e0dbc7"},
             {"#859900", "#b58900", "#93a1a1", "#2aa198", "#d33682", "#268bd2", "#cb4b16"},
             "#eee8d5",
             {"#073642", "#dc322f", "#859900", "#b58900", "#268bd2", "#d33682", "#2aa198", "#eee8d5",
              "#002b36", "#cb4b16", "#586e75", "#657b83", "#839496", "#6c71c4", "#93a1a1", "#fdf6e3"}),
    };
}

QString rgb(const QColor &c)
{
    return QStringLiteral("%1,%2,%3").arg(c.red()).arg(c.green()).arg(c.blue());
}

} // namespace

const QList<Theme> &Theme::all()
{
    static const QList<Theme> themes = buildThemes();
    return themes;
}

const Theme &Theme::byId(const QString &id)
{
    for (const Theme &t : all()) {
        if (t.id == id)
            return t;
    }
    return all().first();
}

QString Theme::writeTerminalScheme(const QString &dir) const
{
    QDir().mkpath(dir);
    const QString schemeName = QStringLiteral("SlayerEditor-") + id;
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
