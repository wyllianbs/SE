#include "NewFileDialog.h"
#include "Icons.h"
#include "Language.h"
#include "SyntaxHighlighter.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace {
constexpr int kIdRole = Qt::UserRole;
constexpr int kNameRole = Qt::UserRole + 1;
constexpr int kTextRole = Qt::UserRole + 2;
}

NewFileDialog::NewFileDialog(const AppSettings &settings, const QString &lastLanguage, const Theme &theme,
                             QWidget *parent)
    : QDialog(parent)
    , m_settings(settings)
    , m_list(new QListWidget(this))
    , m_name(new QLineEdit(this))
    , m_preview(new QPlainTextEdit(this))
{
    setWindowTitle(tr("New File"));
    setObjectName(QStringLiteral("SettingsDialog"));
    resize(760, 460);

    m_list->setObjectName(QStringLiteral("SettingsNav"));
    m_list->setFixedWidth(230);
    m_list->setIconSize(QSize(18, 18));

    auto add = [this](const QIcon &icon, const QString &label, const QString &id, const QString &name,
                      const QString &text) {
        auto *item = new QListWidgetItem(icon, label);
        item->setData(kIdRole, id);
        item->setData(kNameRole, name);
        item->setData(kTextRole, text);
        item->setSizeHint(QSize(0, 34));
        m_list->addItem(item);
        return item;
    };
    // Linguagens instaladas no sistema, com o Hello, World! de cada uma.
    for (const QString &id : settings.installedLanguages()) {
        const Language &lang = Language::byId(id);
        if (lang.helloWorld.isEmpty())
            continue;
        add(Icons::icon(Icons::forLanguage(id), Icons::colorForLanguage(id), 18), lang.displayName, id,
            lang.newFileName, lang.helloWorld);
    }
    for (const CustomLanguage &c : settings.customLanguages) {
        const QString ext = Language::splitExtensions(c.extensions).value(0, QStringLiteral("txt"));
        add(Icons::icon(Icons::File, Icons::colorForLanguage(QString()), 18), c.name, QStringLiteral("custom:") + c.name,
            QStringLiteral("untitled.") + ext, Language::helloWorldForTool(c.tool));
    }
    add(Icons::icon(Icons::File, Icons::colorForLanguage(QString()), 18), tr("Empty file"), QString(),
        QString(), QString());

    m_preview->setReadOnly(true);
    m_preview->setObjectName(QStringLiteral("NewFilePreview"));
    m_preview->setFont(QFont(Icons::monoFamily(), 10));
    m_preview->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_highlighter = new SyntaxHighlighter(m_preview->document());
    m_highlighter->setTheme(theme);
    m_name->setPlaceholderText(tr("Untitled"));
    connect(m_name, &QLineEdit::textEdited, this, [this]() { m_nameEdited = true; });

    auto *form = new QFormLayout;
    form->addRow(tr("File name"), m_name);
    auto *hint = new QLabel(tr("The file is created in the editor; Save chooses the folder."));
    hint->setObjectName(QStringLiteral("Hint"));

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Create"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *right = new QVBoxLayout;
    right->setContentsMargins(16, 16, 16, 12);
    right->addLayout(form);
    right->addWidget(m_preview, 1);
    right->addWidget(hint);
    right->addWidget(buttons);
    auto *root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->addWidget(m_list);
    root->addLayout(right, 1);

    connect(m_list, &QListWidget::currentRowChanged, this, &NewFileDialog::onSelectionChanged);
    connect(m_list, &QListWidget::itemDoubleClicked, this, &QDialog::accept);

    int row = 0;
    for (int i = 0; i < m_list->count(); ++i) {
        if (m_list->item(i)->data(kIdRole).toString() == lastLanguage)
            row = i;
    }
    m_list->setCurrentRow(row);
    m_list->setFocus();
}

void NewFileDialog::onSelectionChanged()
{
    QListWidgetItem *item = m_list->currentItem();
    if (!item)
        return;
    // O nome acompanha a linguagem até o usuário digitar outro.
    if (!m_nameEdited)
        m_name->setText(item->data(kNameRole).toString());
    const QString id = item->data(kIdRole).toString();
    m_highlighter->setLanguage(id.startsWith(QLatin1String("custom:")) || id.isEmpty() ? Language::plainText()
                                                                                         : Language::byId(id));
    m_preview->setPlainText(item->data(kTextRole).toString());
}

QString NewFileDialog::languageId() const
{
    return m_list->currentItem() ? m_list->currentItem()->data(kIdRole).toString() : QString();
}

QString NewFileDialog::fileName() const
{
    return m_name->text().trimmed();
}

QString NewFileDialog::content() const
{
    return m_list->currentItem() ? m_list->currentItem()->data(kTextRole).toString() : QString();
}
