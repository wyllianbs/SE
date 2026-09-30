#include "FindBar.h"

#include <QCheckBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTextDocument>
#include <QToolButton>

FindBar::FindBar(QWidget *parent)
    : QWidget(parent)
    , m_find(new QLineEdit(this))
    , m_replace(new QLineEdit(this))
    , m_replaceRow(new QWidget(this))
    , m_caseSensitive(new QCheckBox(tr("Aa"), this))
    , m_wholeWord(new QCheckBox(tr("Word"), this))
    , m_status(new QLabel(this))
{
    setObjectName(QStringLiteral("FindBar"));
    setAttribute(Qt::WA_StyledBackground);
    m_find->setPlaceholderText(tr("Find"));
    m_find->setClearButtonEnabled(true);
    m_replace->setPlaceholderText(tr("Replace with"));
    m_caseSensitive->setToolTip(tr("Match case"));
    m_wholeWord->setToolTip(tr("Whole words only"));
    m_status->setObjectName(QStringLiteral("FindStatus"));

    auto *prev = new QPushButton(QStringLiteral("↑"), this);
    auto *next = new QPushButton(QStringLiteral("↓"), this);
    prev->setToolTip(tr("Previous (Shift+Enter)"));
    next->setToolTip(tr("Next (Enter)"));
    auto *close = new QToolButton(this);
    close->setText(QStringLiteral("✕"));
    close->setAutoRaise(true);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 6, 10, 6);
    layout->setSpacing(6);

    auto *row = new QHBoxLayout;
    row->addWidget(m_find, 1);
    row->addWidget(prev);
    row->addWidget(next);
    row->addWidget(m_caseSensitive);
    row->addWidget(m_wholeWord);
    row->addWidget(m_status);
    row->addWidget(close);
    layout->addLayout(row);

    auto *rrow = new QHBoxLayout(m_replaceRow);
    rrow->setContentsMargins(0, 0, 0, 0);
    auto *replaceBtn = new QPushButton(tr("Replace"), m_replaceRow);
    auto *replaceAllBtn = new QPushButton(tr("Replace All"), m_replaceRow);
    rrow->addWidget(m_replace, 1);
    rrow->addWidget(replaceBtn);
    rrow->addWidget(replaceAllBtn);
    layout->addWidget(m_replaceRow);

    connect(m_find, &QLineEdit::returnPressed, this, &FindBar::findNext);
    connect(m_find, &QLineEdit::textChanged, this, [this]() {
        if (!m_editor)
            return;
        QTextCursor c = m_editor->textCursor();
        c.setPosition(c.selectionStart());
        m_editor->setTextCursor(c);
        find(false);
    });
    connect(next, &QPushButton::clicked, this, &FindBar::findNext);
    connect(prev, &QPushButton::clicked, this, &FindBar::findPrevious);
    connect(replaceBtn, &QPushButton::clicked, this, &FindBar::replaceOne);
    connect(replaceAllBtn, &QPushButton::clicked, this, &FindBar::replaceAll);
    connect(m_replace, &QLineEdit::returnPressed, this, &FindBar::replaceOne);
    connect(close, &QToolButton::clicked, this, &QWidget::hide);
    hide();
}

void FindBar::activate(bool replace)
{
    m_replaceRow->setVisible(replace);
    if (m_editor && m_editor->textCursor().hasSelection()) {
        const QString sel = m_editor->textCursor().selectedText();
        if (!sel.contains(QChar::ParagraphSeparator))
            m_find->setText(sel);
    }
    show();
    m_find->setFocus();
    m_find->selectAll();
}

bool FindBar::find(bool backward)
{
    m_status->clear();
    if (!m_editor || m_find->text().isEmpty())
        return false;
    QTextDocument::FindFlags flags;
    if (backward) flags |= QTextDocument::FindBackward;
    if (m_caseSensitive->isChecked()) flags |= QTextDocument::FindCaseSensitively;
    if (m_wholeWord->isChecked()) flags |= QTextDocument::FindWholeWords;

    if (m_editor->find(m_find->text(), flags))
        return true;
    // Recomeça do outro extremo.
    QTextCursor c = m_editor->textCursor();
    c.movePosition(backward ? QTextCursor::End : QTextCursor::Start);
    m_editor->setTextCursor(c);
    if (m_editor->find(m_find->text(), flags))
        return true;
    m_status->setText(tr("No results"));
    return false;
}

void FindBar::findNext() { find(false); }
void FindBar::findPrevious() { find(true); }

void FindBar::replaceOne()
{
    if (!m_editor)
        return;
    QTextCursor c = m_editor->textCursor();
    const Qt::CaseSensitivity cs = m_caseSensitive->isChecked() ? Qt::CaseSensitive : Qt::CaseInsensitive;
    if (c.hasSelection() && c.selectedText().compare(m_find->text(), cs) == 0)
        c.insertText(m_replace->text());
    find(false);
}

void FindBar::replaceAll()
{
    if (!m_editor || m_find->text().isEmpty())
        return;
    QTextCursor start = m_editor->textCursor();
    start.movePosition(QTextCursor::Start);
    m_editor->setTextCursor(start);
    int count = 0;
    QTextCursor edit = m_editor->textCursor();
    edit.beginEditBlock();
    QTextDocument::FindFlags flags;
    if (m_caseSensitive->isChecked()) flags |= QTextDocument::FindCaseSensitively;
    if (m_wholeWord->isChecked()) flags |= QTextDocument::FindWholeWords;
    while (m_editor->find(m_find->text(), flags)) {
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
