#include "../src/PreviewServer.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>

class TestPreview : public QObject
{
    Q_OBJECT

    static QByteArray get(quint16 port, const QByteArray &path)
    {
        QTcpSocket c;
        c.connectToHost(QStringLiteral("127.0.0.1"), port);
        if (!c.waitForConnected(2000))
            return {};
        c.write("GET " + path + " HTTP/1.1\r\nHost: x\r\n\r\n");
        QByteArray resp;
        QElapsedTimer t;
        t.start();
        while (c.state() == QAbstractSocket::ConnectedState && t.elapsed() < 3000) {
            QCoreApplication::processEvents();
            c.waitForReadyRead(20);
            resp += c.readAll();
        }
        return resp + c.readAll();
    }

private Q_SLOTS:
    void servesOnlyInsideRoot()
    {
        QTemporaryDir tmp;
        QDir(tmp.path()).mkpath("site");
        QDir(tmp.path()).mkpath("site-secret"); // vizinha com o mesmo prefixo
        auto write = [](const QString &path, const QByteArray &data) {
            QFile f(path);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(data);
        };
        write(tmp.filePath("site/index.html"), "<p>ok</p>");
        write(tmp.filePath("site-secret/password.txt"), "TOP SECRET");

        PreviewServer server;
        QVERIFY(server.start(tmp.filePath("site"), QStringLiteral("127.0.0.1"), 0));
        QVERIFY(get(server.port(), "/").startsWith("HTTP/1.1 200"));
        for (const QByteArray &bad : {QByteArray("/../site-secret/password.txt"),
                                      QByteArray("/%2e%2e/site-secret/password.txt"),
                                      QByteArray("../site-secret/password.txt"), QByteArray("/../../etc/hostname")}) {
            const QByteArray resp = get(server.port(), bad);
            QVERIFY2(resp.startsWith("HTTP/1.1 403"), bad.constData());
            QVERIFY(!resp.contains("TOP SECRET"));
        }
    }
};

QTEST_GUILESS_MAIN(TestPreview)
#include "tst_preview.moc"
