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
    // ── Comandos de dobramento do Kate ──
    void kateFoldingCommands()
    {
        Document doc;
        doc.setLanguage(Language::byId("c"));
        doc.text()->setPlainText(
            "int a(void) {\n"          // 0  topo
            "    if (1) {\n"           // 1    contido
            "        x();\n"
            "    }\n"
            "    while (1) {\n"        // 4    contido
            "        y();\n"
            "    }\n"
            "}\n"
            "int b(void) {\n"          // 8  topo
            "    return 0;\n"
            "}\n");
        QTRY_VERIFY(doc.regionAt(0) && doc.regionAt(1) && doc.regionAt(4) && doc.regionAt(8));
        doc.toggleCurrentNode(5);                      // dentro do while: o mais interno
        QVERIFY(doc.isFolded(4) && !doc.isFolded(0));
        doc.toggleCurrentNode(5);
        QVERIFY(!doc.isFolded(4));
        doc.toggleContainedNodes(2);                   // dentro do if: bloco atual é o "if" (sem filhos)
        QVERIFY(!doc.isFolded(1));
        doc.toggleContainedNodes(0);                   // na função: dobra if e while
        QVERIFY(doc.isFolded(1) && doc.isFolded(4) && !doc.isFolded(0));
        doc.toggleContainedNodes(0);                   // de novo: abre os dois
        QVERIFY(!doc.isFolded(1) && !doc.isFolded(4));
        doc.setToplevelFolded(true);
        QVERIFY(doc.isFolded(0) && doc.isFolded(8) && !doc.isFolded(1));
        doc.setToplevelFolded(false);
        QVERIFY(!doc.isFolded(0) && !doc.isFolded(8));
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
