#include "Icons.h"
#include "MainWindow.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QStyleFactory>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("slayer-editor"));
    QApplication::setApplicationDisplayName(QStringLiteral("Slayer Editor"));
    QApplication::setOrganizationName(QStringLiteral("slayer-editor"));
    QApplication::setApplicationVersion(QStringLiteral(SLAYER_VERSION));
    QApplication::setDesktopFileName(QStringLiteral("slayer-editor"));
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

    Icons::loadFonts();
    QFont uiFont = QApplication::font();
    uiFont.setPointSize(qMax(10, uiFont.pointSize()));
    QApplication::setFont(uiFont);

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Slayer Editor — code editor with an embedded terminal"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("files"), QStringLiteral("Files to open."), QStringLiteral("[files...]"));
    parser.process(app);

    MainWindow window;
    window.openFiles(parser.positionalArguments());
    window.show();
    return app.exec();
}
