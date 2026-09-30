#pragma once

#include "Theme.h"

#include <QDialog>

class QLabel;
class QLineEdit;
class QListWidget;
class QTextBrowser;

/** Um tópico da ajuda. keywords amplia a busca além do título e do texto. */
struct HelpTopic
{
    QString title;
    QString keywords;
    QString html;
};

/**
 * Ajuda do Slayer Editor com busca por palavras-chave: cada termo digitado
 * precisa aparecer no título, nas palavras-chave ou no texto do tópico
 * (sem diferenciar maiúsculas nem acentos).
 */
class HelpDialog : public QDialog
{
    Q_OBJECT
public:
    explicit HelpDialog(const Theme &theme, QWidget *parent = nullptr);

    static QList<HelpTopic> topics();
    /** Índices dos tópicos que casam com a consulta (exposto para testes). */
    static QList<int> search(const QList<HelpTopic> &topics, const QString &query);

private:
    void filter(const QString &query);
    void showTopic(int topic);

    QList<HelpTopic> m_topics;
    QLineEdit *m_search;
    QListWidget *m_list;
    QTextBrowser *m_view;
    QLabel *m_empty;
    Theme m_theme;
};
