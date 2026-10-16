#include "FindBar.h"

#include "Icons.h"

#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QTextEdit>
#include <QTextDocument>
#include <QToolButton>
#include <QVBoxLayout>

static constexpr int kMaxHistory = 30;

FindBar::FindBar(QWidget *parent)
    : QWidget(parent)
    , m_find(new QComboBox(this))
    , m_replace(new QLineEdit(this))
    , m_replaceRow(new QWidget(this))
    , m_caseSensitive(new QCheckBox(tr("Aa"), this))
    , m_wholeWord(new QCheckBox(tr("Word"), this))
    , m_status(new QLabel(this))
    , m_iconColor(QColor(0x9a, 0x9a, 0x9a))
{
    setObjectName(QStringLiteral("FindBar"));
    setAttribute(Qt::WA_StyledBackground);

    // Combo de busca editável, com histórico (como no Kate).
    m_find->setEditable(true);
    m_find->setInsertPolicy(QComboBox::NoInsert); // o histórico é gerido à mão
    m_find->setMinimumWidth(240);
    m_find->setMaxVisibleItems(15);
    m_find->lineEdit()->setPlaceholderText(tr("Find"));
    m_find->lineEdit()->setClearButtonEnabled(true);
    loadHistory();
    m_find->setCurrentIndex(-1);
    m_find->setCurrentText(QString());

    m_replace->setPlaceholderText(tr("Replace with"));
    m_replace->setClearButtonEnabled(true);
    m_caseSensitive->setToolTip(tr("Match case"));
    m_wholeWord->setToolTip(tr("Whole words only"));
    m_status->setObjectName(QStringLiteral("FindStatus"));

    m_prev = new QPushButton(this);
    m_next = new QPushButton(this);
    m_prev->setToolTip(tr("Previous (Shift+Enter)"));
    m_next->setToolTip(tr("Next (Enter)"));
    m_prev->setAutoDefault(false);
    m_next->setAutoDefault(false);
    setIconColor(m_iconColor);

    auto *close = new QToolButton(this);
    close->setText(QStringLiteral("✕"));
    close->setAutoRaise(true);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 6, 10, 6);
    layout->setSpacing(6);

    auto *row = new QHBoxLayout;
    row->addWidget(m_find, 1);
    row->addWidget(m_prev);
    row->addWidget(m_next);
    row->addWidget(m_caseSensitive);
    row->addWidget(m_wholeWord);
    row->addWidget(m_status);
    row->addWidget(close);
    layout->addLayout(row);

    auto *rrow = new QHBoxLayout(m_replaceRow);
    rrow->setContentsMargins(0, 0, 0, 0);
    auto *replaceBtn = new QPushButton(tr("Replace"), m_replaceRow);
    auto *replaceAllBtn = new QPushButton(tr("Replace All"), m_replaceRow);
    replaceBtn->setAutoDefault(false);
    replaceAllBtn->setAutoDefault(false);
    rrow->addWidget(m_replace, 1);
    rrow->addWidget(replaceBtn);
    rrow->addWidget(replaceAllBtn);
    layout->addWidget(m_replaceRow);

    connect(m_find->lineEdit(), &QLineEdit::returnPressed, this, &FindBar::findNext);
    // Busca incremental conforme digita (ou escolhe do histórico).
    connect(m_find, &QComboBox::editTextChanged, this, [this]() {
        if (!m_editor)
            return;
        QTextCursor c = m_editor->textCursor();
        c.setPosition(c.selectionStart());
        m_editor->setTextCursor(c);
        find(false);
    });
    connect(m_prev, &QPushButton::clicked, this, &FindBar::findPrevious);
    connect(m_next, &QPushButton::clicked, this, &FindBar::findNext);
    connect(replaceBtn, &QPushButton::clicked, this, &FindBar::replaceOne);
    connect(replaceAllBtn, &QPushButton::clicked, this, &FindBar::replaceAll);
    connect(m_replace, &QLineEdit::returnPressed, this, &FindBar::replaceOne);
    connect(close, &QToolButton::clicked, this, &QWidget::hide);
    hide();
}

void FindBar::setIconColor(const QColor &color)
{
    m_iconColor = color;
    if (m_prev)
        m_prev->setIcon(Icons::icon(Icons::ChevronUp, color, 16));
    if (m_next)
        m_next->setIcon(Icons::icon(Icons::ChevronDown, color, 16));
}

QString FindBar::pattern() const
{
    return m_find->currentText();
}

void FindBar::loadHistory()
{
    const QStringList hist = QSettings().value(QStringLiteral("find/history")).toStringList();
    m_find->addItems(hist);
}

void FindBar::saveHistory()
{
    QStringList hist;
    for (int i = 0; i < m_find->count(); ++i)
        hist << m_find->itemText(i);
    QSettings().setValue(QStringLiteral("find/history"), hist);
}

void FindBar::addToHistory(const QString &text)
{
    if (text.isEmpty())
        return;
    QSignalBlocker block(m_find); // não dispara busca ao mexer nos itens
    const int existing = m_find->findText(text);
    if (existing >= 0)
        m_find->removeItem(existing);
    m_find->insertItem(0, text);
    while (m_find->count() > kMaxHistory)
        m_find->removeItem(m_find->count() - 1);
    m_find->setCurrentIndex(0); // mantém o termo buscado no campo
    saveHistory();
}

void FindBar::activate(bool replace)
{
    m_replaceRow->setVisible(replace);
    if (m_editor && m_editor->textCursor().hasSelection()) {
        const QString sel = m_editor->textCursor().selectedText();
        if (!sel.contains(QChar::ParagraphSeparator))
            m_find->setCurrentText(sel);
    }
    show();
    m_find->setFocus();
    m_find->lineEdit()->selectAll();
}

bool FindBar::find(bool backward)
{
    m_status->clear();
    if (!m_editor || pattern().isEmpty())
        return false;
    QTextDocument::FindFlags flags;
    if (backward) flags |= QTextDocument::FindBackward;
    if (m_caseSensitive->isChecked()) flags |= QTextDocument::FindCaseSensitively;
    if (m_wholeWord->isChecked()) flags |= QTextDocument::FindWholeWords;

    if (m_editor->find(pattern(), flags))
        return true;
    // Recomeça do outro extremo.
    QTextCursor c = m_editor->textCursor();
    c.movePosition(backward ? QTextCursor::End : QTextCursor::Start);
    m_editor->setTextCursor(c);
    if (m_editor->find(pattern(), flags))
        return true;
    m_status->setText(tr("No results"));
    return false;
}

void FindBar::findNext()
{
    addToHistory(pattern());
    find(false);
}

void FindBar::findPrevious()
{
    addToHistory(pattern());
    find(true);
}

void FindBar::replaceOne()
{
    if (!m_editor)
        return;
    QTextCursor c = m_editor->textCursor();
    const Qt::CaseSensitivity cs = m_caseSensitive->isChecked() ? Qt::CaseSensitive : Qt::CaseInsensitive;
    if (c.hasSelection() && c.selectedText().compare(pattern(), cs) == 0)
        c.insertText(m_replace->text());
    find(false);
}

void FindBar::replaceAll()
{
    if (!m_editor || pattern().isEmpty())
        return;
    addToHistory(pattern());
    QTextCursor start = m_editor->textCursor();
    start.movePosition(QTextCursor::Start);
    m_editor->setTextCursor(start);
    int count = 0;
    QTextCursor edit = m_editor->textCursor();
    edit.beginEditBlock();
    QTextDocument::FindFlags flags;
    if (m_caseSensitive->isChecked()) flags |= QTextDocument::FindCaseSensitively;
    if (m_wholeWord->isChecked()) flags |= QTextDocument::FindWholeWords;
    while (m_editor->find(pattern(), flags)) {
        m_editor->textCursor().insertText(m_replace->text());
        ++count;
    }
    edit.endEditBlock();
    m_status->setText(tr("%n replaced", nullptr, count));
}

void FindBar::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        hide();
        if (m_editor)
            m_editor->setFocus();
        return;
    }
    if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)
        && (event->modifiers() & Qt::ShiftModifier)) {
        findPrevious();
        return;
    }
    QWidget::keyPressEvent(event);
}
