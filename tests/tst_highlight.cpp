#include "../src/SyntaxHighlighter.h"

#include <QGuiApplication>
#include <QTest>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextLayout>

/** Verifica se o caractere em (linha, coluna) recebeu a cor de comentário. */
class TestHighlight : public QObject
{
    Q_OBJECT
    QTextDocument m_doc;
    SyntaxHighlighter *m_hl = nullptr;
    Theme m_theme = Theme::all().first();

    void load(const QString &languageId, const QString &text)
    {
        m_doc.setPlainText(text);
        m_hl->setLanguage(Language::byId(languageId));
    }
    bool hasRole(int line, int column, SyntaxHighlighter::Role role)
    {
        const QColor expected = roleColor(m_theme, role);
        const QTextBlock b = m_doc.findBlockByNumber(line);
        for (const QTextLayout::FormatRange &r : b.layout()->formats()) {
            if (column >= r.start && column < r.start + r.length)
                return r.format.foreground().color() == expected;
        }
        // sem formato = cor padrão
        return role == SyntaxHighlighter::Role::None;
    }
    static QColor roleColor(const Theme &t, SyntaxHighlighter::Role role)
    {
        using R = SyntaxHighlighter::Role;
        switch (role) {
        case R::Function:     return t.function;
        case R::Number:       return t.number;
        case R::Type:         return t.type;
        case R::Keyword:      return t.keyword;
        case R::Preprocessor: return t.preprocessor;
        case R::String:       return t.string;
        case R::Comment:      return t.comment;
        case R::None:         break;
        }
        return t.text;
    }
    bool isComment(int line, int column)
    {
        const QTextBlock b = m_doc.findBlockByNumber(line);
        for (const QTextLayout::FormatRange &r : b.layout()->formats()) {
            if (column >= r.start && column < r.start + r.length)
                return r.format.foreground().color() == m_theme.comment;
        }
        return false;
    }

private Q_SLOTS:
    void init()
    {
        delete m_hl;
        m_hl = new SyntaxHighlighter(&m_doc);
        m_hl->setTheme(m_theme);
    }
    void pythonDoubleQuoteDocstring()
    {
        load("python", "\"\"\"one\ntwo 'x'\n\"\"\"\ny = 1\n");
        QVERIFY(isComment(0, 3));
        QVERIFY(isComment(1, 0));
        QVERIFY(isComment(2, 0));
        QVERIFY(!isComment(3, 0));
    }
    void pythonSingleQuoteDocstring()
    {
        load("python", "def f():\n    '''a\n    b'''\n    return 1\n");
        QVERIFY(isComment(1, 4));
        QVERIFY(isComment(2, 4));
        QVERIFY(!isComment(3, 4));
    }
    void pythonTripleQuoteKindsDoNotMix()
    {
        // Aberto com ''' não fecha com """.
        load("python", "'''a \"\"\" b\nstill'''\nz = 2\n");
        QVERIFY(isComment(1, 0));
        QVERIFY(!isComment(2, 0));
    }
    void cBlockComment()
    {
        load("c", "/*\n * x\n */\nint a; /* in */ int b;\n");
        QVERIFY(isComment(1, 3));
        QVERIFY(isComment(3, 8));
        QVERIFY(!isComment(3, 16));
    }
    void lineCommentAfterUrlString()
    {
        load("javascript", "const u = \"http://a/*b*/\"; // c /* d\nlet x = 1;\n");
        QVERIFY(!isComment(0, 12)); // dentro da string
        QVERIFY(isComment(0, 28));  // o // de verdade
        QVERIFY(!isComment(1, 0));  // o /* do comentário não abriu bloco
    }
    void hashInsidePythonString()
    {
        load("python", "s = \"a # b\"  # c\n");
        QVERIFY(!isComment(0, 7));
        QVERIFY(isComment(0, 13));
    }
    // --- HTML ---
    void htmlTagNameHighlighted()
    {
        // "<div>" deve colorir "div" como keyword
        load("html", "<div class=\"c\">texto</div>\n");
        QVERIFY(hasRole(0, 1, SyntaxHighlighter::Role::Keyword)); // d de div
    }
    void htmlArticleNotHighlighted()
    {
        // "a" solto no texto não deve receber cor de tag
        load("html", "<p>Na última declaração, a função retorna int.</p>\n");
        // posição 25 = 'a' (artigo)
        const QString line = QStringLiteral("<p>Na última declaração, a função retorna int.</p>");
        const int pos = line.indexOf(QStringLiteral(", a ")) + 2; // índice do artigo "a"
        QVERIFY(hasRole(0, pos, SyntaxHighlighter::Role::None));
    }
    void htmlAnchorTagHighlighted()
    {
        // "<a href=" — o "a" do tag deve ser keyword
        load("html", "<a href=\"url\">link</a>\n");
        QVERIFY(hasRole(0, 1, SyntaxHighlighter::Role::Keyword)); // o 'a' tag
    }
    void htmlTagNameClosingHighlighted()
    {
        // "</p>" deve colorir "p" como keyword
        load("html", "<p>texto</p>\n");
        const int closeP = QStringLiteral("<p>texto</").length(); // posição do 'p' em </p>
        QVERIFY(hasRole(0, closeP, SyntaxHighlighter::Role::Keyword));
    }
    void htmlAttributeHighlighted()
    {
        // href= deve ser colorido como tipo (atributo)
        load("html", "<a href=\"url\">link</a>\n");
        const int hPos = QStringLiteral("<a ").length(); // h de href
        QVERIFY(hasRole(0, hPos, SyntaxHighlighter::Role::Type));
    }
    void themeSwitchRecolorsWithoutRehighlight()
    {
        // Troca de tema só recolore: o resultado tem de ser idêntico a um
        // realce completo feito já com o novo tema.
        load("python", "def f(x):  # c\n    return \"s\" + 1\n");
        m_theme = Theme::all().last();
        m_hl->setTheme(m_theme);
        QList<QList<QTextLayout::FormatRange>> recolored;
        for (QTextBlock b = m_doc.firstBlock(); b.isValid(); b = b.next())
            recolored << b.layout()->formats();
        m_hl->rehighlight();
        int i = 0;
        for (QTextBlock b = m_doc.firstBlock(); b.isValid(); b = b.next(), ++i)
            QCOMPARE(b.layout()->formats(), recolored.at(i));
        QVERIFY(isComment(0, 12));
        m_theme = Theme::all().first();
    }
};

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    TestHighlight t;
    return QTest::qExec(&t, argc, argv);
}
#include "tst_highlight.moc"
