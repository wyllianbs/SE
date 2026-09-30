#include "../src/CodeEditor.h"
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
    void clickGutterTogglesBreakpoint()
    {
        Document doc;
        doc.text()->setPlainText("a\nb\nc\nd\n");
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
