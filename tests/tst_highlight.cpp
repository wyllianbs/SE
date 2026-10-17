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
