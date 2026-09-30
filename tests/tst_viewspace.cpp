#include "../src/ViewSpace.h"

#include <QApplication>
#include <QLabel>
#include <QSignalSpy>
#include <QTabBar>
#include <QTest>

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
    void doubleClickOnTabDoesNot()
    {
        ViewSpace vs;
        vs.resize(800, 300);
        vs.addTab(new QLabel("a"), "file.c");
        vs.show();
        QVERIFY(QTest::qWaitForWindowExposed(&vs));
        QSignalSpy spy(&vs, &ViewSpace::newTabRequested);
        QTest::mouseDClick(vs.tabBar(), Qt::LeftButton, {}, vs.tabBar()->tabRect(0).center());
        QCOMPARE(spy.count(), 0);
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
