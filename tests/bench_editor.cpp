// Benchmark do editor com arquivos grandes (calibra o modo desempenho).
// Uso: bench_editor <arquivo>... — imprime os tempos (ms) de cada operação,
// com e sem quebra de linha, e com o modo desempenho desligado e ligado.
#include "../src/CodeEditor.h"
#include "../src/Document.h"
#include "../src/Language.h"
#include "../src/Theme.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QScrollBar>
#include <QTest>
#include <QTextBlock>

#include <cstdio>

static qint64 timed(const std::function<void()> &f)
{
    QElapsedTimer t;
    t.start();
    f();
    QApplication::processEvents();
    return t.elapsed();
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    std::printf("%-22s %-5s %-4s %8s %8s %8s %8s\n", "arquivo", "wrap", "perf", "abrir", "rolar", "digitar",
                "tema");
    const QList<Theme> themes = Theme::all();
    for (int i = 1; i < argc; ++i) {
        for (bool wrap : {false, true}) {
            for (int perf : {0, 2}) {
                Document doc;
                CodeEditor editor(&doc);
                editor.resize(1200, 800);
                editor.setTheme(themes.first());
                editor.setMinimapVisible(true);
                editor.setWordWrap(wrap);
                doc.setLargeFileLimits(perf, 50000, 5 * 1024 * 1024);
                editor.show();
                if (!QTest::qWaitForWindowExposed(&editor))
                    return 1;
                const qint64 open = timed([&]() {
                    doc.setLanguage(qEnvironmentVariableIsSet("BENCH_PLAIN") ? Language::plainText()
                                                                             : Language::forFileName(QString::fromLocal8Bit(argv[i])));
                    doc.load(QString::fromLocal8Bit(argv[i]));
                    editor.repaint();
                });
                const qint64 scroll = timed([&]() {
                    QScrollBar *bar = editor.verticalScrollBar();
                    for (int k = 1; k <= 10; ++k) {
                        bar->setValue(bar->maximum() * k / 10);
                        editor.repaint();
                    }
                });
                const qint64 type = timed([&]() {
                    QTextCursor c(editor.document()->findBlockByNumber(editor.document()->blockCount() / 2));
                    editor.setTextCursor(c);
                    for (int k = 0; k < 20; ++k) {
                        editor.textCursor().insertText(QStringLiteral("x"));
                        editor.repaint();
                    }
                });
                const qint64 theme = timed([&]() {
                    editor.setTheme(themes.value(1, themes.first()));
                    doc.setTheme(themes.value(1, themes.first()));
                    editor.repaint();
                });
                std::printf("%-22s %-5s %-4s %8lld %8lld %8lld %8lld\n",
                            qPrintable(QFileInfo(QString::fromLocal8Bit(argv[i])).fileName()), wrap ? "sim" : "não",
                            perf ? "on" : "off", open, scroll, type, theme);
                std::fflush(stdout);
            }
        }
    }
    return 0;
}
