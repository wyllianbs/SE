#include "SettingsDialog.h"
#include "Icons.h"
#include "Language.h"
#include "LspManager.h"

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
    addPage(tr("Languages"), Icons::Languages, buildLanguagesPage());
    addPage(tr("Language Servers"), Icons::Server, buildServersPage());
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
    m_editorBlink = new QCheckBox(tr("Blinking cursor"));
    auto *display = new QVBoxLayout;
    for (QCheckBox *c : {m_lineNumbers, m_wordWrap, m_highlightLine, m_editorBlink})
        display->addWidget(c);

    m_autoPairs = new QCheckBox(tr("Auto-close brackets and quotes  ( )  [ ]  { }  \" \"  ' '"));
    m_completion = new QCheckBox(tr("Code completion (words in the file + language keywords, Ctrl+Space)"));
    m_folding = new QCheckBox(tr("Code folding (arrows next to the line numbers, Ctrl+Shift+[ / ])"));
    m_lspEnabled = new QCheckBox(tr("Language servers (LSP): diagnostics and smarter completion"));
    auto *typing = new QVBoxLayout;
    typing->addWidget(m_autoPairs);
    typing->addWidget(m_completion);
    typing->addWidget(m_folding);
    typing->addWidget(m_lspEnabled);

    auto *layout = new QVBoxLayout;
    layout->setSpacing(14);
    layout->addWidget(card(tr("Font"), font));
    layout->addWidget(card(tr("Display"), display));
    layout->addWidget(card(tr("Features"), typing));
    return makePage(tr("Editor"), QString(), layout);
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
    for (const Theme &t : Theme::all()) {
        // Miniatura: fundo, barra lateral e três linhas de "código".
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
        auto *item = new QListWidgetItem(QIcon(pm), t.name);
        item->setData(kIdRole, t.id);
        m_themes->addItem(item);
    }

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
    scale->addRow(tr("Toolbar buttons"), makeSlider(m_buttonScale, m_buttonScaleLabel, 80, 200));
    scale->addRow(tr("Interface"), makeSlider(m_uiScale, m_uiScaleLabel, 80, 200));

    m_uiLanguage = new QComboBox;
    m_uiLanguage->addItem(tr("Automatic (system language)"), QStringLiteral("auto"));
    m_uiLanguage->addItem(QStringLiteral("English"), QStringLiteral("en"));
    m_uiLanguage->addItem(QStringLiteral("Português (Brasil)"), QStringLiteral("pt_BR"));
    auto *languageForm = new QFormLayout;
    languageForm->setHorizontalSpacing(24);
    languageForm->addRow(tr("Interface language"), m_uiLanguage);

    auto *themeBox = new QVBoxLayout;
    themeBox->addWidget(m_themes, 1);

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

QWidget *SettingsDialog::buildLanguagesPage()
{
    m_languages = new QTableWidget(0, 4);
    m_languages->setObjectName(QStringLiteral("LanguageTable"));
    m_languages->setHorizontalHeaderLabels({tr("Language"), tr("Tool"), tr("Flags"), tr("Extensions")});
    m_languages->verticalHeader()->hide();
    m_languages->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_languages->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_languages->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_languages->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_languages->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_languages->setSelectionMode(QAbstractItemView::SingleSelection);
    m_languages->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed
                                 | QAbstractItemView::AnyKeyPressed);
    m_languages->setMinimumHeight(220);
    m_languages->setShowGrid(false);
    m_languages->setAlternatingRowColors(true);

    auto *add = new QPushButton(tr("Add Language…"));
    auto *remove = new QPushButton(tr("Remove"));
    auto *reset = new QPushButton(tr("Reset Row"));
    for (QPushButton *b : {add, remove, reset})
        b->setAutoDefault(false);
    remove->setObjectName(QStringLiteral("Danger"));

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
        addLanguageRow(QString(), tool, {tool, QString(), ext}, true);
        m_languages->selectRow(m_languages->rowCount() - 1);
    });
    connect(remove, &QPushButton::clicked, this, [this]() {
        const int row = m_languages->currentRow();
        if (row >= 0 && m_languages->item(row, 0)->data(kCustomRole).toBool())
            m_languages->removeRow(row);
    });
    connect(reset, &QPushButton::clicked, this, [this]() {
        const int row = m_languages->currentRow();
        if (row < 0 || m_languages->item(row, 0)->data(kCustomRole).toBool())
            return;
        const LanguageConfig def = AppSettings::defaultLanguageConfig(m_languages->item(row, 0)->data(kIdRole).toString());
        m_languages->item(row, 1)->setText(def.tool);
        m_languages->item(row, 2)->setText(def.flags);
        m_languages->item(row, 3)->setText(def.extensions);
    });
    connect(m_languages, &QTableWidget::currentCellChanged, this, [this, remove, reset](int row) {
        const bool custom = row >= 0 && m_languages->item(row, 0)->data(kCustomRole).toBool();
        remove->setEnabled(custom);
        reset->setEnabled(row >= 0 && !custom);
    });

    remove->setEnabled(false);
    reset->setEnabled(false);
    auto *buttons = new QHBoxLayout;
    buttons->addWidget(add);
    buttons->addStretch(1);
    buttons->addWidget(reset);
    buttons->addWidget(remove);

    auto *box = new QVBoxLayout;
    box->addWidget(m_languages, 1);
    box->addLayout(buttons);

    auto *layout = new QVBoxLayout;
    layout->addWidget(card(QString(), box), 1);
    return makePage(tr("Languages"),
                    tr("Run (F5) saves the file and runs “tool flags file” in the terminal. C, C++ and Rust are "
                       "compiled to /tmp/slayer_out and then executed. Double-click a cell to edit it."),
                    layout, true);
}

QWidget *SettingsDialog::buildServersPage()
{
    m_servers = new QTableWidget(0, 4);
    m_servers->setObjectName(QStringLiteral("LanguageTable"));
    m_servers->setHorizontalHeaderLabels({tr("Language"), tr("On"), tr("Server command"), tr("Status")});
    m_servers->verticalHeader()->hide();
    m_servers->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_servers->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Fixed);
    m_servers->setColumnWidth(1, 56);
    m_servers->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_servers->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_servers->setSelectionMode(QAbstractItemView::NoSelection);
    m_servers->setFocusPolicy(Qt::NoFocus);
    m_servers->setShowGrid(false);
    m_servers->setAlternatingRowColors(true);
    m_servers->setMinimumHeight(220);
    m_servers->verticalHeader()->setDefaultSectionSize(38);

    for (const QString &id : LspManager::languages()) {
        const int row = m_servers->rowCount();
        m_servers->insertRow(row);
        auto *name = new QTableWidgetItem(Icons::icon(Icons::forLanguage(id), Icons::colorForLanguage(id), 16),
                                          Language::byId(id).displayName);
        name->setData(kIdRole, id);
        name->setFlags(Qt::ItemIsEnabled);
        m_servers->setItem(row, 0, name);

        auto *enabledHolder = new QWidget;
        auto *enabledLayout = new QHBoxLayout(enabledHolder);
        enabledLayout->setContentsMargins(0, 0, 0, 0);
        enabledLayout->setAlignment(Qt::AlignCenter);
        auto *enabledBox = new QCheckBox;
        enabledBox->setToolTip(tr("Use a language server for %1").arg(Language::byId(id).displayName));
        enabledLayout->addWidget(enabledBox);
        m_servers->setCellWidget(row, 1, enabledHolder);

        // Servidores conhecidos + campo livre para outro comando.
        auto *combo = new QComboBox;
        combo->setEditable(true);
        for (const LspPreset &p : LspManager::presets(id))
            combo->addItem(p.command, p.install);
        connect(combo, &QComboBox::currentTextChanged, this, [this]() { refreshServerStatus(); });
        m_servers->setCellWidget(row, 2, combo);
        m_servers->setItem(row, 3, new QTableWidgetItem);
    }

    auto *hint = new QLabel(tr("Servers start when a file of their language is opened. Without compile_commands.json, "
                               "clangd uses the C/C++ flags from Settings → Languages. Install commands are shown "
                               "in the status tooltip; nothing is installed automatically."));
    hint->setObjectName(QStringLiteral("Hint"));
    hint->setWordWrap(true);
    auto *box = new QVBoxLayout;
    box->addWidget(m_servers, 1);
    box->addWidget(hint);
    auto *layout = new QVBoxLayout;
    layout->addWidget(card(QString(), box), 1);
    return makePage(tr("Language Servers"),
                    tr("Diagnostics (wavy underlines) and completion from a Language Server Protocol server. "
                       "Turn all servers on or off in Settings → Editor."),
                    layout, true);
}

void SettingsDialog::refreshServerStatus()
{
    for (int row = 0; row < m_servers->rowCount(); ++row) {
        auto *combo = qobject_cast<QComboBox *>(m_servers->cellWidget(row, 2));
        QTableWidgetItem *status = m_servers->item(row, 3);
        if (!combo || !status)
            continue;
        const QString command = combo->currentText().trimmed();
        const bool installed = LspManager::isInstalled(command);
        status->setText(installed ? tr("Installed") : tr("Not found"));
        status->setForeground(installed ? QColor("#a6e3a1") : QColor("#f38ba8"));
        QString install;
        for (const LspPreset &p : LspManager::presets(m_servers->item(row, 0)->data(kIdRole).toString())) {
            if (p.command == command)
                install = p.install;
        }
        status->setToolTip(installed || install.isEmpty() ? QString() : tr("Install with:\n%1").arg(install));
    }
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

void SettingsDialog::addLanguageRow(const QString &id, const QString &name, const LanguageConfig &cfg, bool custom)
{
    const int row = m_languages->rowCount();
    m_languages->insertRow(row);
    auto *nameItem = new QTableWidgetItem(name);
    nameItem->setData(kIdRole, id);
    nameItem->setData(kCustomRole, custom);
    const QString iconId = custom ? QString() : id;
    nameItem->setIcon(Icons::icon(Icons::forLanguage(iconId), Icons::colorForLanguage(iconId), 16));
    if (!custom)
        nameItem->setFlags(nameItem->flags() & ~Qt::ItemIsEditable);
    m_languages->setItem(row, 0, nameItem);
    m_languages->setItem(row, 1, new QTableWidgetItem(cfg.tool));
    auto *flagsItem = new QTableWidgetItem(cfg.flags);
    flagsItem->setToolTip(cfg.flags);
    m_languages->setItem(row, 2, flagsItem);
    m_languages->setItem(row, 3, new QTableWidgetItem(cfg.extensions));
}

void SettingsDialog::setForm(const AppSettings &s)
{
    QFont ef(s.editorFont.isEmpty() ? Icons::monoFamily() : s.editorFont);
    m_editorFont->setCurrentFont(ef);
    m_editorFontSize->setValue(s.editorFontSize);
    m_tabWidth->setValue(s.tabWidth);
    m_lineNumbers->setChecked(s.lineNumbers);
    m_wordWrap->setChecked(s.wordWrap);
    m_highlightLine->setChecked(s.highlightCurrentLine);
    m_autoPairs->setChecked(s.autoClosePairs);
    m_completion->setChecked(s.codeCompletion);
    m_folding->setChecked(s.codeFolding);
    m_lspEnabled->setChecked(s.lspEnabled);
    for (int row = 0; row < m_servers->rowCount(); ++row) {
        const LspConfig cfg = s.lspConfig(m_servers->item(row, 0)->data(kIdRole).toString());
        m_servers->cellWidget(row, 1)->findChild<QCheckBox *>()->setChecked(cfg.enabled);
        qobject_cast<QComboBox *>(m_servers->cellWidget(row, 2))->setEditText(cfg.command);
    }
    refreshServerStatus();
    m_insertSpaces->setChecked(s.insertSpaces);
    m_editorBlink->setChecked(s.editorCursorBlink);

    for (int i = 0; i < m_themes->count(); ++i) {
        if (m_themes->item(i)->data(kIdRole).toString() == s.theme)
            m_themes->setCurrentRow(i);
    }
    m_buttonScale->setValue(s.buttonScale / 10);
    m_uiScale->setValue(s.uiScale / 10);
    m_buttonScaleLabel->setText(QStringLiteral("%1%").arg(m_buttonScale->value() * 10));
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
    m_terminalTransparencyLabel->setText(s.terminalTransparency == 0 ? tr("Off")
                                                                      : QStringLiteral("%1%").arg(s.terminalTransparency));

    m_languages->setRowCount(0);
    for (const Language &lang : Language::all())
        addLanguageRow(lang.id, lang.displayName, s.languageConfig(lang.id), false);
    for (const CustomLanguage &c : s.customLanguages)
        addLanguageRow(QString(), c.name, {c.tool, c.flags, c.extensions}, true);

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
    s.highlightCurrentLine = m_highlightLine->isChecked();
    s.autoClosePairs = m_autoPairs->isChecked();
    s.codeCompletion = m_completion->isChecked();
    s.codeFolding = m_folding->isChecked();
    s.lspEnabled = m_lspEnabled->isChecked();
    for (int row = 0; row < m_servers->rowCount(); ++row) {
        const QString id = m_servers->item(row, 0)->data(kIdRole).toString();
        const bool on = m_servers->cellWidget(row, 1)->findChild<QCheckBox *>()->isChecked();
        QString command = qobject_cast<QComboBox *>(m_servers->cellWidget(row, 2))->currentText().trimmed();
        const QList<LspPreset> presets = LspManager::presets(id);
        if (!presets.isEmpty() && command == presets.first().command)
            command.clear(); // padrão: acompanha mudanças futuras do preset
        if (!on || !command.isEmpty())
            s.lsp.insert(id, {on, command});
    }
    s.insertSpaces = m_insertSpaces->isChecked();
    s.editorCursorBlink = m_editorBlink->isChecked();

    if (auto *item = m_themes->currentItem())
        s.theme = item->data(kIdRole).toString();
    s.buttonScale = m_buttonScale->value() * 10;
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

    for (int row = 0; row < m_languages->rowCount(); ++row) {
        const QTableWidgetItem *nameItem = m_languages->item(row, 0);
        const LanguageConfig cfg{m_languages->item(row, 1)->text().trimmed(), m_languages->item(row, 2)->text().trimmed(),
                                 m_languages->item(row, 3)->text().trimmed()};
        if (nameItem->data(kCustomRole).toBool()) {
            const QString name = nameItem->text().trimmed();
            if (!name.isEmpty())
                s.customLanguages.append({name, cfg.tool.isEmpty() ? name : cfg.tool, cfg.flags, cfg.extensions});
        } else {
            const QString id = nameItem->data(kIdRole).toString();
            // Só guarda o que difere do padrão: padrões futuros continuam valendo.
            const LanguageConfig def = AppSettings::defaultLanguageConfig(id);
            if (cfg.tool != def.tool || cfg.flags != def.flags || cfg.extensions != def.extensions)
                s.languages.insert(id, cfg);
        }
    }

    return s;
}
