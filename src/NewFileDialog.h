#pragma once

#include "Settings.h"
#include "Theme.h"

#include <QDialog>

class QLineEdit;
class QListWidget;
class QPlainTextEdit;

/**
 * Arquivo novo: escolhe a linguagem (só as instaladas, mais as personalizadas
 * e "arquivo vazio") e o nome sugerido; o editor já abre com o Hello, World!.
 */
class NewFileDialog : public QDialog
{
    Q_OBJECT
public:
    NewFileDialog(const AppSettings &settings, const QString &lastLanguage, const Theme &theme,
                  QWidget *parent = nullptr);

    /** Id da linguagem ("c", "custom:Nome"…) ou vazio para arquivo vazio. */
    QString languageId() const;
    QString fileName() const;
    QString content() const;

private:
    void onSelectionChanged();

    AppSettings m_settings;
    QListWidget *m_list;
    QLineEdit *m_name;
    QPlainTextEdit *m_preview;
    class SyntaxHighlighter *m_highlighter;
    bool m_nameEdited = false;
};
