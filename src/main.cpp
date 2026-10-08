#include "Icons.h"
#include "MainWindow.h"
#include "Settings.h"

#include <QApplication>
#include <QIcon>
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
    // app_id = basename do .desktop instalado, para o compositor achar o ícone.
    // No Flatpak é o FLATPAK_ID (= o app-id); fora dele, o mesmo app-id.
    QApplication::setDesktopFileName(qEnvironmentVariable("FLATPAK_ID", QStringLiteral("io.github.wyllianbs.SlayerEditor")));
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

    // Ícone da aplicação, com vários tamanhos para a barra de título ficar
    // nítida. No Wayland, o compositor normalmente pega o ícone do .desktop
    // instalado (pelo app_id); com o protocolo xdg-toplevel-icon (Qt 6.8+ e
    // KWin recentes) este ícone vale mesmo sem instalar o pacote.
    QIcon appIcon;
    for (int size : {16, 22, 24, 32, 48, 64, 128})
        appIcon.addFile(QStringLiteral(":/icons/hicolor/%1.png").arg(size), QSize(size, size));
    appIcon.addFile(QStringLiteral(":/icons/slayer-editor.png"), QSize(256, 256));
    QApplication::setWindowIcon(appIcon);

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
