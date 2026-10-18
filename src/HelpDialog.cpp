#include "HelpDialog.h"
#include "Icons.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QTextBrowser>
#include <QVBoxLayout>

namespace {

/** Minúsculas e sem acentos, para a busca casar "codificacao" com "codificação". */
QString normalize(const QString &text)
{
    QString out;
    for (QChar c : text.normalized(QString::NormalizationForm_D)) {
        if (c.category() != QChar::Mark_NonSpacing)
            out += c.toLower();
    }
    return out;
}

QString plain(const QString &html)
{
    QString s = html;
    static const QRegularExpression tags(QStringLiteral("<[^>]*>"));
    return s.replace(tags, QStringLiteral(" "));
}

QString keys(const QString &k)
{
    return QStringLiteral("<code>%1</code>").arg(k.toHtmlEscaped());
}

/** Preenche %1, %2… da tabela de atalhos (QString::arg aceita até 9 por chamada). */
QString shortcutTable(QString html, const QStringList &keyList)
{
    for (const QString &k : keyList)
        html = html.arg(keys(k));
    return html;
}

} // namespace

QList<HelpTopic> HelpDialog::topics()
{
    return {
        {tr("Getting started"), tr("start begin welcome overview new open file tab"),
         tr("<p>Slayer Editor is a code editor with an embedded terminal. Create a file with %1, open one with %2 "
            "or drop file names on the command line: <code>slayer-editor main.c</code>.</p>"
            "<p>Closing the last tab shows the welcome screen. Open files, window layout and terminal position are "
            "restored on the next start.</p>")
             .arg(keys("Ctrl+N"), keys("Ctrl+O"))},
        {tr("Saving files"), tr("save save as save all write disk modified unsaved"),
         tr("<p>%1 saves, %2 saves under a new name and %3 saves every modified document (untitled ones ask for a "
            "name). A ● in the tab marks unsaved changes; closing asks before discarding them.</p>")
             .arg(keys("Ctrl+S"), keys("Ctrl+Shift+S"), keys("Ctrl+L"))},
        {tr("Running code"), tr("run execute compile build f5 gcc g++ python flags tool"),
         tr("<p>%1 saves the file and runs it in the terminal. C, C++ and Rust are compiled to "
            "<code>/tmp/slayer_out</code> and then executed; interpreted languages run directly.</p>"
            "<p>%2 runs and stays in the interpreter (Python, JavaScript, shell). In an open folder, %3 builds "
            "and runs the whole project (see <b>Projects</b>).</p>"
            "<p>Change the tool, flags and extensions of each language in <b>Settings → Languages</b>, or add your "
            "own language (Ruby, Lua, Perl…) with <b>Add Language…</b>.</p>")
             .arg(keys("F5"), keys("Alt+F5"), keys("F4"))},
        {tr("Terminal"), tr("terminal shell bash console dock position left right top bottom float exit"),
         tr("<p>%1 shows or hides the terminal and moves the focus to it; %2 opens a new terminal tab. The ▥ "
            "button on its title bar moves it to the left, top, right or bottom; you can also drag it by the title "
            "or detach it as a window. %3 / %4 copy and paste in the terminal.</p>"
            "<p>Right-click the terminal for copy and paste, select all, find, clear, save the output, zoom, open "
            "the file manager there, rename the tab and monitor activity or silence; <b>Configure</b> sets the "
            "color scheme, cursor shape, history and scrollbar (also in <b>Settings → Terminal</b>).</p>"
            "<p>Typing <code>exit</code> starts a new shell. With a folder open, new terminals start in it. "
            "Font, size, shell and cursor are in <b>Settings → Terminal</b>.</p>")
             .arg(keys("Ctrl+`"), keys("Ctrl+Shift+`"), keys("Ctrl+Shift+C"), keys("Ctrl+Shift+V"))},
        {tr("Split view"), tr("split view vertical horizontal side by side pane window divide"),
         tr("<p>The button at the right end of the tab bar splits the view, like Kate: %1 side by side, %2 stacked. "
            "The same document can be shown in several views; edits appear in all of them.</p>"
            "<p>“Move Document to New Split” moves the tab instead of duplicating it. %3 closes the current view, "
            "%4 / %5 switch between views.</p>")
             .arg(keys("Ctrl+Shift+L"), keys("Ctrl+Shift+T"), keys("Ctrl+Shift+R"), keys("Ctrl+Alt+→"),
                  keys("Ctrl+Alt+←"))},
        {tr("Find and replace"), tr("find search replace next previous case word"),
         tr("<p>%1 opens the find bar and %2 the replace bar. %3 / %4 jump to the next or previous match; "
            "<b>Aa</b> matches case and <b>Word</b> whole words.</p>")
             .arg(keys("Ctrl+F"), keys("Ctrl+H"), keys("F3"), keys("Shift+F3"))},
        {tr("Code completion"), tr("completion autocomplete suggestion popup ctrl space intellisense"),
         tr("<p>Suggestions appear after two letters: words of the file, language keywords and, when a language "
            "server runs, its items (shown first). %1 opens the list at any time. %2 or %3 accept, %4 closes.</p>"
            "<p>Turn it on or off in <b>View</b> or <b>Settings → Editor → Features</b>.</p>")
             .arg(keys("Ctrl+Space"), keys("Enter"), keys("Tab"), keys("Esc"))},
        {tr("Language servers (LSP)"), tr("lsp language server clangd pylsp pyright diagnostics errors warnings underline"),
         tr("<p>With a language server installed, errors and warnings are underlined (red, yellow, blue) and the "
            "line number takes their color. Hover the underline for the message; the message of the cursor line "
            "is also shown in the status bar.</p>"
            "<p>Choose the server of each language in <b>Settings → Languages</b>; its status tells "
            "whether it is installed and how to install it. For C and C++, clangd uses "
            "<code>compile_commands.json</code> when present, otherwise the flags from Settings → Languages.</p>")},
        {tr("Code folding"), tr("fold unfold collapse expand block folding node toplevel"),
         tr("<p>Click the arrow next to a line number to fold its block (C, C++, Java, JavaScript, Rust, Go and "
            "Python). Folded lines end with ⋯. <b>View → Code Folding</b> has the Kate commands: %1 toggles the "
            "current node, %2 the nodes contained in it, %3 / %4 fold or unfold the toplevel nodes.</p>")
             .arg(keys("Ctrl+Shift+["), keys("Ctrl+Shift+]"), keys("Ctrl+Alt+["), keys("Ctrl+Alt+]"))},
        {tr("Brackets, indentation and tabs"), tr("auto closing pairs brackets quotes indent tab spaces soft tabs width"),
         tr("<p>Brackets and quotes are closed automatically; typing the closing one just steps over it. Enter "
            "keeps the indentation and indents after <code>{</code> or a Python <code>:</code>. %1 / %2 indent "
            "or unindent the selected lines, %3 toggles comments.</p>"
            "<p>Tab width (4 by default) and “insert spaces” are in <b>Settings → Editor</b>; the status bar "
            "button changes them for the current file.</p>")
             .arg(keys("Tab"), keys("Shift+Tab"), keys("Ctrl+/"))},
        {tr("Text encoding"), tr("encoding utf-8 utf8 latin iso-8859 windows-1252 bom charset codificação"),
         tr("<p>The status bar shows the encoding of the file. Click it to choose another one: an unmodified file "
            "is reloaded in that encoding, otherwise it applies when saving. Files with a BOM keep it; files that "
            "are not valid UTF-8 open as Windows-1252.</p>")},
        {tr("Status bar"), tr("status bar footer branch git line column insert overwrite"),
         tr("<p>From left to right: git branch, message of the cursor line, language server, “Performance” (when "
            "performance mode is on), line:column, INSERT/OVERWRITE (%1 toggles), tabs, encoding and language "
            "(click to change the highlighting).</p>")
             .arg(keys("Insert"))},
        {tr("Themes and appearance"), tr("theme dark light color appearance scale font size zoom toolbar icons text configure"),
         tr("<p>Pick a theme in <b>View → Theme</b> or <b>Settings → Appearance</b>; it also colors the terminal. "
            "%4 switches the theme to its dark or light variant. "
            "%1 / %2 / %3 zoom the editor. Right-click the toolbar for text position, icon size, lock and "
            "<b>Configure Toolbars…</b> (choose and order its buttons).</p>")
             .arg(keys("Ctrl++"), keys("Ctrl+-"), keys("Ctrl+0"), keys("Ctrl+Shift+D"))},
        {tr("Projects"), tr("project folder open folder workspace tree quick open find in project build run cmake make"),
         tr("<p><b>File → Open Folder…</b> (%1) opens a folder as a project: its files appear in the side panel "
            "(%2 shows or hides it; right-click for new file, new folder, rename, move to trash and copy path). "
            "Each project remembers its open tabs; <b>Open Recent Project</b> and <b>Close Folder</b> are in the "
            "File menu.</p>"
            "<p>%3 (Quick Open) finds a file by name, typing just some of its letters. %4 searches all the files, "
            "with match case, whole word and regular expression options. Patterns hidden from the tree, Quick Open "
            "and the search are in <b>Settings → Editor → Project</b>; the project's <code>.gitignore</code> is also "
            "honored by Quick Open and the search.</p>"
            "<p>%5 builds and %6 builds and runs the project, from its folder, in the terminal. The commands are "
            "detected from the project root (CMake, Make, Cargo, Go, npm, Meson, Python) and can be changed in "
            "<b>Run → Configure Project Commands…</b>.</p>")
             .arg(keys("Ctrl+Shift+O"), keys("Ctrl+Alt+B"), keys("Ctrl+P"), keys("Ctrl+Shift+F"), keys("Ctrl+Shift+B"),
                  keys("F4"))},
        {tr("Debugging"), tr("debug debugger breakpoint step gdb debugpy variables call stack continue"),
         tr("<p>%1 debugs the current file (Python with debugpy, C and C++ with gdb). %2 toggles a breakpoint, "
            "or click next to the line number. While paused: %3 step over, %4 step into, %5 step out, %6 continue, "
            "%7 stop.</p>"
            "<p>The <b>Debugger</b> panel shows the variables and the call stack; the program reads and writes in "
            "the terminal.</p>")
             .arg(keys("Ctrl+F5"), keys("F9"), keys("F6"), keys("F7"), keys("Shift+F7"), keys("F8"), keys("Shift+F5"))},
        {tr("HTML live preview"), tr("html preview browser live reload css javascript server php"),
         tr("<p>For HTML files, %1 opens the page in the browser and reloads it as you edit. The server (built-in "
            "or an external one such as <code>php -S</code>), host, port, browser and reload mode are in "
            "<b>Settings → Preview</b>.</p>")
             .arg(keys("Shift+F5"))},
        {tr("Performance mode"), tr("performance large big file slow highlighting minified fast"),
         tr("<p><b>Settings → Editor → Performance</b> (off by default) turns off syntax highlighting, code folding, "
            "the minimap, indent guides and occurrence highlighting. <b>Automatic</b> decides when a file is opened: "
            "more lines or MB than the limits set there, or a line longer than 10,000 characters (minified code). "
            "<b>Always</b> applies to every file. The status bar shows “Performance” while it is on.</p>")},
        {tr("Profiles"), tr("profile profiles save load settings setup preset"),
         tr("<p><b>Settings → Profiles</b> stores every setting under a name, so you can switch setups (class, "
            "presentation, home) in one click. Loading a profile applies it immediately.</p>")},
        {tr("Keyboard shortcuts"), tr("shortcuts keys keyboard hotkeys"),
         shortcutTable(tr(
            "<table cellpadding='3'>"
            "<tr><td colspan='2'><b>Files</b></td></tr>"
            "<tr><td>%1</td><td>New file (from a template)</td></tr>"
            "<tr><td>%2</td><td>New empty file</td></tr>"
            "<tr><td>%3</td><td>Open file</td></tr>"
            "<tr><td>%4</td><td>Open folder</td></tr>"
            "<tr><td>%5</td><td>Quick Open (find a file of the project)</td></tr>"
            "<tr><td>%6</td><td>Save</td></tr>"
            "<tr><td>%7</td><td>Save as</td></tr>"
            "<tr><td>%8</td><td>Save all</td></tr>"
            "<tr><td>%9</td><td>Close tab</td></tr>"
            "<tr><td>%10</td><td>Reload document</td></tr>"
            "<tr><td>%11</td><td>Quit</td></tr>"
            "<tr><td colspan='2'><b>Editing</b></td></tr>"
            "<tr><td>%12</td><td>Undo / redo</td></tr>"
            "<tr><td>%13</td><td>Toggle comment</td></tr>"
            "<tr><td>%14</td><td>Indent / unindent</td></tr>"
            "<tr><td>%15</td><td>Code completion</td></tr>"
            "<tr><td>%16</td><td>Insert / overwrite</td></tr>"
            "<tr><td>%17</td><td>Find / replace</td></tr>"
            "<tr><td>%18</td><td>Next / previous match</td></tr>"
            "<tr><td>%19</td><td>Find in project</td></tr>"
            "<tr><td>%20</td><td>Go to line</td></tr>"
            "<tr><td colspan='2'><b>View</b></td></tr>"
            "<tr><td>%21</td><td>Show/hide terminal</td></tr>"
            "<tr><td>%22</td><td>New terminal tab</td></tr>"
            "<tr><td>%23</td><td>Copy / paste in the terminal</td></tr>"
            "<tr><td>%24</td><td>Project panel</td></tr>"
            "<tr><td>%25</td><td>Split vertical / horizontal</td></tr>"
            "<tr><td>%26</td><td>Close current view</td></tr>"
            "<tr><td>%27</td><td>Next / previous view</td></tr>"
            "<tr><td>%28</td><td>Fold current / contained nodes</td></tr>"
            "<tr><td>%29</td><td>Fold / unfold toplevel nodes</td></tr>"
            "<tr><td>%30</td><td>Zoom in / out / reset</td></tr>"
            "<tr><td>%31</td><td>Dark/light mode</td></tr>"
            "<tr><td colspan='2'><b>Run and debug</b></td></tr>"
            "<tr><td>%32</td><td>Run file</td></tr>"
            "<tr><td>%33</td><td>Run interactive</td></tr>"
            "<tr><td>%34</td><td>Live preview (HTML) / stop debugging</td></tr>"
            "<tr><td>%35</td><td>Build project</td></tr>"
            "<tr><td>%36</td><td>Run project</td></tr>"
            "<tr><td>%37</td><td>Debug current file</td></tr>"
            "<tr><td>%38</td><td>Toggle breakpoint</td></tr>"
            "<tr><td>%39</td><td>Step over / into / out</td></tr>"
            "<tr><td>%40</td><td>Continue</td></tr>"
            "<tr><td colspan='2'><b>Other</b></td></tr>"
            "<tr><td>%41</td><td>Settings</td></tr>"
            "<tr><td>%42</td><td>Help</td></tr></table>"),
                       {QStringLiteral("Ctrl+N"), QStringLiteral("Ctrl+Alt+N"), QStringLiteral("Ctrl+O"), QStringLiteral("Ctrl+Shift+O"), QStringLiteral("Ctrl+P"), QStringLiteral("Ctrl+S"), QStringLiteral("Ctrl+Shift+S"), QStringLiteral("Ctrl+L"), QStringLiteral("Ctrl+W"), QStringLiteral("Ctrl+Shift+F5"), QStringLiteral("Ctrl+Q"), QStringLiteral("Ctrl+Z / Ctrl+Shift+Z"), QStringLiteral("Ctrl+/"), QStringLiteral("Tab / Shift+Tab"), QStringLiteral("Ctrl+Space"), QStringLiteral("Insert"), QStringLiteral("Ctrl+F / Ctrl+H"), QStringLiteral("F3 / Shift+F3"), QStringLiteral("Ctrl+Shift+F"), QStringLiteral("Ctrl+G"), QStringLiteral("Ctrl+`"), QStringLiteral("Ctrl+Shift+`"), QStringLiteral("Ctrl+Shift+C / Ctrl+Shift+V"), QStringLiteral("Ctrl+Alt+B"), QStringLiteral("Ctrl+Shift+L / Ctrl+Shift+T"), QStringLiteral("Ctrl+Shift+R"), QStringLiteral("Ctrl+Alt+→ / Ctrl+Alt+←"), QStringLiteral("Ctrl+Shift+[ / Ctrl+Shift+]"), QStringLiteral("Ctrl+Alt+[ / Ctrl+Alt+]"), QStringLiteral("Ctrl++ / Ctrl+- / Ctrl+0"), QStringLiteral("Ctrl+Shift+D"), QStringLiteral("F5"), QStringLiteral("Alt+F5"), QStringLiteral("Shift+F5"), QStringLiteral("Ctrl+Shift+B"), QStringLiteral("F4"), QStringLiteral("Ctrl+F5"), QStringLiteral("F9"), QStringLiteral("F6 / F7 / Shift+F7"), QStringLiteral("F8"), QStringLiteral("Ctrl+,"), QStringLiteral("F1")})},
    };
}

QList<int> HelpDialog::search(const QList<HelpTopic> &topics, const QString &query)
{
    const QStringList terms = normalize(query).split(QLatin1Char(' '), Qt::SkipEmptyParts);
    QList<int> out;
    for (int i = 0; i < topics.size(); ++i) {
        const QString hay = normalize(topics[i].title + QLatin1Char(' ') + topics[i].keywords + QLatin1Char(' ')
                                      + plain(topics[i].html));
        bool all = true;
        for (const QString &t : terms)
            all = all && hay.contains(t);
        if (all)
            out << i;
    }
    return out;
}

HelpDialog::HelpDialog(const Theme &theme, QWidget *parent)
    : QDialog(parent)
    , m_topics(topics())
    , m_search(new QLineEdit(this))
    , m_list(new QListWidget(this))
    , m_view(new QTextBrowser(this))
    , m_empty(new QLabel(this))
    , m_theme(theme)
{
    setWindowTitle(tr("Slayer Editor Help"));
    setObjectName(QStringLiteral("SettingsDialog"));
    resize(900, 600);

    m_search->setPlaceholderText(tr("Search help (e.g. split, encoding, lsp)…"));
    m_search->setClearButtonEnabled(true);
    m_search->addAction(Icons::icon(Icons::Search, theme.textSecondary, 16), QLineEdit::LeadingPosition);
    m_list->setObjectName(QStringLiteral("SettingsNav"));
    m_list->setFixedWidth(250);
    m_view->setObjectName(QStringLiteral("HelpView"));
    m_view->setOpenExternalLinks(true);
    m_view->document()->setDefaultStyleSheet(
        QStringLiteral("h2 { color: %1; } code { background: %2; color: %3; } p { line-height: 140%; }")
            .arg(theme.primary.name(), theme.window.name(), theme.text.name()));
    m_empty->setObjectName(QStringLiteral("Hint"));
    m_empty->setText(tr("No topic matches."));
    m_empty->setAlignment(Qt::AlignCenter);
    m_empty->hide();

    auto *left = new QVBoxLayout;
    left->setContentsMargins(12, 12, 0, 12);
    left->addWidget(m_search);
    left->addWidget(m_list, 1);
    left->addWidget(m_empty);
    auto *root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->addLayout(left);
    root->addWidget(m_view, 1);

    connect(m_search, &QLineEdit::textChanged, this, &HelpDialog::filter);
    connect(m_list, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row >= 0)
            showTopic(m_list->item(row)->data(Qt::UserRole).toInt());
    });
    filter(QString());
    m_search->setFocus();
}

void HelpDialog::filter(const QString &query)
{
    m_list->clear();
    for (int i : search(m_topics, query)) {
        auto *item = new QListWidgetItem(m_topics[i].title);
        item->setData(Qt::UserRole, i);
        item->setSizeHint(QSize(0, 34));
        m_list->addItem(item);
    }
    m_empty->setVisible(m_list->count() == 0);
    if (m_list->count() > 0)
        m_list->setCurrentRow(0);
    else
        m_view->clear();
}

void HelpDialog::showTopic(int topic)
{
    const HelpTopic &t = m_topics.value(topic);
    m_view->setHtml(QStringLiteral("<h2>%1</h2>%2").arg(t.title.toHtmlEscaped(), t.html));
}
