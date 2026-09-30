#include "Icons.h"
#include "MainWindow.h"
#include "Settings.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QLibraryInfo>
#include <QLocale>
#include <QTranslator>
#include <QStyleFactory>

int main(int argc, char *argv[])
{
    // Escala da interface (Settings → Appearance): precisa ser definida antes do QApplication.
    QCoreApplication::setOrganizationName(QStringLiteral("slayer-editor"));
    QCoreApplication::setApplicationName(QStringLiteral("slayer-editor"));
    const AppSettings settings = AppSettings::load();
    if (qEnvironmentVariableIsEmpty("QT_SCALE_FACTOR")) {
        const int scale = settings.uiScale;
        if (scale != 100)
            qputenv("QT_SCALE_FACTOR", QByteArray::number(scale / 100.0));
    }

    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("slayer-editor"));
    QApplication::setApplicationDisplayName(QStringLiteral("Slayer Editor"));
    QApplication::setOrganizationName(QStringLiteral("slayer-editor"));
    QApplication::setApplicationVersion(QStringLiteral(SLAYER_VERSION));
    QApplication::setDesktopFileName(QStringLiteral("slayer-editor"));
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

    // Idioma: inglês por padrão; pt-BR quando escolhido ou quando o sistema é pt_BR.
    QString language = settings.uiLanguage;
    if (language == QLatin1String("auto"))
        language = QLocale::system().name() == QLatin1String("pt_BR") ? QStringLiteral("pt_BR") : QStringLiteral("en");
    QTranslator appTranslator;
    QTranslator qtTranslator;
    if (language == QLatin1String("pt_BR")) {
        QLocale::setDefault(QLocale(QLocale::Portuguese, QLocale::Brazil));
        if (appTranslator.load(QStringLiteral(":/i18n/slayer-editor_pt_BR.qm")))
            QApplication::installTranslator(&appTranslator);
        // Botões e diálogos padrão do Qt (pacote qt6-translations-l10n).
        if (qtTranslator.load(QStringLiteral("qtbase_pt_BR"), QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
            QApplication::installTranslator(&qtTranslator);
    }

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
