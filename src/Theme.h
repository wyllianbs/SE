#pragma once

#include <QColor>
#include <QList>
#include <QString>

/** Paleta de cores do editor, da interface e do terminal. */
struct Theme
{
    QString id;
    QString name;
    bool dark = true;

    QColor primary;
    QColor window;        //< fundo da janela / painéis
    QColor surface;       //< fundo do editor
    QColor text;
    QColor textSecondary;
    QColor divider;
    QColor currentLine;
    QColor selection;

    QColor keyword;
    QColor type;
    QColor comment;
    QColor string;
    QColor number;
    QColor function;
    QColor preprocessor;

    QColor terminalBackground;
    QColor ansi[16];

    static const QList<Theme> &all();
    static const Theme &byId(const QString &id);

    /** Grava o esquema de cores do QTermWidget e devolve o nome dele. */
    QString writeTerminalScheme(const QString &dir) const;
};
