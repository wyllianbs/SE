#include "Document.h"
#include "SyntaxHighlighter.h"

#include <QFile>
#include <QFileInfo>
#include <QPlainTextDocumentLayout>
#include <QSaveFile>
#include <QTextDocument>
#include <QTextStream>

Document::Document(QObject *parent)
    : QObject(parent)
    , m_text(new QTextDocument(this))
    , m_highlighter(nullptr)
    , m_language(Language::plainText())
{
    // Necessário para o documento ser compartilhado por QPlainTextEdits.
    m_text->setDocumentLayout(new QPlainTextDocumentLayout(m_text));
    m_highlighter = new SyntaxHighlighter(m_text);
    connect(m_text, &QTextDocument::modificationChanged, this, &Document::changed);
}

void Document::setFilePath(const QString &path)
{
    m_filePath = path;
    Q_EMIT changed();
}

QString Document::displayName() const
{
    return m_filePath.isEmpty() ? tr("Untitled") : QFileInfo(m_filePath).fileName();
}

bool Document::isModified() const
{
    return m_text->isModified();
}

bool Document::isUntitledAndEmpty() const
{
    return m_filePath.isEmpty() && !m_text->isModified() && m_text->isEmpty();
}

void Document::setLanguage(const Language &language)
{
    m_language = language;
    m_highlighter->setLanguage(language);
    Q_EMIT changed();
}

void Document::setTheme(const Theme &theme)
{
    m_highlighter->setTheme(theme);
}

bool Document::load(const QString &path, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error)
            *error = file.errorString();
        return false;
    }
    QTextStream in(&file);
    m_text->setPlainText(in.readAll());
    m_text->setModified(false);
    setFilePath(path);
    return true;
}

bool Document::save(const QString &path, QString *error)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error)
            *error = file.errorString();
        return false;
    }
    QTextStream out(&file);
    out << m_text->toPlainText();
    out.flush();
    if (!file.commit()) {
        if (error)
            *error = file.errorString();
        return false;
    }
    m_text->setModified(false);
    setFilePath(path);
    return true;
}
