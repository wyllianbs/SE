#include "../src/CodeEditor.h"
#include "../src/Icons.h"
#include "../src/ViewSpace.h"

#include <QApplication>
#include <QLabel>
#include <QSignalSpy>
#include <QTabBar>
#include <QTest>
#include <QTextDocument>

class TestViewSpace : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void doubleClickEmptyStripCreatesTab()
    {
        ViewSpace vs;
        vs.resize(800, 300);
        vs.addTab(new QLabel("a"), "file.c");
        vs.show();
        QVERIFY(QTest::qWaitForWindowExposed(&vs));
        QSignalSpy spy(&vs, &ViewSpace::newTabRequested);

        // Ponto vazio à direita da aba, na altura da barra: clica no widget que está lá.
        const QRect bar = vs.tabBar()->geometry();
        const QPoint empty(bar.left() + vs.tabBar()->tabRect(0).right() + 120, bar.center().y());
        QWidget *target = vs.childAt(empty) ? vs.childAt(empty) : &vs;
        QTest::mouseDClick(target, Qt::LeftButton, {}, target->mapFrom(&vs, empty));
        QCOMPARE(spy.count(), 1);
    }
    void doubleClickOnTabRenames()
    {
        ViewSpace vs;
        vs.resize(800, 300);
        vs.addTab(new QLabel("a"), "file.c");
        vs.show();
        QVERIFY(QTest::qWaitForWindowExposed(&vs));
        QSignalSpy created(&vs, &ViewSpace::newTabRequested);
        QSignalSpy action(&vs, &ViewSpace::tabActionRequested);
        QTest::mouseDClick(vs.tabBar(), Qt::LeftButton, {}, vs.tabBar()->tabRect(0).center());
        QCOMPARE(created.count(), 0);
        QCOMPARE(action.count(), 1);
        QCOMPARE(action.first().at(1).toInt(), 0);
        QCOMPARE(action.first().at(2).toString(), QStringLiteral("rename"));
    }
    void opensBlockPerLanguage()
    {
        // Linhas que ABREM um bloco (Enter deve indentar um nível a mais).
        struct Case { const char *id; const char *line; bool opens; };
        const Case cases[] = {
            {"c", "int main(void) {", true}, {"c", "    return 0;", false},
            {"cpp", "if (x) {", true}, {"rust", "fn main() {", true}, {"go", "func a() {", true},
            {"java", "class A {", true}, {"javascript", "const f = () => {", true},
            {"php", "function f() {", true}, {"r", "f <- function() {", true},
            {"python", "def f():", true}, {"python", "    x = 1", false}, {"python", "y = {}", false},
            {"ruby", "def greet", true}, {"ruby", "3.times do |i|", true}, {"ruby", "if x", true},
            {"ruby", "end", false}, {"ruby", "puts x", false},
            {"lua", "function f()", true}, {"lua", "if x then", true}, {"lua", "for i=1,3 do", true},
            {"lua", "end", false}, {"lua", "print(x)", false},
            {"bash", "if [ -f x ]; then", true}, {"bash", "for i in 1 2; do", true},
            {"bash", "echo hi", false},
            {"pascal", "begin", true}, {"pascal", "if x then", true}, {"pascal", "var", true},
            {"pascal", "end;", false},
            {"fortran", "program hello", true}, {"fortran", "do i = 1, 3", true},
            {"fortran", "if (x) then", true}, {"fortran", "end program", false},
            {"haskell", "main = do", true}, {"haskell", "foo = bar", false}, {"haskell", "foo =", true}, {"haskell", "  where", true},
            {"haskell", "x = 1", false},
        };
        for (const Case &c : cases) {
            const bool got = CodeEditor::opensBlock(QString::fromLatin1(c.id), QString::fromUtf8(c.line));
            QVERIFY2(got == c.opens,
                     qPrintable(QStringLiteral("%1: '%2' expected %3")
                                    .arg(c.id, c.line).arg(c.opens)));
        }
    }
    void editorFontHasNoLigatures()
    {
        const QFont f = Icons::editorFont(QStringLiteral("JetBrainsMono Nerd Font"), 12);
        // calt/liga/clig/dlig desligados: <= -> != aparecem como digitados.
        QCOMPARE(f.featureValue(QFont::Tag("calt")), 0u);
        QCOMPARE(f.featureValue(QFont::Tag("liga")), 0u);
        QCOMPARE(f.featureValue(QFont::Tag("clig")), 0u);
        QCOMPARE(f.featureValue(QFont::Tag("dlig")), 0u);
        QCOMPARE(f.styleHint(), QFont::Monospace);
    }

    void clickGutterTogglesBreakpoint()
    {
        Document doc;
        doc.text()->setPlainText("a\nb\nc\nd\n");
        doc.setLanguage(Language::byId("python")); // só linguagens com depurador têm a coluna
        CodeEditor editor(&doc);
        editor.resize(500, 300);
        editor.show();
        QVERIFY(QTest::qWaitForWindowExposed(&editor));
        // A margem é o primeiro filho do editor, à esquerda do viewport.
        QWidget *gutter = nullptr;
        for (QWidget *w : editor.findChildren<QWidget *>(Qt::FindDirectChildrenOnly))
            if (w->geometry().left() == editor.contentsRect().left() && w->width() == editor.lineNumberAreaWidth())
                gutter = w;
        QVERIFY(gutter);
        const int lineHeight = editor.fontMetrics().height();
        const QPoint third(3, int(lineHeight * 2.5)); // coluna da esquerda, 3ª linha
        QTest::mouseClick(gutter, Qt::LeftButton, {}, third);
        QCOMPARE(doc.breakpoints(), QSet<int>{2});
        QTest::mouseClick(gutter, Qt::LeftButton, {}, third);
        QVERIFY(doc.breakpoints().isEmpty());
        // Sem depurador (HTML): o clique na margem não cria ponto de parada.
        doc.toggleBreakpoint(1);
        doc.setLanguage(Language::byId("html"));
        QVERIFY(doc.breakpoints().isEmpty()); // trocar de linguagem limpa
        QTest::mouseClick(gutter, Qt::LeftButton, {}, third);
        doc.toggleBreakpoint(2);
        QVERIFY(doc.breakpoints().isEmpty());
    }
};

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    TestViewSpace t;
    return QTest::qExec(&t, argc, argv);
}
#include "tst_viewspace.moc"
