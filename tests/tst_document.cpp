#include "../src/Document.h"

#include <QGuiApplication>
#include <QTemporaryDir>
#include <QSignalSpy>
#include <QTest>
#include <QTextDocument>

class TestDocument : public QObject
{
    Q_OBJECT
    QTemporaryDir m_dir;

    QString write(const QString &name, const QByteArray &data)
    {
        const QString path = m_dir.filePath(name);
        QFile f(path);
        f.open(QIODevice::WriteOnly);
        f.write(data);
        return path;
    }
    static QByteArray read(const QString &path)
    {
        QFile f(path);
        f.open(QIODevice::ReadOnly);
        return f.readAll();
    }

private Q_SLOTS:
    void detectsUtf8()
    {
        Document doc;
        QVERIFY(doc.load(write("a.txt", "ol\xc3\xa1\n")));
        QCOMPARE(doc.encoding(), QStringLiteral("UTF-8"));
        QVERIFY(!doc.hasBom());
        QCOMPARE(doc.text()->toPlainText(), QStringLiteral("olá\n"));
    }
    void fallsBackToWindows1252()
    {
        Document doc;
        QVERIFY(doc.load(write("b.txt", "ol\xe1 \x80\n"))); // á e € em cp1252
        QCOMPARE(doc.encoding(), QStringLiteral("windows-1252"));
        QCOMPARE(doc.text()->toPlainText(), QStringLiteral("olá €\n"));
    }
    void utf8Bom()
    {
        Document doc;
        const QString path = write("c.txt", "\xef\xbb\xbfhi");
        QVERIFY(doc.load(path));
        QVERIFY(doc.hasBom());
        QCOMPARE(doc.text()->toPlainText(), QStringLiteral("hi"));
        QVERIFY(doc.save(path));
        QCOMPARE(read(path), QByteArray("\xef\xbb\xbfhi"));
    }
    void saveAsUtf16AndReload()
    {
        Document doc;
        const QString path = write("d.txt", "ma\xc3\xa7\xc3\xa3");
        QVERIFY(doc.load(path));
        doc.setEncoding(QStringLiteral("UTF-16LE"), true);
        QVERIFY(doc.save(path));
        QCOMPARE(read(path).left(2), QByteArray("\xff\xfe"));
        Document again;
        QVERIFY(again.load(path));
        QCOMPARE(again.text()->toPlainText(), QStringLiteral("maçã"));
        QVERIFY(again.hasBom());
    }
    void reloadWithExplicitEncoding()
    {
        Document doc;
        const QString path = write("e.txt", "ol\xc3\xa1");
        QVERIFY(doc.load(path, nullptr, QStringLiteral("ISO-8859-1")));
        QCOMPARE(doc.text()->toPlainText(), QStringLiteral("olÃ¡"));
    }
    void unrepresentableCharacterFails()
    {
        Document doc;
        doc.text()->setPlainText(QStringLiteral("€ 5"));
        doc.setEncoding(QStringLiteral("ISO-8859-1"), false);
        QString error;
        QVERIFY(!doc.save(m_dir.filePath("f.txt"), &error));
        QVERIFY(!error.isEmpty());
    }
    // ── Alterações feitas por outros programas ──
    static void writeExternally(const QString &path, const QByteArray &data, bool atomic = false)
    {
        QTest::qWait(30); // garante outro mtime
        if (atomic) { // como muitos editores: grava ao lado e renomeia
            QFile tmp(path + ".tmp");
            tmp.open(QIODevice::WriteOnly);
            tmp.write(data);
            tmp.close();
            QFile::remove(path);
            QFile::rename(path + ".tmp", path);
        } else {
            QFile f(path);
            f.open(QIODevice::WriteOnly | QIODevice::Truncate);
            f.write(data);
        }
    }

    void autoReloadUnmodified()
    {
        Document doc;
        const QString path = write("w1.txt", "one\n");
        QVERIFY(doc.load(path));
        doc.setAutoReload(true);
        QSignalSpy asked(&doc, &Document::changedOnDisk);
        writeExternally(path, "two\n");
        QTRY_COMPARE(doc.text()->toPlainText(), QStringLiteral("two\n"));
        QVERIFY(asked.isEmpty());
        QVERIFY(!doc.isModified());
        // Salvar por renomeação: detectado, e a vigilância continua.
        writeExternally(path, "three\n", true);
        QTRY_COMPARE(doc.text()->toPlainText(), QStringLiteral("three\n"));
        writeExternally(path, "four\n");
        QTRY_COMPARE(doc.text()->toPlainText(), QStringLiteral("four\n"));
    }

    void asksWhenAutoReloadOff()
    {
        Document doc;
        const QString path = write("w2.txt", "one\n");
        QVERIFY(doc.load(path));
        QSignalSpy asked(&doc, &Document::changedOnDisk);
        writeExternally(path, "two\n");
        QTRY_COMPARE(asked.count(), 1);
        QCOMPARE(asked.first().at(0).toBool(), false);
        QCOMPARE(doc.text()->toPlainText(), QStringLiteral("one\n")); // nada foi trocado sem pedir
        QVERIFY(doc.reload());
        QCOMPARE(doc.text()->toPlainText(), QStringLiteral("two\n"));
    }

    void neverOverwritesUnsavedChanges()
    {
        Document doc;
        const QString path = write("w3.txt", "one\n");
        QVERIFY(doc.load(path));
        doc.setAutoReload(true);
        doc.text()->setPlainText("mine\n");
        QVERIFY(doc.isModified());
        QSignalSpy asked(&doc, &Document::changedOnDisk);
        writeExternally(path, "theirs\n");
        QTRY_COMPARE(asked.count(), 1);
        QCOMPARE(doc.text()->toPlainText(), QStringLiteral("mine\n"));
    }

    void ownSaveIsNotAChange()
    {
        Document doc;
        const QString path = write("w4.txt", "one\n");
        QVERIFY(doc.load(path));
        QSignalSpy asked(&doc, &Document::changedOnDisk);
        doc.text()->setPlainText("saved by us\n");
        QVERIFY(doc.save(path));
        QTest::qWait(600);
        QVERIFY(asked.isEmpty());
    }

    void deletedFile()
    {
        Document doc;
        const QString path = write("w5.txt", "one\n");
        QVERIFY(doc.load(path));
        doc.setAutoReload(true);
        QSignalSpy asked(&doc, &Document::changedOnDisk);
        QFile::remove(path);
        QTRY_COMPARE(asked.count(), 1);
        QCOMPARE(asked.first().at(0).toBool(), true);
        QCOMPARE(doc.text()->toPlainText(), QStringLiteral("one\n"));
    }
};

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    TestDocument t;
    return QTest::qExec(&t, argc, argv);
}
#include "tst_document.moc"
