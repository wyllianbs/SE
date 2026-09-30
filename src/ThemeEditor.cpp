#include "ThemeEditor.h"
#include "Language.h"
#include "SyntaxHighlighter.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QTextBlock>
#include <QToolButton>
#include <QUuid>
#include <QVBoxLayout>

namespace {

QIcon swatch(const QColor &c)
{
    QPixmap pm(QSize(28, 18) * 2);
    pm.setDevicePixelRatio(2);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QColor(128, 128, 128, 160));
    p.setBrush(c);
    p.drawRoundedRect(QRectF(0.5, 0.5, 27, 17), 5, 5);
    return QIcon(pm);
}

const char *kSample =
    "#include <stdio.h>\n"
    "\n"
    "/* Média das notas */\n"
    "double media(const double *v, int n)\n"
    "{\n"
    "    double s = 0.0;\n"
    "    for (int i = 0; i < n; i++)\n"
    "        s += v[i];  // soma\n"
    "    return n ? s / n : 0;\n"
    "}\n"
    "\n"
    "int main(void) { printf(\"%.2f\\n\", media((double[]){7, 8}, 2)); }\n";

} // namespace

ThemeEditor::ThemeEditor(const QList<Theme> &userThemes, const QString &currentId, const Theme &uiTheme,
                         QWidget *parent)
    : QDialog(parent)
    , m_user(userThemes)
    , m_list(new QListWidget(this))
    , m_name(new QLineEdit(this))
    , m_dark(new QCheckBox(tr("Dark theme"), this))
    , m_preview(new QPlainTextEdit(this))
    , m_terminalPreview(new QPlainTextEdit(this))
    , m_newButton(new QPushButton(tr("New From This…"), this))
    , m_restoreButton(new QPushButton(tr("Restore Original"), this))
    , m_deleteButton(new QPushButton(tr("Delete"), this))
{
    Q_UNUSED(uiTheme)
    setWindowTitle(tr("Customize Themes"));
    setObjectName(QStringLiteral("SettingsDialog"));
    resize(980, 660);

    m_list->setObjectName(QStringLiteral("SettingsNav"));
    m_list->setFixedWidth(220);

    // Cores agrupadas (Interface, Sintaxe, Terminal).
    auto *colors = new QWidget;
    auto *grid = new QGridLayout(colors);
    grid->setHorizontalSpacing(14);
    grid->setVerticalSpacing(6);
    int row = 0;
    QString group;
    for (const Theme::Field &f : Theme::fields()) {
        if (group != QLatin1String(f.group)) {
            group = QLatin1String(f.group);
            auto *title = new QLabel(QCoreApplication::translate("Theme", f.group));
            title->setObjectName(QStringLiteral("CardTitle"));
            grid->addWidget(title, row++, 0, 1, 2);
        }
        auto *button = new QToolButton;
        button->setIconSize(QSize(28, 18));
        button->setAutoRaise(true);
        grid->addWidget(new QLabel(QCoreApplication::translate("Theme", f.label)), row, 0);
        grid->addWidget(button, row++, 1, Qt::AlignLeft);
        QColor Theme::*member = f.member;
        connect(button, &QToolButton::clicked, this, [this, button, member]() { pickColor(button, &(m_current.*member)); });
        m_swatches.append({button, member});
    }
    auto *ansiTitle = new QLabel(tr("Terminal colors (ANSI 0–15)"));
    ansiTitle->setObjectName(QStringLiteral("Hint"));
    grid->addWidget(ansiTitle, row++, 0, 1, 2);
    auto *ansiBox = new QWidget;
    auto *ansiGrid = new QGridLayout(ansiBox);
    ansiGrid->setContentsMargins(0, 0, 0, 0);
    ansiGrid->setSpacing(2);
    for (int i = 0; i < 16; ++i) {
        auto *b = new QToolButton;
        b->setIconSize(QSize(28, 18));
        b->setAutoRaise(true);
        b->setToolTip(QString::number(i));
        ansiGrid->addWidget(b, i / 8, i % 8);
        connect(b, &QToolButton::clicked, this, [this, b, i]() { pickColor(b, &m_current.ansi[i]); });
        m_ansiSwatches << b;
    }
    grid->addWidget(ansiBox, row++, 0, 1, 2);
    grid->setRowStretch(row, 1);
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(colors);
    scroll->setFixedWidth(350);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    m_preview->setReadOnly(true);
    m_preview->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_preview->setPlainText(QString::fromUtf8(kSample));
    m_preview->setFont(QFont(QStringLiteral("JetBrainsMono Nerd Font"), 10));
    m_highlighter = new SyntaxHighlighter(m_preview->document());
    m_highlighter->setLanguage(Language::byId(QStringLiteral("c")));
    m_terminalPreview->setReadOnly(true);
    m_terminalPreview->setMaximumHeight(120);
    m_terminalPreview->setFont(m_preview->font());

    auto *header = new QHBoxLayout;
    header->addWidget(new QLabel(tr("Name")));
    header->addWidget(m_name, 1);
    header->addWidget(m_dark);

    auto *previewBox = new QVBoxLayout;
    previewBox->addLayout(header);
    auto *editorTitle = new QLabel(tr("Preview"));
    editorTitle->setObjectName(QStringLiteral("CardTitle"));
    previewBox->addWidget(editorTitle);
    previewBox->addWidget(m_preview, 1);
    previewBox->addWidget(m_terminalPreview);

    auto *actions = new QHBoxLayout;
    actions->addWidget(m_newButton);
    actions->addWidget(m_restoreButton);
    actions->addWidget(m_deleteButton);
    actions->addStretch(1);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    actions->addWidget(buttons);
    m_deleteButton->setObjectName(QStringLiteral("Danger"));
    for (QPushButton *b : {m_newButton, m_restoreButton, m_deleteButton})
        b->setAutoDefault(false);

    auto *right = new QVBoxLayout;
    auto *content = new QHBoxLayout;
    content->addWidget(scroll);
    content->addLayout(previewBox, 1);
    right->addLayout(content, 1);
    right->addLayout(actions);
    right->setContentsMargins(16, 16, 16, 12);

    auto *root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->addWidget(m_list);
    root->addLayout(right, 1);

    connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
        storeCurrent();
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_list, &QListWidget::currentRowChanged, this, [this](int r) {
        if (r < 0)
            return;
        storeCurrent();
        select(m_list->item(r)->data(Qt::UserRole).toString());
    });
    connect(m_name, &QLineEdit::textEdited, this, [this](const QString &t) {
        m_current.name = t.trimmed().isEmpty() ? m_current.name : t.trimmed();
        if (auto *item = m_list->currentItem())
            item->setText(m_current.name);
    });
    connect(m_dark, &QCheckBox::toggled, this, [this](bool on) { m_current.dark = on; });
    connect(m_newButton, &QPushButton::clicked, this, [this]() {
        storeCurrent();
        bool ok = false;
        const QString name = QInputDialog::getText(this, tr("New Theme"), tr("Theme name:"), QLineEdit::Normal,
                                                   tr("%1 (copy)").arg(m_current.name), &ok)
                                 .trimmed();
        if (!ok || name.isEmpty())
            return;
        Theme t = m_current;
        t.id = QStringLiteral("user-") + QUuid::createUuid().toString(QUuid::Id128).left(8);
        t.counterpart.clear(); // tema novo não herda o par do original
        t.name = name;
        m_user.append(t);
        refreshList();
        select(t.id);
    });
    connect(m_restoreButton, &QPushButton::clicked, this, [this]() {
        const QString id = m_current.id;
        m_user.removeIf([&id](const Theme &t) { return t.id == id; });
        for (const Theme &b : Theme::builtIns()) {
            if (b.id == id)
                m_current = b;
        }
        refreshList();
        select(id);
    });
    connect(m_deleteButton, &QPushButton::clicked, this, [this]() {
        if (QMessageBox::question(this, tr("Delete Theme"), tr("Delete theme “%1”?").arg(m_current.name))
            != QMessageBox::Yes)
            return;
        const QString id = m_current.id;
        m_user.removeIf([&id](const Theme &t) { return t.id == id; });
        m_current = Theme::builtIns().first();
        refreshList();
        select(m_current.id);
    });

    refreshList();
    select(currentId);
}

void ThemeEditor::refreshList()
{
    const QSignalBlocker blocker(m_list);
    m_list->clear();
    for (const Theme &t : Theme::compose(m_user)) {
        QString label = t.name;
        if (!Theme::isBuiltIn(t.id))
            label += QStringLiteral("  ✦");        // tema do usuário
        else if (isModified(t))
            label += QStringLiteral("  ✎");        // tema do app personalizado
        auto *item = new QListWidgetItem(swatch(t.surface), label);
        item->setData(Qt::UserRole, t.id);
        item->setSizeHint(QSize(0, 34));
        m_list->addItem(item);
    }
}

bool ThemeEditor::isModified(const Theme &t) const
{
    for (const Theme &u : m_user) {
        if (u.id == t.id)
            return true;
    }
    return false;
}

void ThemeEditor::select(const QString &id)
{
    const QList<Theme> themes = Theme::compose(m_user);
    m_current = themes.first();
    for (const Theme &t : themes) {
        if (t.id == id)
            m_current = t;
    }
    const QSignalBlocker blocker(m_list);
    for (int i = 0; i < m_list->count(); ++i) {
        if (m_list->item(i)->data(Qt::UserRole).toString() == m_current.id)
            m_list->setCurrentRow(i);
    }
    loadForm();
}

void ThemeEditor::loadForm()
{
    m_name->setText(m_current.name);
    const QSignalBlocker blocker(m_dark);
    m_dark->setChecked(m_current.dark);
    for (const auto &[button, member] : std::as_const(m_swatches))
        button->setIcon(swatch(m_current.*member));
    for (int i = 0; i < 16; ++i)
        m_ansiSwatches[i]->setIcon(swatch(m_current.ansi[i]));
    updatePreview();
    updateButtons();
}

/** Grava o tema do formulário na lista do usuário (se diferente do original). */
void ThemeEditor::storeCurrent()
{
    const QString id = m_current.id;
    bool same = false;
    for (const Theme &b : Theme::builtIns()) {
        if (b.id == id)
            same = (b.toJson() == m_current.toJson());
    }
    m_user.removeIf([&id](const Theme &t) { return t.id == id; });
    if (!same)
        m_user.append(m_current);
}

void ThemeEditor::pickColor(QToolButton *button, QColor *target)
{
    const QColor c = QColorDialog::getColor(*target, this, tr("Choose Color"));
    if (!c.isValid())
        return;
    *target = c;
    button->setIcon(swatch(c));
    storeCurrent();
    updatePreview();
    updateButtons();
    // Marca ✎/✦ na lista sem perder a seleção.
    const QString id = m_current.id;
    refreshList();
    const QSignalBlocker blocker(m_list);
    for (int i = 0; i < m_list->count(); ++i) {
        if (m_list->item(i)->data(Qt::UserRole).toString() == id)
            m_list->setCurrentRow(i);
    }
}

void ThemeEditor::updatePreview()
{
    const Theme &t = m_current;
    m_preview->setStyleSheet(QStringLiteral("QPlainTextEdit { background: %1; color: %2; border: 1px solid %3; "
                                            "border-radius: 8px; selection-background-color: %4; }")
                                 .arg(t.surface.name(), t.text.name(), t.divider.name(), t.selection.name()));
    m_highlighter->setTheme(t);
    // Linha atual destacada na 6ª linha, como no editor.
    QTextEdit::ExtraSelection line;
    line.format.setBackground(t.currentLine);
    line.format.setProperty(QTextFormat::FullWidthSelection, true);
    line.cursor = QTextCursor(m_preview->document()->findBlockByNumber(5));
    m_preview->setExtraSelections({line});

    m_terminalPreview->setStyleSheet(QStringLiteral("QPlainTextEdit { background: %1; color: %2; border: 1px solid %3; "
                                                    "border-radius: 8px; }")
                                         .arg(t.terminalBackground.name(), t.text.name(), t.divider.name()));
    QString html = QStringLiteral("<span style='color:%1'>user@host</span>:<span style='color:%2'>~/aula</span>$ "
                                  "gcc media.c &amp;&amp; ./a.out<br>")
                       .arg(t.ansi[2].name(), t.ansi[4].name());
    html += QStringLiteral("7.50<br>");
    for (int i = 0; i < 16; ++i)
        html += QStringLiteral("<span style='background:%1'>&nbsp;&nbsp;&nbsp;</span>").arg(t.ansi[i].name());
    m_terminalPreview->document()->setHtml(QStringLiteral("<div style='color:%1'>%2</div>").arg(t.text.name(), html));
}

void ThemeEditor::updateButtons()
{
    const bool builtIn = Theme::isBuiltIn(m_current.id);
    m_restoreButton->setVisible(builtIn);
    m_restoreButton->setEnabled(builtIn && isModified(m_current));
    m_deleteButton->setVisible(!builtIn);
}
