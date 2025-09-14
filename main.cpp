#include "mainwindow.h"
#include <QApplication>
#include <QTranslator>
#include <QLocale>
#include <QLibraryInfo>

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    app.setApplicationName("OnBoarder");
    app.setOrganizationName("Sacdeneu");
    app.setApplicationVersion("1.0.0");
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
