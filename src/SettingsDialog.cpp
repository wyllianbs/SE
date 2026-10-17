#include "SettingsDialog.h"
#include "Icons.h"
#include "Language.h"
#include "LspManager.h"
#include "ThemeEditor.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFontComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QSet>
#include <QRegularExpression>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QTableWidget>
#include <QTextStream>
#include <QToolButton>
#include <QVBoxLayout>

namespace {

constexpr int kIdRole = Qt::UserRole;
constexpr int kCustomRole = Qt::UserRole + 1;

/**
 * Página com título, descrição e conteúdo rolável. Com expand, o conteúdo
 * ocupa a altura disponível (páginas com tabela ou lista crescem junto com a
 * janela); sem ele, fica no topo.
 */
QWidget *makePage(const QString &title, const QString &subtitle, QLayout *content, bool expand = false)
{
    auto *inner = new QWidget;
    auto *v = new QVBoxLayout(inner);
    v->setContentsMargins(28, 24, 28, 24);
    v->setSpacing(14);
    auto *h = new QLabel(title);
    h->setObjectName(QStringLiteral("PageTitle"));
    v->addWidget(h);
    if (!subtitle.isEmpty()) {
        auto *s = new QLabel(subtitle);
        s->setObjectName(QStringLiteral("PageSubtitle"));
        s->setWordWrap(true);
        v->addWidget(s);
    }
    v->addLayout(content, expand ? 1 : 0);
    if (!expand)
        v->addStretch(1);

    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(inner);
    return scroll;
}

/** Cartão agrupando opções relacionadas. */
QWidget *card(const QString &title, QLayout *content)
{
    auto *box = new QWidget;
    box->setObjectName(QStringLiteral("Card"));
    box->setAttribute(Qt::WA_StyledBackground);
    auto *v = new QVBoxLayout(box);
    v->setContentsMargins(18, 14, 18, 16);
    v->setSpacing(10);
    if (!title.isEmpty()) {
        auto *l = new QLabel(title);
        l->setObjectName(QStringLiteral("CardTitle"));
        v->addWidget(l);
    }
    v->addLayout(content, 1);
    return box;
}

QStringList availableShells()
{
    QStringList shells;
    QFile f(QStringLiteral("/etc/shells"));
    if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&f);
        while (!in.atEnd()) {
            const QString line = in.readLine().trimmed();
            if (line.startsWith(QLatin1Char('/')) && QFile::exists(line) && !shells.contains(line)
                && !line.startsWith(QLatin1String("/usr/bin/")))
                shells << line;
        }
    }
    return shells;
}

} // namespace

SettingsDialog::SettingsDialog(const AppSettings &settings, const Theme &theme, QWidget *parent)
    : QDialog(parent)
    , m_detect(settings)
    , m_theme(theme)
    , m_nav(new QListWidget(this))
    , m_pages(new QStackedWidget(this))
{
    setWindowTitle(tr("Settings"));
    setObjectName(QStringLiteral("SettingsDialog"));
    resize(900, 620);

    m_nav->setObjectName(QStringLiteral("SettingsNav"));
    m_nav->setFixedWidth(240);
    m_nav->setIconSize(QSize(18, 18));
    m_nav->setFocusPolicy(Qt::NoFocus);

    addPage(tr("Profiles"), Icons::Profiles, buildProfilesPage());
    addPage(tr("Editor"), Icons::Code, buildEditorPage());
    addPage(tr("Appearance"), Icons::Palette, buildAppearancePage());
    addPage(tr("Terminal"), Icons::Terminal, buildTerminalPage());
    addPage(tr("Preview"), Icons::Server, buildPreviewPage());
    addPage(tr("Languages"), Icons::Languages, buildLanguagesPage());
    addPage(tr("Symbols"), Icons::Symbols, buildSymbolsPage());
    connect(m_nav, &QListWidget::currentRowChanged, m_pages, &QStackedWidget::setCurrentIndex);
    m_nav->setCurrentRow(1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Apply
                                         | QDialogButtonBox::Cancel | QDialogButtonBox::RestoreDefaults);
    buttons->button(QDialogButtonBox::RestoreDefaults)->setText(tr("Restore Defaults"));
    connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
        Q_EMIT applied(this->settings());
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked, this,
            [this]() { Q_EMIT applied(this->settings()); });
    connect(buttons->button(QDialogButtonBox::RestoreDefaults), &QPushButton::clicked, this, [this]() {
        if (QMessageBox::question(this, tr("Restore Defaults"),
                                  tr("Reset every setting to its default value? Saved profiles are kept."))
            == QMessageBox::Yes)
            setForm(AppSettings::defaults());
    });

    auto *right = new QVBoxLayout;
    right->setContentsMargins(0, 0, 16, 14);
    right->addWidget(m_pages, 1);
    right->addWidget(buttons);

    auto *root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addWidget(m_nav);
    root->addLayout(right, 1);

    setForm(settings);
}

void SettingsDialog::addPage(const QString &title, char32_t glyph, QWidget *page)
{
    auto *item = new QListWidgetItem(Icons::icon(static_cast<Icons::Glyph>(glyph), m_theme.primary, 18), title);
    item->setSizeHint(QSize(0, 40));
    m_nav->addItem(item);
    m_pages->addWidget(page);
}

// ------------------------------------------------------------ páginas

QWidget *SettingsDialog::buildProfilesPage()
{
    m_profiles = new QListWidget;
    m_profiles->setObjectName(QStringLiteral("ProfileList"));
    m_profiles->setMinimumHeight(140);
    m_profileName = new QLineEdit;
    m_profileName->setPlaceholderText(tr("Profile name"));
    m_activeProfile = new QLabel;
    m_activeProfile->setObjectName(QStringLiteral("Hint"));

    auto *save = new QPushButton(tr("Save Current Settings"));
    save->setDefault(false);
    save->setAutoDefault(false);
    auto *load = new QPushButton(tr("Load"));
    auto *remove = new QPushButton(tr("Delete"));
    load->setAutoDefault(false);
    remove->setAutoDefault(false);
    remove->setObjectName(QStringLiteral("Danger"));

    connect(save, &QPushButton::clicked, this, [this]() {
        const QString name = m_profileName->text().trimmed();
        if (name.isEmpty()) {
            m_profileName->setFocus();
            return;
        }
        if (AppSettings::profileNames().contains(name)
            && QMessageBox::question(this, tr("Save Profile"), tr("Overwrite profile “%1”?").arg(name))
                   != QMessageBox::Yes)
            return;
        AppSettings::saveProfile(name, settings());
        AppSettings::setActiveProfile(name);
        m_profileName->clear();
        refreshProfiles();
    });
    connect(m_profileName, &QLineEdit::returnPressed, save, &QPushButton::click);

    auto loadSelected = [this]() {
        auto *item = m_profiles->currentItem();
        if (!item)
            return;
        if (const auto profile = AppSettings::loadProfile(item->text())) {
            setForm(*profile);
            AppSettings::setActiveProfile(item->text());
            refreshProfiles();
            Q_EMIT applied(settings());
        }
    };
    connect(load, &QPushButton::clicked, this, loadSelected);
    connect(m_profiles, &QListWidget::itemDoubleClicked, this, loadSelected);
    connect(remove, &QPushButton::clicked, this, [this]() {
        auto *item = m_profiles->currentItem();
        if (!item)
            return;
        if (QMessageBox::question(this, tr("Delete Profile"), tr("Delete profile “%1”?").arg(item->text()))
            == QMessageBox::Yes) {
            AppSettings::deleteProfile(item->text());
            refreshProfiles();
        }
    });

    auto *saveRow = new QHBoxLayout;
    saveRow->addWidget(m_profileName, 1);
    saveRow->addWidget(save);
    auto *saveBox = new QVBoxLayout;
    saveBox->addLayout(saveRow);

    auto *actions = new QHBoxLayout;
    actions->addWidget(m_activeProfile, 1);
    actions->addWidget(load);
    actions->addWidget(remove);
    auto *listBox = new QVBoxLayout;
    listBox->addWidget(m_profiles, 1);
    listBox->addLayout(actions);

    auto *layout = new QVBoxLayout;
    layout->setSpacing(14);
    layout->addWidget(card(tr("New profile"), saveBox));
    layout->addWidget(card(tr("Saved profiles"), listBox), 1);
    refreshProfiles();
    return makePage(tr("Profiles"),
                    tr("A profile stores every setting under a name, so you can switch setups in one click "
                       "(for example “Class”, “Presentation”, “Home”)."),
                    layout, true);
}

QWidget *SettingsDialog::buildEditorPage()
{
    m_editorFont = new QFontComboBox;
    m_editorFont->setFontFilters(QFontComboBox::MonospacedFonts);
    m_editorFontSize = new QSpinBox;
    m_editorFontSize->setRange(6, 48);
    m_editorFontSize->setSuffix(QStringLiteral(" pt"));
    m_tabWidth = new QSpinBox;
    m_tabWidth->setRange(1, 16);
    m_tabWidth->setSuffix(tr(" spaces"));

    auto *font = new QFormLayout;
    font->setLabelAlignment(Qt::AlignLeft);
    font->setHorizontalSpacing(24);
    font->addRow(tr("Font"), m_editorFont);
    font->addRow(tr("Size"), m_editorFontSize);
    font->addRow(tr("Tab width"), m_tabWidth);
    m_insertSpaces = new QCheckBox(tr("Insert spaces instead of tab characters"));
    font->addRow(QString(), m_insertSpaces);

    m_lineNumbers = new QCheckBox(tr("Show line numbers"));
    m_wordWrap = new QCheckBox(tr("Wrap long lines"));
    m_highlightLine = new QCheckBox(tr("Highlight the current line"));
    m_minimap = new QCheckBox(tr("Show the code minimap (overview on the right)"));
    m_editorBlink = new QCheckBox(tr("Blinking cursor"));
    auto *display = new QVBoxLayout;
    for (QCheckBox *c : {m_lineNumbers, m_wordWrap, m_highlightLine, m_minimap, m_editorBlink})
        display->addWidget(c);
    m_wrapIndent = new QCheckBox(tr("Indent wrapped lines (experimental — may be unstable on Wayland)"));
    m_wrapIndent->setToolTip(tr("Indent the continuation of wrapped lines to the line's own indentation. "
                                "Disabled by default because it has crashed Qt's text drawing on some Wayland "
                                "setups while searching."));
    display->addWidget(m_wrapIndent);
    // Recuo extra das linhas quebradas (0 = modo VSCode).
    m_wrapIndentExtra = new QSpinBox;
    m_wrapIndentExtra->setRange(0, 32);
    m_wrapIndentExtra->setSuffix(tr(" spaces"));
    m_wrapIndentExtra->setToolTip(tr("Extra indentation for wrapped lines, beyond the line's own indentation. "
                                     "0 aligns the continuation under the first character (like VS Code)."));
    auto *wrapIndentRow = new QHBoxLayout;
    auto *wrapIndentLabel = new QLabel(tr("Extra indent on wrapped lines"));
    wrapIndentRow->addWidget(wrapIndentLabel);
    wrapIndentRow->addStretch(1);
    wrapIndentRow->addWidget(m_wrapIndentExtra);
    display->addLayout(wrapIndentRow);

    m_autoPairs = new QCheckBox(tr("Auto-close brackets and quotes  ( )  [ ]  { }  \" \"  ' '"));
    m_completion = new QCheckBox(tr("Code completion (words in the file + language keywords, Ctrl+Space)"));
    m_folding = new QCheckBox(tr("Code folding (arrows next to the line numbers, Ctrl+Shift+[ / ])"));
    m_lspEnabled = new QCheckBox(tr("Language servers (LSP): diagnostics and smarter completion"));
    m_autoSave = new QCheckBox(tr("Auto-save edited files"));
    auto *typing = new QVBoxLayout;
    typing->addWidget(m_autoPairs);
    typing->addWidget(m_completion);
    typing->addWidget(m_folding);
    typing->addWidget(m_lspEnabled);
    typing->addWidget(m_autoSave);

    auto *layout = new QVBoxLayout;
    layout->setSpacing(14);
    layout->addWidget(card(tr("Font"), font));
    layout->addWidget(card(tr("Display"), display));
    layout->addWidget(card(tr("Features"), typing));
    return makePage(tr("Editor"), QString(), layout);
}

QWidget *SettingsDialog::buildSymbolsPage()
{
    m_prettySymbols = new QCheckBox(tr("Display sequences as symbols (the file text is not changed)"));

    m_symbolsTable = new QTableWidget(0, 3);
    m_symbolsTable->setObjectName(QStringLiteral("LanguageTable"));
    m_symbolsTable->setHorizontalHeaderLabels({tr("On"), tr("Sequence"), tr("Symbol")});
    m_symbolsTable->verticalHeader()->hide();
    m_symbolsTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_symbolsTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_symbolsTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_symbolsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_symbolsTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_symbolsTable->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed
                                    | QAbstractItemView::AnyKeyPressed);
    m_symbolsTable->setShowGrid(false);
    m_symbolsTable->setAlternatingRowColors(true);
    m_symbolsTable->setEnabled(m_prettySymbols->isChecked());
    connect(m_prettySymbols, &QCheckBox::toggled, m_symbolsTable, &QWidget::setEnabled);

    auto *add = new QPushButton(tr("Add Symbol"));
    auto *remove = new QPushButton(tr("Remove"));
    auto *restore = new QPushButton(tr("Restore Defaults"));
    for (QPushButton *b : {add, remove, restore})
        b->setAutoDefault(false);
    remove->setObjectName(QStringLiteral("Danger"));
    connect(add, &QPushButton::clicked, this, [this]() {
        addSymbolRow({QStringLiteral("<>"), QString::fromUtf8("≠"), true});
        m_symbolsTable->setCurrentCell(m_symbolsTable->rowCount() - 1, 1);
        m_symbolsTable->editItem(m_symbolsTable->item(m_symbolsTable->rowCount() - 1, 1));
    });
    connect(remove, &QPushButton::clicked, this, [this]() {
        if (m_symbolsTable->currentRow() >= 0)
            m_symbolsTable->removeRow(m_symbolsTable->currentRow());
    });
    connect(restore, &QPushButton::clicked, this, [this]() {
        m_symbolsTable->setRowCount(0);
        for (const SymbolRule &r : AppSettings::defaultSymbols())
            addSymbolRow(r);
    });

    auto *buttons = new QHBoxLayout;
    buttons->addWidget(add);
    buttons->addWidget(restore);
    buttons->addStretch(1);
    buttons->addWidget(remove);

    auto *box = new QVBoxLayout;
    box->addWidget(m_prettySymbols);
    box->addWidget(m_symbolsTable, 1);
    box->addLayout(buttons);

    auto *layout = new QVBoxLayout;
    layout->addWidget(card(QString(), box), 1);
    return makePage(tr("Symbols"),
                    tr("Show typed sequences as symbols without changing the file: for example “<=” as ≤, “->” as "
                       "→. Off by default. The sequence under the cursor is shown as typed so you can edit it. "
                       "Double-click a cell to change it."),
                    layout, true);
}

void SettingsDialog::addSymbolRow(const SymbolRule &rule)
{
    const int row = m_symbolsTable->rowCount();
    m_symbolsTable->insertRow(row);
    auto *onHolder = new QWidget;
    auto *onLayout = new QHBoxLayout(onHolder);
    onLayout->setContentsMargins(0, 0, 0, 0);
    onLayout->setAlignment(Qt::AlignCenter);
    auto *on = new QCheckBox;
    on->setChecked(rule.enabled);
    onLayout->addWidget(on);
    m_symbolsTable->setCellWidget(row, 0, onHolder);
    m_symbolsTable->setItem(row, 1, new QTableWidgetItem(rule.sequence));
    auto *sym = new QTableWidgetItem(rule.symbol);
    QFont f = sym->font();
    f.setPointSize(f.pointSize() + 2);
    sym->setFont(f);
    m_symbolsTable->setItem(row, 2, sym);
}

QWidget *SettingsDialog::buildAppearancePage()
{
    m_themes = new QListWidget;
    m_themes->setObjectName(QStringLiteral("ThemeList"));
    m_themes->setViewMode(QListView::IconMode);
    m_themes->setIconSize(QSize(120, 72));
    m_themes->setGridSize(QSize(150, 110));
    m_themes->setMovement(QListView::Static);
    m_themes->setResizeMode(QListView::Adjust);
    m_themes->setMinimumHeight(240);
    m_themes->setSpacing(6);
    auto makeSlider = [](QSlider *&slider, QLabel *&label, int min, int max) {
        slider = new QSlider(Qt::Horizontal);
        slider->setRange(min / 10, max / 10);
        slider->setPageStep(1);
        slider->setTickPosition(QSlider::TicksBelow);
        label = new QLabel;
        label->setMinimumWidth(48);
        QObject::connect(slider, &QSlider::valueChanged, label,
                         [label](int v) { label->setText(QStringLiteral("%1%").arg(v * 10)); });
        auto *row = new QHBoxLayout;
        row->addWidget(slider, 1);
        row->addWidget(label);
        return row;
    };

    auto *scale = new QFormLayout;
    scale->setHorizontalSpacing(24);
    m_toolbarIconSize = new QComboBox;
    for (const auto &[label, px] : QList<QPair<QString, int>>{{tr("Small (16x16)"), 16}, {tr("Medium (22x22)"), 22},
                                                              {tr("Large (32x32)"), 32}, {tr("Huge (48x48)"), 48}})
        m_toolbarIconSize->addItem(label, px);
    m_toolbarIconSize->setToolTip(tr("More options: right-click the toolbar"));
    scale->addRow(tr("Toolbar icons"), m_toolbarIconSize);
    scale->addRow(tr("Interface"), makeSlider(m_uiScale, m_uiScaleLabel, 80, 200));
    m_minimapWidth = new QSpinBox;
    m_minimapWidth->setRange(20, 400);
    m_minimapWidth->setSuffix(tr(" px"));
    m_minimapWidth->setToolTip(tr("Width of the code minimap (View ▸ Show Minimap)"));
    scale->addRow(tr("Minimap width"), m_minimapWidth);

    m_uiLanguage = new QComboBox;
    m_uiLanguage->addItem(tr("Automatic (system language)"), QStringLiteral("auto"));
    m_uiLanguage->addItem(QStringLiteral("English"), QStringLiteral("en"));
    m_uiLanguage->addItem(QStringLiteral("Português (Brasil)"), QStringLiteral("pt_BR"));
    auto *languageForm = new QFormLayout;
    languageForm->setHorizontalSpacing(24);
    languageForm->addRow(tr("Interface language"), m_uiLanguage);

    auto *customize = new QPushButton(tr("Customize…"));
    customize->setAutoDefault(false);
    customize->setToolTip(tr("Create your own themes or change the colors of the built-in ones"));
    connect(customize, &QPushButton::clicked, this, [this]() {
        ThemeEditor editor(m_userThemes, currentThemeId(), m_theme, this);
        if (editor.exec() == QDialog::Accepted) {
            m_userThemes = editor.userThemes();
            rebuildThemeList(editor.selectedId());
        }
    });
    auto *customizeRow = new QHBoxLayout;
    auto *customizeHint = new QLabel(tr("✦ your themes   ✎ customized built-in themes"));
    customizeHint->setObjectName(QStringLiteral("Hint"));
    customizeRow->addWidget(customizeHint, 1);
    customizeRow->addWidget(customize);

    // Escuro / Claro: filtra a grade e troca o tema pelo seu par.
    m_modeDark = new QPushButton(tr("Dark"));
    m_modeLight = new QPushButton(tr("Light"));
    m_modeDark->setObjectName(QStringLiteral("SegmentLeft"));
    m_modeLight->setObjectName(QStringLiteral("SegmentRight"));
    for (QPushButton *b : {m_modeDark, m_modeLight}) {
        b->setCheckable(true);
        b->setAutoDefault(false);
    }
    m_modeDark->setIcon(Icons::icon(Icons::Moon, m_theme.textSecondary, 16));
    m_modeLight->setIcon(Icons::icon(Icons::Sun, m_theme.textSecondary, 16));
    connect(m_modeDark, &QPushButton::clicked, this, [this]() { setMode(true); });
    connect(m_modeLight, &QPushButton::clicked, this, [this]() { setMode(false); });
    m_followSystem = new QCheckBox(tr("Follow the system"));
    m_followSystem->setToolTip(tr("Use the light or dark variant of the chosen theme according to the desktop, "
                                  "switching automatically when it changes"));
    auto *modeRow = new QHBoxLayout;
    modeRow->setSpacing(0);
    modeRow->addWidget(m_modeDark);
    modeRow->addWidget(m_modeLight);
    modeRow->addSpacing(18);
    modeRow->addWidget(m_followSystem);
    modeRow->addStretch(1);

    auto *themeBox = new QVBoxLayout;
    themeBox->addLayout(modeRow);
    themeBox->addWidget(m_themes, 1);
    themeBox->addLayout(customizeRow);

    auto *layout = new QVBoxLayout;
    layout->setSpacing(14);
    layout->addWidget(card(tr("Theme"), themeBox), 1);
    layout->addWidget(card(tr("Scale"), scale));
    layout->addWidget(card(tr("Language"), languageForm));
    return makePage(tr("Appearance"), tr("The theme colors the interface, the editor and the terminal."), layout, true);
}

QWidget *SettingsDialog::buildTerminalPage()
{
    m_terminalFont = new QFontComboBox;
    m_terminalFont->setFontFilters(QFontComboBox::MonospacedFonts);
    m_terminalFontSize = new QSpinBox;
    m_terminalFontSize->setRange(6, 48);
    m_terminalFontSize->setSuffix(QStringLiteral(" pt"));
    m_shell = new QComboBox;
    m_shell->setEditable(true);
    m_shell->addItem(tr("Default ($SHELL)"), QString());
    for (const QString &s : availableShells())
        m_shell->addItem(s, s);
    m_terminalBlink = new QCheckBox(tr("Blinking cursor"));

    auto *form = new QFormLayout;
    form->setHorizontalSpacing(24);
    form->addRow(tr("Font"), m_terminalFont);
    form->addRow(tr("Size"), m_terminalFontSize);
    form->addRow(tr("Shell"), m_shell);
    form->addRow(QString(), m_terminalBlink);
    m_runFocus = new QComboBox;
    m_runFocus->addItem(tr("Terminal only when input is needed"), 2);
    m_runFocus->addItem(tr("Terminal (for programs that read input)"), 0);
    m_runFocus->addItem(tr("Editor"), 1);
    m_runFocus->setToolTip(tr("Interactive runs always focus the terminal. Otherwise the editor keeps the "
                              "focus until the program waits for keyboard input."));
    form->addRow(tr("Focus on run"), m_runFocus);

    m_terminalTransparency = new QSlider(Qt::Horizontal);
    m_terminalTransparency->setRange(0, 90);
    m_terminalTransparency->setSingleStep(5);
    m_terminalTransparency->setPageStep(10);
    m_terminalTransparencyLabel = new QLabel;
    m_terminalTransparencyLabel->setMinimumWidth(48);
    connect(m_terminalTransparency, &QSlider::valueChanged, this, [this](int v) {
        m_terminalTransparencyLabel->setText(v == 0 ? tr("Off") : QStringLiteral("%1%").arg(v));
    });
    auto *transparencyRow = new QHBoxLayout;
    transparencyRow->addWidget(m_terminalTransparency, 1);
    transparencyRow->addWidget(m_terminalTransparencyLabel);
    form->addRow(tr("Transparency"), transparencyRow);

    auto *hint = new QLabel(tr("Move the terminal to the left, top, right or bottom with the ▥ button on its "
                               "title bar, or drag it by the title. Typing “exit” starts a new shell.<br><br>"
                               "Transparency applies to the detached terminal (❐ button): the desktop shows "
                               "through it. Docked, the terminal stays opaque."));
    hint->setObjectName(QStringLiteral("Hint"));
    hint->setWordWrap(true);
    auto *tips = new QVBoxLayout;
    tips->addWidget(hint);

    auto *layout = new QVBoxLayout;
    layout->setSpacing(14);
    layout->addWidget(card(QString(), form));
    layout->addWidget(card(tr("Tip"), tips));
    return makePage(tr("Terminal"), QString(), layout);
}

QWidget *SettingsDialog::buildPreviewPage()
{
    m_previewCommand = new QLineEdit;
    m_previewCommand->setPlaceholderText(tr("Built-in server (leave empty), or e.g.  php -S {host}:{port}"));
    m_previewHost = new QLineEdit;
    m_previewPort = new QSpinBox;
    m_previewPort->setRange(1, 65535);
    m_previewBrowser = new QComboBox;
    m_previewBrowser->addItem(tr("System default"), QString());
    static const QList<QPair<QString, QString>> known{
        {QStringLiteral("firefox"), QStringLiteral("Firefox")},
        {QStringLiteral("chromium"), QStringLiteral("Chromium")},
        {QStringLiteral("chromium-browser"), QStringLiteral("Chromium")},
        {QStringLiteral("google-chrome"), QStringLiteral("Google Chrome")},
        {QStringLiteral("google-chrome-stable"), QStringLiteral("Google Chrome")},
        {QStringLiteral("brave-browser"), QStringLiteral("Brave")},
        {QStringLiteral("microsoft-edge"), QStringLiteral("Microsoft Edge")},
        {QStringLiteral("opera"), QStringLiteral("Opera")},
        {QStringLiteral("vivaldi"), QStringLiteral("Vivaldi")},
        {QStringLiteral("epiphany"), QStringLiteral("GNOME Web")},
        {QStringLiteral("falkon"), QStringLiteral("Falkon")},
        {QStringLiteral("konqueror"), QStringLiteral("Konqueror")},
    };
    QSet<QString> seen;
    for (const auto &b : known) {
        if (seen.contains(b.second) || QStandardPaths::findExecutable(b.first).isEmpty())
            continue;
        seen.insert(b.second);
        m_previewBrowser->addItem(b.second, b.first);
    }
    m_previewReload = new QComboBox;
    m_previewReload->addItem(tr("On all changes in the editor"), 1);
    m_previewReload->addItem(tr("Never"), 0);
    m_previewReload->addItem(tr("Every N seconds"), 2);
    m_previewInterval = new QSpinBox;
    m_previewInterval->setRange(1, 3600);
    m_previewInterval->setSuffix(tr(" s"));
    connect(m_previewReload, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this]() { m_previewInterval->setEnabled(m_previewReload->currentData().toInt() == 2); });

    m_previewHint = new QLabel;
    m_previewHint->setWordWrap(true);
    m_previewHint->setObjectName(QStringLiteral("EmptyHint"));
    // Aviso dinâmico de qual servidor está em uso + desabilita host/port quando o
    // comando já traz o próprio endereço (sem {host}/{port}).
    static const QRegularExpression cmdAddr(QStringLiteral("(\\[[^\\]]+\\]|[A-Za-z0-9_.-]+)?:(\\d{2,5})(?!\\d)"));
    auto updateHint = [this]() {
        const QString cmd = m_previewCommand->text().trimmed();
        if (cmd.isEmpty()) {
            m_previewHint->setText(tr("Default: built-in live-preview server. The browser opens at the "
                                      "host and port above, with live reload."));
            return;
        }
        // Se o comando traz host:porta, é esse o endereço aberto (o backend roda
        // numa porta interna); senão, abre no Host/porta acima.
        const auto m = cmdAddr.match(cmd);
        if (m.hasMatch() && !cmd.contains(QLatin1String("{port}"))) {
            QString h = m.captured(1);
            if (h.isEmpty() || h == QLatin1String("0.0.0.0"))
                h = QStringLiteral("127.0.0.1");
            m_previewHint->setText(tr("Backend: %1. The preview opens at %2:%3 (from the command) with live "
                                      "reload; the Host/port above is used only when the command has no port.")
                                       .arg(cmd, h, m.captured(2)));
        } else {
            m_previewHint->setText(tr("Backend: %1. Slayer serves at the host and port above and forwards "
                                      "to it, adding live reload.").arg(cmd));
        }
    };
    connect(m_previewCommand, &QLineEdit::textChanged, this, [updateHint]() { updateHint(); });
    updateHint();

    auto *server = new QFormLayout;
    server->setHorizontalSpacing(24);
    server->addRow(tr("Server command"), m_previewCommand);
    server->addRow(QString(), m_previewHint);
    auto *hp = new QHBoxLayout;
    hp->addWidget(m_previewHost, 1);
    hp->addWidget(new QLabel(QStringLiteral(":")));
    hp->addWidget(m_previewPort);
    server->addRow(tr("Host / port"), hp);
    server->addRow(tr("Browser"), m_previewBrowser);
    m_previewCoi = new QCheckBox(tr("Cross-origin isolation (COOP/COEP)"));
    m_previewCoi->setToolTip(tr("Send the headers that enable SharedArrayBuffer — needed by in-browser "
                                "WASM apps that use threads (C/Python compilers, some games). May block "
                                "cross-origin resources that lack CORP/CORS."));
    server->addRow(QString(), m_previewCoi);

    auto *reload = new QFormLayout;
    reload->setHorizontalSpacing(24);
    reload->addRow(tr("Live reload"), m_previewReload);
    reload->addRow(tr("Interval"), m_previewInterval);

    auto *layout = new QVBoxLayout;
    layout->setSpacing(14);
    layout->addWidget(card(tr("Server"), server));
    layout->addWidget(card(tr("Live reload"), reload));
    return makePage(tr("Preview"),
                    tr("Run opens HTML in the browser. Leave the server command empty to use the "
                       "built-in live-preview server (best for static HTML/CSS/JS); set a command "
                       "like “php -S {host}:{port}” for a backend."),
                    layout);
}

/**
 * Linguagens e servidores de linguagem numa página só: lista à esquerda,
 * detalhes da linguagem escolhida à direita (execução e LSP).
 */
QWidget *SettingsDialog::buildLanguagesPage()
{
    m_langList = new QListWidget;
    m_langList->setObjectName(QStringLiteral("ProfileList"));
    m_langList->setFixedWidth(210);
    m_langList->setIconSize(QSize(16, 16));

    auto *add = new QPushButton(tr("Add Language…"));
    m_langRemove = new QPushButton(tr("Remove"));
    m_langReset = new QPushButton(tr("Reset"));
    for (QPushButton *b : {add, m_langRemove, m_langReset})
        b->setAutoDefault(false);
    m_langRemove->setObjectName(QStringLiteral("Danger"));

    // ── Execução ──
    m_langTool = new QLineEdit;
    m_langFlags = new QLineEdit;
    m_langExtensions = new QLineEdit;
    m_langTool->setPlaceholderText(tr("compiler or interpreter"));
    m_langExtensions->setPlaceholderText(tr("e.g. rb, rake"));
    auto *runForm = new QFormLayout;
    runForm->setHorizontalSpacing(18);
    runForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    runForm->addRow(tr("Tool"), m_langTool);
    runForm->addRow(tr("Flags"), m_langFlags);
    runForm->addRow(tr("Extensions"), m_langExtensions);
    m_langRunHint = new QLabel;
    m_langRunHint->setObjectName(QStringLiteral("Hint"));
    m_langRunHint->setWordWrap(true);
    m_langRunHint->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_langRunHint->setTextFormat(Qt::PlainText);
    runForm->addRow(QString(), m_langRunHint);

    // ── Servidor de linguagem ──
    m_lspOn = new QCheckBox(tr("Use a language server (diagnostics and completion)"));
    m_lspCommand = new QComboBox;
    m_lspCommand->setEditable(true);
    m_lspStatus = new QLabel;
    m_lspStatus->setWordWrap(true);
    m_lspStatus->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto *lspForm = new QFormLayout;
    lspForm->setHorizontalSpacing(18);
    lspForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    m_lspCommand->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    lspForm->addRow(m_lspOn);
    lspForm->addRow(tr("Server"), m_lspCommand);
    lspForm->addRow(tr("Status"), m_lspStatus);
    m_lspCard = card(tr("Language server (LSP)"), lspForm);

    auto *details = new QVBoxLayout;
    details->setSpacing(12);
    details->addWidget(card(tr("Run (F5)"), runForm));
    details->addWidget(m_lspCard);
    details->addStretch(1);

    auto *listBox = new QVBoxLayout;
    listBox->addWidget(m_langList, 1);
    auto *listButtons = new QHBoxLayout;
    listButtons->addWidget(add);
    listBox->addLayout(listButtons);
    auto *detailButtons = new QHBoxLayout;
    detailButtons->addStretch(1);
    detailButtons->addWidget(m_langReset);
    detailButtons->addWidget(m_langRemove);
    details->addLayout(detailButtons);

    auto *row = new QHBoxLayout;
    row->setSpacing(14);
    row->addLayout(listBox);
    row->addLayout(details, 1);

    m_missingLanguages = new QLabel;
    m_missingLanguages->setObjectName(QStringLiteral("Hint"));
    m_missingLanguages->setWordWrap(true);

    auto *layout = new QVBoxLayout;
    layout->addLayout(row, 1);
    layout->addWidget(m_missingLanguages);

    connect(m_langList, &QListWidget::currentRowChanged, this, [this](int r) {
        storeLanguageForm();
        m_currentEntry = r;
        loadLanguageForm();
    });
    // O que se digita vai para a entrada atual na hora (lista e status refletem).
    for (QLineEdit *e : {m_langTool, m_langFlags, m_langExtensions})
        connect(e, &QLineEdit::textEdited, this, [this]() { storeLanguageForm(); updateLanguageStatus(); });
    connect(m_lspOn, &QCheckBox::toggled, this, [this]() { storeLanguageForm(); updateLanguageStatus(); });
    connect(m_lspCommand, &QComboBox::currentTextChanged, this, [this]() {
        if (!m_loadingLanguage) {
            storeLanguageForm();
            updateLanguageStatus();
        }
    });

    connect(add, &QPushButton::clicked, this, [this]() {
        bool ok = false;
        const QString tool = QInputDialog::getText(
            this, tr("Add Language"),
            tr("Interpreter or compiler (e.g. ruby, lua, perl, php, Rscript):"), QLineEdit::Normal, QString(), &ok)
                                 .trimmed();
        if (!ok || tool.isEmpty())
            return;
        const QString ext = QInputDialog::getText(this, tr("Add Language"), tr("File extensions (e.g. rb):"),
                                                  QLineEdit::Normal, QString(), &ok)
                                .trimmed();
        if (!ok)
            return;
        storeLanguageForm();
        m_entries.append({QString(), tool, true, {tool, QString(), ext}, {}});
        refreshLanguageList(m_entries.size() - 1);
    });
    connect(m_langRemove, &QPushButton::clicked, this, [this]() {
        if (m_currentEntry < 0 || !m_entries[m_currentEntry].custom)
            return;
        m_entries.removeAt(m_currentEntry);
        m_currentEntry = -1;
        refreshLanguageList(0);
    });
    connect(m_langReset, &QPushButton::clicked, this, [this]() {
        if (m_currentEntry < 0 || m_entries[m_currentEntry].custom)
            return;
        LangEntry &e = m_entries[m_currentEntry];
        e.cfg = AppSettings::defaultLanguageConfig(e.id);
        e.lsp = LspConfig();
        loadLanguageForm();
    });

    return makePage(tr("Languages"),
                    tr("Languages found on this system. Run (F5) saves the file and runs “tool flags file” in the "
                       "terminal (C, C++, Rust… are compiled to /tmp/slayer_out first). The language server adds "
                       "diagnostics and completion; turn all servers off in Settings → Editor."),
                    layout, true);
}

void SettingsDialog::refreshLanguageList(int select)
{
    const QSignalBlocker blocker(m_langList);
    m_langList->clear();
    for (const LangEntry &e : std::as_const(m_entries)) {
        const QString iconId = e.custom ? QString() : e.id;
        auto *item = new QListWidgetItem(Icons::icon(Icons::forLanguage(iconId), Icons::colorForLanguage(iconId), 16),
                                         e.name);
        // Indicador do LSP: ● ativo e instalado, ○ ativo mas não encontrado.
        const QString cmd = effectiveLspCommand(e);
        if (!e.custom && e.lsp.enabled && !cmd.isEmpty())
            item->setText(e.name + (LspManager::isInstalled(cmd) ? QStringLiteral("   ●") : QStringLiteral("   ○")));
        m_langList->addItem(item);
    }
    m_currentEntry = qBound(-1, select, int(m_entries.size()) - 1);
    m_langList->setCurrentRow(m_currentEntry);
    loadLanguageForm();
}

QString SettingsDialog::effectiveLspCommand(const LangEntry &e) const
{
    if (!e.lsp.command.trimmed().isEmpty())
        return e.lsp.command.trimmed();
    const QList<LspPreset> presets = LspManager::presets(e.id);
    return presets.isEmpty() ? QString() : presets.first().command;
}

void SettingsDialog::loadLanguageForm()
{
    const bool has = m_currentEntry >= 0 && m_currentEntry < m_entries.size();
    for (QWidget *w : std::initializer_list<QWidget *>{m_langTool, m_langFlags, m_langExtensions, m_lspCard,
                                                       m_langReset, m_langRemove})
        w->setEnabled(has);
    if (!has)
        return;
    m_loadingLanguage = true;
    const LangEntry &e = m_entries[m_currentEntry];
    m_langTool->setText(e.cfg.tool);
    m_langFlags->setText(e.cfg.flags);
    m_langExtensions->setText(e.cfg.extensions);
    m_langReset->setVisible(!e.custom);
    m_langRemove->setVisible(e.custom);

    const QList<LspPreset> presets = e.custom ? QList<LspPreset>() : LspManager::presets(e.id);
    m_lspCard->setVisible(!presets.isEmpty());
    m_lspCommand->clear();
    for (const LspPreset &p : presets)
        m_lspCommand->addItem(QStringLiteral("%1 — %2").arg(p.name, p.command), p.command);
    m_lspOn->setChecked(e.lsp.enabled);
    const QString cmd = effectiveLspCommand(e);
    const int idx = m_lspCommand->findData(cmd);
    if (idx >= 0)
        m_lspCommand->setCurrentIndex(idx);
    else
        m_lspCommand->setEditText(cmd);
    m_loadingLanguage = false;
    updateLanguageStatus();
}

/** Grava o formulário na entrada atual. */
void SettingsDialog::storeLanguageForm()
{
    if (m_loadingLanguage || m_currentEntry < 0 || m_currentEntry >= m_entries.size())
        return;
    LangEntry &e = m_entries[m_currentEntry];
    e.cfg = {m_langTool->text().trimmed(), m_langFlags->text().trimmed(), m_langExtensions->text().trimmed()};
    if (e.custom && !e.cfg.tool.isEmpty())
        e.name = e.name.isEmpty() ? e.cfg.tool : e.name;
    e.lsp.enabled = m_lspOn->isChecked();
    // Item da lista guarda o comando; texto digitado vale como comando próprio.
    const int idx = m_lspCommand->currentIndex();
    QString cmd = (idx >= 0 && m_lspCommand->itemText(idx) == m_lspCommand->currentText())
                      ? m_lspCommand->itemData(idx).toString()
                      : m_lspCommand->currentText().trimmed();
    const QList<LspPreset> presets = LspManager::presets(e.id);
    if (!presets.isEmpty() && cmd == presets.first().command)
        cmd.clear(); // padrão: acompanha mudanças futuras do preset
    e.lsp.command = cmd;
}

void SettingsDialog::updateLanguageStatus()
{
    if (m_currentEntry < 0 || m_currentEntry >= m_entries.size())
        return;
    const LangEntry &e = m_entries[m_currentEntry];
    // Comando de execução resultante, para conferência.
    const Language lang = e.custom ? Language::custom(e.name, Language::splitExtensions(e.cfg.extensions))
                                   : Language::byId(e.id);
    const QString example = Language::splitExtensions(e.cfg.extensions).value(0, QStringLiteral("txt"));
    m_langRunHint->setText(tr("Runs: %1")
                               .arg(lang.commandFor(QStringLiteral("/dir/file.") + example, e.cfg.tool, e.cfg.flags)));

    const QString cmd = effectiveLspCommand(e);
    m_lspCommand->setEnabled(e.lsp.enabled);
    if (!e.lsp.enabled) {
        m_lspStatus->setText(tr("Off"));
        m_lspStatus->setStyleSheet(QString());
    } else if (LspManager::isInstalled(cmd)) {
        m_lspStatus->setText(tr("Installed"));
        m_lspStatus->setStyleSheet(QStringLiteral("color: #a6e3a1;"));
    } else {
        QString install;
        for (const LspPreset &p : LspManager::presets(e.id)) {
            if (p.command == cmd)
                install = p.install;
        }
        m_lspStatus->setText(install.isEmpty() ? tr("Not found")
                                               : tr("Not found — install with: %1").arg(install));
        m_lspStatus->setStyleSheet(QStringLiteral("color: #f38ba8;"));
    }
    // Marcador ●/○ da lista acompanha a mudança.
    if (QListWidgetItem *item = m_langList->item(m_currentEntry)) {
        QString text = e.name;
        if (!e.custom && e.lsp.enabled && !cmd.isEmpty())
            text += LspManager::isInstalled(cmd) ? QStringLiteral("   ●") : QStringLiteral("   ○");
        item->setText(text);
    }
}

QString SettingsDialog::currentThemeId() const
{
    auto *item = m_themes->currentItem();
    return item ? item->data(kIdRole).toString() : Theme::builtIns().first().id;
}

void SettingsDialog::setMode(bool dark)
{
    // O tema atual vai para a variante do modo escolhido (Mocha ↔ Latte…).
    const QList<Theme> themes = Theme::compose(m_userThemes);
    QString id = currentThemeId();
    for (const Theme &t : themes) {
        if (t.id == id && t.dark != dark && !t.counterpart.isEmpty())
            id = t.counterpart;
    }
    m_showDark = dark;
    rebuildThemeList(id);
}

/** Miniaturas dos temas (do app, personalizados e do usuário) do modo atual. */
void SettingsDialog::rebuildThemeList(const QString &selectId)
{
    const QList<Theme> themes = Theme::compose(m_userThemes);
    for (const Theme &t : themes) {
        if (t.id == selectId)
            m_showDark = t.dark; // o tema selecionado define o modo exibido
    }
    m_modeDark->setChecked(m_showDark);
    m_modeLight->setChecked(!m_showDark);
    m_themes->clear();
    for (const Theme &t : themes) {
        if (t.dark != m_showDark)
            continue;
        // Miniatura: fundo, barra de título e três linhas de "código".
        QPixmap pm(120, 72);
        pm.fill(t.surface);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        p.fillRect(0, 0, 120, 12, t.window);
        p.fillRect(0, 56, 120, 16, t.terminalBackground);
        const QColor colors[] = {t.keyword, t.string, t.function, t.comment};
        for (int i = 0; i < 4; ++i) {
            p.setBrush(colors[i]);
            p.setPen(Qt::NoPen);
            p.drawRoundedRect(10, 17 + i * 9, 30 + (i * 23) % 60, 5, 2, 2);
        }
        p.setBrush(t.primary);
        p.drawEllipse(QPoint(110, 6), 3, 3);
        p.end();
        QString label = t.name;
        bool customized = false;
        for (const Theme &u : std::as_const(m_userThemes))
            customized |= (u.id == t.id);
        if (!Theme::isBuiltIn(t.id))
            label += QStringLiteral(" ✦");
        else if (customized)
            label += QStringLiteral(" ✎");
        auto *item = new QListWidgetItem(QIcon(pm), label);
        item->setData(kIdRole, t.id);
        m_themes->addItem(item);
        if (t.id == selectId)
            m_themes->setCurrentItem(item);
    }
    if (!m_themes->currentItem() && m_themes->count() > 0)
        m_themes->setCurrentRow(0);
}

// ---------------------------------------------------------- formulário

void SettingsDialog::refreshProfiles()
{
    const QString active = AppSettings::activeProfile();
    m_profiles->clear();
    for (const QString &name : AppSettings::profileNames()) {
        auto *item = new QListWidgetItem(name);
        if (name == active) {
            QFont f = item->font();
            f.setBold(true);
            item->setFont(f);
            item->setIcon(Icons::icon(Icons::Check, m_theme.primary, 14));
        }
        m_profiles->addItem(item);
    }
    m_activeProfile->setText(active.isEmpty() ? tr("No active profile") : tr("Active: %1").arg(active));
    if (m_profiles->count() == 0)
        m_activeProfile->setText(tr("No saved profiles yet."));
}

void SettingsDialog::setForm(const AppSettings &s)
{
    QFont ef(s.editorFont.isEmpty() ? Icons::monoFamily() : s.editorFont);
    m_editorFont->setCurrentFont(ef);
    m_editorFontSize->setValue(s.editorFontSize);
    m_tabWidth->setValue(s.tabWidth);
    m_lineNumbers->setChecked(s.lineNumbers);
    m_wordWrap->setChecked(s.wordWrap);
    m_wrapIndent->setChecked(s.wrapIndent);
    m_wrapIndentExtra->setValue(s.wrapIndentExtra);
    m_highlightLine->setChecked(s.highlightCurrentLine);
    m_minimap->setChecked(s.minimap);
    m_autoPairs->setChecked(s.autoClosePairs);
    m_completion->setChecked(s.codeCompletion);
    m_folding->setChecked(s.codeFolding);
    m_lspEnabled->setChecked(s.lspEnabled);
    m_insertSpaces->setChecked(s.insertSpaces);
    m_prettySymbols->setChecked(s.prettySymbols);
    m_symbolsTable->setEnabled(s.prettySymbols);
    m_symbolsTable->setRowCount(0);
    const QList<SymbolRule> rules = s.symbols.isEmpty() ? AppSettings::defaultSymbols() : s.symbols;
    for (const SymbolRule &r : rules)
        addSymbolRow(r);
    m_editorBlink->setChecked(s.editorCursorBlink);

    m_userThemes.clear();
    for (const QJsonValue &v : s.userThemes)
        m_userThemes << Theme::fromJson(v.toObject());
    rebuildThemeList(s.theme);
    m_followSystem->setChecked(s.themeFollowSystem);
    m_toolbarIconSize->setCurrentIndex(qMax(0, m_toolbarIconSize->findData(s.toolbarIconSize)));
    m_minimapWidth->setValue(s.minimapWidth);
    m_uiScale->setValue(s.uiScale / 10);
    m_uiScaleLabel->setText(QStringLiteral("%1%").arg(m_uiScale->value() * 10));
    m_uiLanguage->setCurrentIndex(qMax(0, m_uiLanguage->findData(s.uiLanguage)));

    m_terminalFont->setCurrentFont(QFont(s.terminalFont.isEmpty() ? Icons::monoFamily() : s.terminalFont));
    m_terminalFontSize->setValue(s.terminalFontSize);
    const int shellIndex = m_shell->findData(s.shell);
    if (shellIndex >= 0)
        m_shell->setCurrentIndex(shellIndex);
    else
        m_shell->setEditText(s.shell);
    m_terminalBlink->setChecked(s.terminalCursorBlink);
    m_terminalTransparency->setValue(s.terminalTransparency);

    m_autoSave->setChecked(s.autoSave);
    m_previewCommand->setText(s.previewCommand);
    m_previewHost->setText(s.previewHost);
    m_previewPort->setValue(s.previewPort);
    m_previewBrowser->setCurrentIndex(qMax(0, m_previewBrowser->findData(s.previewBrowser)));
    m_previewReload->setCurrentIndex(qMax(0, m_previewReload->findData(s.previewReload)));
    m_runFocus->setCurrentIndex(qMax(0, m_runFocus->findData(s.runFocus)));
    m_previewInterval->setValue(s.previewReloadInterval);
    m_previewCoi->setChecked(s.previewCrossOriginIsolation);
    m_previewInterval->setEnabled(s.previewReload == 2);
    m_terminalTransparencyLabel->setText(s.terminalTransparency == 0 ? tr("Off")
                                                                      : QStringLiteral("%1%").arg(s.terminalTransparency));

    // Só as linguagens instaladas no sistema; as demais guardam o que tinham.
    m_entries.clear();
    m_hiddenLanguages.clear();
    m_hiddenLsp.clear();
    QStringList missing;
    for (const Language &lang : Language::all()) {
        if (s.languageInstalled(lang.id)) {
            m_entries.append({lang.id, lang.displayName, false, s.languageConfig(lang.id), s.lsp.value(lang.id)});
        } else {
            missing << lang.displayName;
            if (s.languages.contains(lang.id))
                m_hiddenLanguages.insert(lang.id, s.languages.value(lang.id));
            if (s.lsp.contains(lang.id))
                m_hiddenLsp.insert(lang.id, s.lsp.value(lang.id));
        }
    }
    for (const CustomLanguage &c : s.customLanguages)
        m_entries.append({QString(), c.name, true, {c.tool, c.flags, c.extensions}, {}});
    m_missingLanguages->setText(missing.isEmpty() ? QString()
                                                  : tr("Not installed on this system (hidden): %1. Install a "
                                                       "compiler or interpreter and it appears here.")
                                                        .arg(missing.join(QStringLiteral(", "))));
    m_currentEntry = -1;
    refreshLanguageList(0);
}

AppSettings SettingsDialog::settings() const
{
    AppSettings s;
    const QString ef = m_editorFont->currentFont().family();
    s.editorFont = (ef == Icons::monoFamily()) ? QString() : ef;
    s.editorFontSize = m_editorFontSize->value();
    s.tabWidth = m_tabWidth->value();
    s.lineNumbers = m_lineNumbers->isChecked();
    s.wordWrap = m_wordWrap->isChecked();
    s.wrapIndent = m_wrapIndent->isChecked();
    s.wrapIndentExtra = m_wrapIndentExtra->value();
    s.highlightCurrentLine = m_highlightLine->isChecked();
    s.minimap = m_minimap->isChecked();
    s.autoClosePairs = m_autoPairs->isChecked();
    s.codeCompletion = m_completion->isChecked();
    s.codeFolding = m_folding->isChecked();
    s.lspEnabled = m_lspEnabled->isChecked();
    s.insertSpaces = m_insertSpaces->isChecked();
    s.prettySymbols = m_prettySymbols->isChecked();
    for (int row = 0; row < m_symbolsTable->rowCount(); ++row) {
        const bool on = m_symbolsTable->cellWidget(row, 0)->findChild<QCheckBox *>()->isChecked();
        const QString seq = m_symbolsTable->item(row, 1) ? m_symbolsTable->item(row, 1)->text() : QString();
        const QString sym = m_symbolsTable->item(row, 2) ? m_symbolsTable->item(row, 2)->text() : QString();
        if (!seq.isEmpty() && !sym.isEmpty())
            s.symbols.append({seq, sym, on});
    }
    // Igual ao padrão? Não grava, para acompanhar mudanças futuras do padrão.
    if (s.symbols == AppSettings::defaultSymbols())
        s.symbols.clear();
    s.editorCursorBlink = m_editorBlink->isChecked();

    if (auto *item = m_themes->currentItem())
        s.theme = item->data(kIdRole).toString();
    for (const Theme &t : m_userThemes)
        s.userThemes.append(t.toJson());
    s.themeFollowSystem = m_followSystem->isChecked();
    s.toolbarIconSize = m_toolbarIconSize->currentData().toInt();
    s.minimapWidth = m_minimapWidth->value();
    // Opções da barra que não estão neste formulário (botão direito nela).
    s.toolbarStyle = m_detect.toolbarStyle;
    s.toolbarLocked = m_detect.toolbarLocked;
    s.toolbarActions = m_detect.toolbarActions;
    s.uiScale = m_uiScale->value() * 10;
    s.uiLanguage = m_uiLanguage->currentData().toString();

    const QString tf = m_terminalFont->currentFont().family();
    s.terminalFont = (tf == Icons::monoFamily()) ? QString() : tf;
    s.terminalFontSize = m_terminalFontSize->value();
    const int shellIndex = m_shell->currentIndex();
    s.shell = (shellIndex >= 0 && m_shell->itemText(shellIndex) == m_shell->currentText())
                  ? m_shell->itemData(shellIndex).toString()
                  : m_shell->currentText().trimmed();
    s.terminalCursorBlink = m_terminalBlink->isChecked();
    s.terminalTransparency = m_terminalTransparency->value();

    s.autoSave = m_autoSave->isChecked();
    s.previewCommand = m_previewCommand->text().trimmed();
    s.previewHost = m_previewHost->text().trimmed().isEmpty() ? QStringLiteral("127.0.0.1")
                                                              : m_previewHost->text().trimmed();
    s.previewPort = m_previewPort->value();
    s.previewBrowser = m_previewBrowser->currentData().toString();
    s.previewReload = m_previewReload->currentData().toInt();
    s.runFocus = m_runFocus->currentData().toInt();
    s.previewReloadInterval = m_previewInterval->value();
    s.previewCrossOriginIsolation = m_previewCoi->isChecked();
    // Preserva opções fora deste formulário.
    s.autoReload = m_detect.autoReload;

    const_cast<SettingsDialog *>(this)->storeLanguageForm();
    s.languages = m_hiddenLanguages;
    s.lsp = m_hiddenLsp;
    for (const LangEntry &e : m_entries) {
        if (e.custom) {
            if (!e.name.trimmed().isEmpty())
                s.customLanguages.append({e.name.trimmed(), e.cfg.tool.isEmpty() ? e.name : e.cfg.tool, e.cfg.flags,
                                          e.cfg.extensions});
            continue;
        }
        // Só guarda o que difere do padrão: padrões futuros continuam valendo.
        const LanguageConfig def = AppSettings::defaultLanguageConfig(e.id);
        if (e.cfg.tool != def.tool || e.cfg.flags != def.flags || e.cfg.extensions != def.extensions)
            s.languages.insert(e.id, e.cfg);
        if (!e.lsp.enabled || !e.lsp.command.isEmpty())
            s.lsp.insert(e.id, e.lsp);
    }

    return s;
}
