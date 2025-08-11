#include "mainwindow.h"
#include <QApplication>
#include <QTranslator>
#include <QLocale>
#include <QLibraryInfo>

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    app.setWindowIcon(QIcon(":/icons/favicon.svg"));

    // Détection de la langue du système
    QString locale = QLocale::system().name(); // ex: "fr_FR", "en_US", etc.

    QTranslator translator;

    // Essayer de charger la traduction pour la langue du système
    if (locale.startsWith("fr")) {
        // Français détecté, garder les textes français par défaut
        // Pas besoin de charger une traduction car le code est déjà en français
    } else {
        // Autre langue, charger la traduction anglaise
        if (translator.load(":/translations/appmanager_en.qm")) {
            app.installTranslator(&translator);
        }
    }

    MainWindow w;
    w.show();

    return app.exec();
}
