#include "mainwindow.h"
#include <QApplication>
#include <QTranslator>
#include <QLocale>
#include <QLibraryInfo>

int main(int argc, char *argv[]) {
    // Handle Squirrel/Velopack lifecycle hooks
    for (int i = 1; i < argc; ++i) {
        QString arg = QString::fromLocal8Bit(argv[i]);
        if (arg.startsWith("--squirrel-") || arg.startsWith("--velopack-") || arg.startsWith("--veloapp-")) {
            return 0; // Exit immediately to let the installer finish
        }
    }

    QApplication app(argc, argv);

    app.setApplicationName("OnBoarder");
    app.setOrganizationName("Sacdeneu");
#if __has_include("version.h")
#include "version.h"
#endif
#ifndef APP_VERSION
#define APP_VERSION "1.0.0"
#endif
    app.setApplicationVersion(APP_VERSION);
    app.setWindowIcon(QIcon(":/icons/favicon.svg"));

    QString locale = QLocale::system().name();

    QTranslator translator;
    if (!locale.startsWith("fr")) {
        if (translator.load(":/translations/appmanager_en.qm")) {
            app.installTranslator(&translator);
        }
    }

    MainWindow w;
    w.show();

    return app.exec();
}
