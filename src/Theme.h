#pragma once

#include <QColor>
#include <QList>
#include <QJsonObject>
#include <QString>

/** Paleta de cores do editor, da interface e do terminal. */
struct Theme
{
    QString id;
    QString name;
    bool dark = true;
    /** Variante oposta (clara ↔ escura) do mesmo tema; vazio se não houver. */
    QString counterpart;

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

    /** Temas do app, sem personalizações. */
    static const QList<Theme> &builtIns();
    static bool isBuiltIn(const QString &id);
    /** Temas em uso: os do app (com personalizações) + os criados pelo usuário. */
    static const QList<Theme> &all();
    static const Theme &byId(const QString &id);
    /** O tema na variante pedida (ele mesmo, ou a variante oposta dele). */
    static const Theme &variant(const QString &id, bool dark);
    /** Aplica as personalizações (mesmo id de um tema do app) e os temas novos. */
    static void setUserThemes(const QList<Theme> &themes);
    static QList<Theme> compose(const QList<Theme> &userThemes);

    QJsonObject toJson() const;
    static Theme fromJson(const QJsonObject &json);

    /** Cores editáveis: chave, rótulo (tr no chamador) e ponteiro para o membro. */
    struct Field { const char *key; const char *label; const char *group; QColor Theme::*member; };
    static const QList<Field> &fields();

    /** Grava o esquema de cores do QTermWidget e devolve o nome dele. */
    QString writeTerminalScheme(const QString &dir, const QString &name = QString()) const;
};
