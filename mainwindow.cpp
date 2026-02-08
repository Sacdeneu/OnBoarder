#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QProgressDialog>
#include <QFile>
#include <QHBoxLayout>
#include <QTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QLabel>
#include <QDebug>
#include <QIcon>
#include <QBrush>
#include <QLocale>
#include <QTranslator>
#include <QApplication>
#include <QMessageBox>
#include <QDir>
#include <QFile>
#include <QDesktopServices>
#include <QUrl>

#ifdef Q_OS_WIN
#define OS_KEY "windows"
#elif defined(Q_OS_MAC)
#define OS_KEY "macos"
#else
#define OS_KEY "linux"
#endif

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent),
    updateDownloadReply(nullptr),
    settings("Sacdeneu", "OnBoarder"),
    networkManager(new QNetworkAccessManager(this)),
    autoUpdateEnabled(false),
    ui(new Ui::MainWindow),
    process(nullptr),
    currentAppIndex(0),
    uninstalling(false)
{
    ui->setupUi(this);
    QFile testFile(":/data/apps.json");
    if (testFile.open(QIODevice::ReadOnly)) {
        ui->logTextEdit->append("✅ Fichier JSON accessible");
        ui->logTextEdit->append("Contenu (100 premiers caractères): " + QString(testFile.read(100)));
        testFile.close();
    } else {
        ui->logTextEdit->append("❌ Fichier JSON INACCESSIBLE");
    }
    ui->logTextEdit->setVisible(true);
    ui->showLogsCheckBox->setChecked(true);
    loadSettings();
    // Connect buttons
    connect(ui->installButton, &QPushButton::clicked, this, &MainWindow::onInstallClicked);
    connect(ui->uninstallButton, &QPushButton::clicked, this, &MainWindow::onUninstallClicked);
    connect(ui->showLogsCheckBox, &QCheckBox::toggled, this, &MainWindow::onShowLogsToggled);
    connect(ui->restartButton, &QPushButton::clicked, this, &MainWindow::onRestartClicked);
    connect(ui->quitButton, &QPushButton::clicked, this, &MainWindow::onQuitClicked);
    connect(ui->updateButton, &QPushButton::clicked, this, &MainWindow::onUpdateButtonClicked);

    // Connect list widget selection changes
    connect(ui->listWidget, &QListWidget::itemChanged, this, &MainWindow::onItemChanged);

    connect(ui->settingsButton, &QPushButton::clicked, this, &MainWindow::onSettingsClicked);
    connect(ui->darkThemeCheckBox, &QCheckBox::toggled, this, &MainWindow::onDarkThemeToggled);
    connect(ui->autoUpdateCheckBox, &QCheckBox::toggled, this, &MainWindow::onAutoUpdateToggled);
    connect(ui->checkUpdateButton, &QPushButton::clicked, this, &MainWindow::onCheckUpdateClicked);
    connect(ui->backToMainButton, &QPushButton::clicked, this, &MainWindow::onBackToMainClicked);
    connect(ui->vsConfigOkButton, &QPushButton::clicked, this, &MainWindow::onVSConfigOkClicked);
    connect(ui->vsConfigCancelButton, &QPushButton::clicked, this, &MainWindow::onVSConfigCancelClicked);

    if (autoUpdateEnabled) {
        QTimer::singleShot(0, this, [this]() { checkForUpdates(false); });
    }

    ui->progressBar->setValue(0);
    ui->logTextEdit->setVisible(false);
    ui->stackedWidget->setCurrentIndex(0); // Start at pageSelect

    // Initially disable buttons
    ui->installButton->setEnabled(false);
    ui->uninstallButton->setEnabled(false);

    // Set window title and icon
    setWindowTitle(tr("OnBoarder"));
    setWindowIcon(QIcon(":/icons/favicon.svg"));

    loadApps();

    updateStepIndicator(1);
}

void MainWindow::onUpdateButtonClicked() {
    ui->stackedWidget->setCurrentIndex(3);
    hideStepIndicator(true);
    onCheckUpdateClicked();
}

MainWindow::~MainWindow() {
    if (process) {
        process->kill();
        process->deleteLater();
    }
    delete ui;
}bool MainWindow::isCustomAppInstalled(const QString &appName, const QString &executablePath) {
#ifdef Q_OS_WIN
    if (appName == "Wrike") {
        QString expandedPath = executablePath;
        expandedPath.replace("%USERNAME%", qgetenv("USERNAME"));

        QFile file(expandedPath);
        if (file.exists()) {
            appendLog(tr("🔍 %1 trouvé via le chemin de l'exécutable").arg(appName));
            return true;
        }

        // Vérification via WMI avec PowerShell
        QProcess proc;
        QString program = "powershell.exe";
        QStringList arguments = {
            "-NoProfile",
            "-ExecutionPolicy", "Bypass",
            "-Command",
            "Get-WmiObject -Class Win32_Product | Where-Object { $_.Name -like '*Wrike*' } | Select-Object -ExpandProperty Name"
        };

        proc.start(program, arguments);
        if (!proc.waitForStarted(3000)) {
            appendLog(tr("⚠️ Impossible de démarrer PowerShell pour la vérification"));
            return false;
        }

        if (!proc.waitForFinished(5000)) {
            appendLog(tr("⚠️ Timeout lors de la vérification PowerShell"));
            return false;
        }

        QString output = proc.readAllStandardOutput();
        QString error = proc.readAllStandardError();

        if (!error.isEmpty()) {
            appendLog(tr("⚠️ Erreur PowerShell : %1").arg(error.trimmed()));
        }

        bool isInstalled = output.contains("Wrike", Qt::CaseInsensitive);
        appendLog(tr("🔍 %1 %2 via WMI").arg(appName, isInstalled ? tr("TROUVÉ") : tr("NON TROUVÉ")));

        return isInstalled;
    }
#else
    Q_UNUSED(appName);
    Q_UNUSED(executablePath);
#endif
    return false;
}

void MainWindow::applyDarkTheme(bool enabled) {
    if (enabled) {
        // True Dark Premium Theme (#050505 background)
        qApp->setStyleSheet(
            "QMainWindow { background-color: #050505; }"
            "QWidget { background-color: #050505; color: #e0e0e0; font-family: 'Segoe UI', sans-serif; }"
            "QWidget#sidebarWidget { background-color: #1e1e1e; }" /* Sidebar darker but distinct */
            "QWidget#step1Widget, QWidget#step2Widget, QWidget#step3Widget { background-color: transparent; }"
            "QPushButton { "
            "  background-color: #121214; border: 1px solid #1c1c1f; border-radius: 6px; "
            "  padding: 8px 16px; color: #ffffff; font-weight: 500; "
            "}"
            "QPushButton:hover { background-color: #1c1c1f; border-color: #2d2d30; }"
            "QPushButton:pressed { background-color: #050505; }"
            "QPushButton:disabled { background-color: #0a0a0c; color: #3f3f46; border-color: #141416; }"
            "QListWidget { "
            "  background-color: #08080a; border: 1px solid #141416; border-radius: 8px; "
            "  padding: 5px; outline: none; "
            "}"
            "QListWidget::item { "
            "  padding: 10px; border-bottom: 1px solid #0d0d0f; border-radius: 4px; "
            "}"
            "QListWidget::item:selected { background-color: #121214; color: #3b82f6; }"
            "QListWidget::item:hover { background-color: #0d0d0f; }"
            "QScrollBar:vertical { "
            "  border: none; background: #050505; width: 10px; margin: 0px; "
            "}"
            "QScrollBar::handle:vertical { "
            "  background: #27272a; min-height: 20px; border-radius: 5px; margin: 2px; "
            "}"
            "QScrollBar::handle:vertical:hover { background: #3f3f46; }"
            "QScrollBar::add-line:vertical { height: 0px; subcontrol-position: bottom; subcontrol-origin: margin; }"
            "QScrollBar::sub-line:vertical { height: 0px; subcontrol-position: top; subcontrol-origin: margin; }"
            "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: none; }"
            "QScrollBar:horizontal { "
            "  border: none; background: #050505; height: 10px; margin: 0px; "
            "}"
            "QScrollBar::handle:horizontal { "
            "  background: #27272a; min-width: 20px; border-radius: 5px; margin: 2px; "
            "}"
            "QScrollBar::handle:horizontal:hover { background: #3f3f46; }"
            "QScrollBar::add-line:horizontal { width: 0px; subcontrol-position: right; subcontrol-origin: margin; }"
            "QScrollBar::sub-line:horizontal { width: 0px; subcontrol-position: left; subcontrol-origin: margin; }"
            "QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal { background: none; }"
            "QProgressBar { "
            "  border: 1px solid #1c1c1f; border-radius: 6px; background-color: #08080a; "
            "  text-align: center; color: transparent; height: 6px; "
            "}"
            "QProgressBar::chunk { background-color: #3b82f6; border-radius: 5px; }"
            "QTextEdit { background-color: #08080a; border: 1px solid #1c1c1f; border-radius: 8px; color: #e4e4e7; }"
            "QCheckBox { spacing: 8px; }"
            "QScrollArea { border: none; background-color: transparent; }"
            "QLabel#vsConfigTitle { color: #ffffff; font-size: 18px; font-weight: bold; }"
            "QLabel#workloadsLabel { color: #a1a1aa; }"
        );
    } else {
        // Light Theme polished
        qApp->setStyleSheet(
            "QMainWindow { background-color: #ffffff; }"
            "QWidget { background-color: #ffffff; color: #18181b; font-family: 'Segoe UI', sans-serif; }"
            "QWidget#sidebarWidget { background-color: #f3f3f3; }" /* Sidebar light grey */
            "QWidget#step1Widget, QWidget#step2Widget, QWidget#step3Widget { background-color: transparent; }"
            "QPushButton { "
            "  background-color: #f4f4f5; border: 1px solid #e4e4e7; border-radius: 6px; "
            "  padding: 8px 16px; color: #18181b; font-weight: 500; "
            "}"
            "QPushButton:hover { background-color: #e4e4e7; border-color: #d4d4d8; }"
            "QListWidget { background-color: #ffffff; border: 1px solid #e4e4e7; border-radius: 8px; }"
            "QScrollBar:vertical { "
            "  border: none; background: #f4f4f5; width: 10px; margin: 0px; "
            "}"
            "QScrollBar::handle:vertical { "
            "  background: #d4d4d8; min-height: 20px; border-radius: 5px; margin: 2px; "
            "}"
            "QScrollBar::handle:vertical:hover { background: #a1a1aa; }"
            "QScrollBar::add-line:vertical { height: 0px; subcontrol-position: bottom; subcontrol-origin: margin; }"
            "QScrollBar::sub-line:vertical { height: 0px; subcontrol-position: top; subcontrol-origin: margin; }"
            "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: none; }"
            "QScrollBar:horizontal { "
            "  border: none; background: #f4f4f5; height: 10px; margin: 0px; "
            "}"
            "QScrollBar::handle:horizontal { "
            "  background: #d4d4d8; min-width: 20px; border-radius: 5px; margin: 2px; "
            "}"
            "QScrollBar::handle:horizontal:hover { background: #a1a1aa; }"
            "QScrollBar::add-line:horizontal { width: 0px; subcontrol-position: right; subcontrol-origin: margin; }"
            "QScrollBar::sub-line:horizontal { width: 0px; subcontrol-position: left; subcontrol-origin: margin; }"
            "QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal { background: none; }"
            "QProgressBar { "
            "  border: 1px solid #e4e4e7; border-radius: 6px; background-color: #f4f4f5; "
            "  text-align: center; color: transparent; height: 6px; "
            "}"
            "QProgressBar::chunk { background-color: #2563eb; border-radius: 5px; }"
        );
    }
}

void MainWindow::loadSettings() {
    bool darkTheme = settings.value("darkTheme", false).toBool();
    autoUpdateEnabled = settings.value("autoUpdate", false).toBool();

    ui->darkThemeCheckBox->setChecked(darkTheme);
    ui->autoUpdateCheckBox->setChecked(autoUpdateEnabled);

    applyDarkTheme(darkTheme);
}

void MainWindow::saveSettings() {
    settings.setValue("darkTheme", ui->darkThemeCheckBox->isChecked());
    settings.setValue("autoUpdate", ui->autoUpdateCheckBox->isChecked());
}
void MainWindow::onSettingsClicked() {
    ui->stackedWidget->setCurrentIndex(3);
    hideStepIndicator(true);
}

void MainWindow::onDarkThemeToggled(bool checked) {
    applyDarkTheme(checked);
    saveSettings();
    
    // Rafraîchir les couleurs de tous les items
    for (AppStatus &app : apps) {
        updateItemText(app);
    }
}

void MainWindow::onAutoUpdateToggled(bool checked) {
    autoUpdateEnabled = checked;
    saveSettings();
}

void MainWindow::onCheckUpdateClicked() {
    checkForUpdates(true);
}

void MainWindow::checkForUpdates(bool manual) {
    QUrl url("https://api.github.com/repos/Sacdeneu/OnBoarder/releases/latest");
    QNetworkReply *reply = networkManager->get(QNetworkRequest(url));
    connect(reply, &QNetworkReply::finished, this, [this, reply, manual]() {
        handleUpdateReply(reply);
        reply->deleteLater();
    });
}

void MainWindow::handleUpdateReply(QNetworkReply *reply) {
    if (reply->error() != QNetworkReply::NoError) {
        if (reply->error() == QNetworkReply::ContentNotFoundError)
            appendLog("⚠️ Pas de release trouvée.");
        else
            appendLog("⚠️ Erreur réseau : " + reply->errorString());
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
    if (!doc.isObject()) {
        appendLog("⚠️ Réponse GitHub invalide.");
        return;
    }

    QJsonObject obj = doc.object();
    QString latestVersion = obj.value("tag_name").toString(); // ex: "v1.2.0"
    QString currentVersion = "v" + QCoreApplication::applicationVersion(); // Ajouter le 'v'

    if (latestVersion != currentVersion) {
        // Chercher le fichier Setup.exe dans les assets
        QString setupDownloadUrl;
        QJsonArray assets = obj.value("assets").toArray();
        ui->updateButton->setVisible(true);
        ui->updateButton->setText(QString("Mise à jour %1 disponible").arg(latestVersion));

        for (const QJsonValue &asset : assets) {
            QJsonObject assetObj = asset.toObject();
            QString name = assetObj.value("name").toString();

            // Chercher le fichier Setup.exe
            if (name.contains("Setup.exe", Qt::CaseInsensitive) ||
                name.contains("-win-Setup.exe", Qt::CaseInsensitive)) {
                setupDownloadUrl = assetObj.value("browser_download_url").toString();
                break;
            }
        }

        if (setupDownloadUrl.isEmpty()) {
            QMessageBox::warning(this, tr("Erreur"),
                                 tr("Aucun installateur trouvé dans la release %1.").arg(latestVersion));
            return;
        }

        pendingUpdateVersion = latestVersion;

        int result = QMessageBox::question(this, tr("Mise à jour disponible"),
                                           tr("Nouvelle version %1 disponible (actuelle %2).\n\n"
                                              "Voulez-vous télécharger et installer la mise à jour maintenant ?\n\n"
                                              "L'application se fermera automatiquement après l'installation.")
                                               .arg(latestVersion, currentVersion),
                                           QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);

        if (result == QMessageBox::Yes) {
            downloadAndInstallUpdate(setupDownloadUrl);
        }
    } else {
        ui->updateButton->setVisible(false);
        QMessageBox::information(this, tr("Mises à jour"),
                                 tr("Votre application est déjà à jour (%1).").arg(currentVersion));
    }
}

void MainWindow::downloadAndInstallUpdate(const QString &url) {
    // Créer une barre de progression pour le téléchargement
    QProgressDialog *progressDialog = new QProgressDialog(
        tr("Téléchargement de la mise à jour..."),
        tr("Annuler"), 0, 100, this);
    progressDialog->setWindowModality(Qt::WindowModal);
    progressDialog->setMinimumDuration(0);
    progressDialog->show();

    updateDownloadReply = networkManager->get(QNetworkRequest(QUrl(url)));

    connect(updateDownloadReply, &QNetworkReply::downloadProgress,
            this, [this, progressDialog](qint64 bytesReceived, qint64 bytesTotal) {
                if (bytesTotal > 0) {
                    int percentage = (bytesReceived * 100) / bytesTotal;
                    progressDialog->setValue(percentage);

                    // Affichage en MB
                    double receivedMB = bytesReceived / (1024.0 * 1024.0);
                    double totalMB = bytesTotal / (1024.0 * 1024.0);

                    progressDialog->setLabelText(
                        tr("Téléchargement de la mise à jour...\n%1 MB / %2 MB")
                            .arg(receivedMB, 0, 'f', 1)
                            .arg(totalMB, 0, 'f', 1));
                }
            });

    connect(progressDialog, &QProgressDialog::canceled, this, [this]() {
        if (updateDownloadReply) {
            updateDownloadReply->abort();
        }
    });

    connect(updateDownloadReply, &QNetworkReply::finished, this, [this, progressDialog]() {
        progressDialog->hide();
        progressDialog->deleteLater();
        onUpdateDownloadFinished();
    });
}


void MainWindow::onUpdateDownloadFinished() {
    if (!updateDownloadReply) return;

    if (updateDownloadReply->error() != QNetworkReply::NoError) {
        QMessageBox::warning(this, tr("Erreur"),
                             tr("Échec du téléchargement de la mise à jour :\n%1")
                                 .arg(updateDownloadReply->errorString()));
        updateDownloadReply->deleteLater();
        updateDownloadReply = nullptr;
        return;
    }

    // Sauvegarder le fichier dans un dossier temporaire
    QString tempDir = QDir::tempPath();
    QString setupFileName = QString("OnBoarder-%1-Setup.exe").arg(pendingUpdateVersion);
    QString setupFilePath = tempDir + "/" + setupFileName;

    // Supprimer le fichier s'il existe déjà
    QFile::remove(setupFilePath);

    QFile file(setupFilePath);
    if (!file.open(QIODevice::WriteOnly)) {
        QMessageBox::warning(this, tr("Erreur"),
                             tr("Impossible de créer le fichier temporaire :\n%1").arg(setupFilePath));
        updateDownloadReply->deleteLater();
        updateDownloadReply = nullptr;
        return;
    }

    file.write(updateDownloadReply->readAll());
    file.close();

    updateDownloadReply->deleteLater();
    updateDownloadReply = nullptr;

    // Confirmer l'installation
    int result = QMessageBox::question(this, tr("Installation de la mise à jour"),
                                       tr("La mise à jour %1 a été téléchargée avec succès.\n\n"
                                          "Voulez-vous l'installer maintenant ?\n\n"
                                          "L'application va se fermer et l'installateur va démarrer.")
                                           .arg(pendingUpdateVersion),
                                       QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);

    if (result == QMessageBox::Yes) {
        // Log de l'installation
        appendLog(tr("🚀 Lancement de l'installateur de mise à jour : %1").arg(setupFilePath));

        // Lancer l'installateur en arrière-plan
        bool started = QProcess::startDetached(setupFilePath);

        if (started) {
            appendLog(tr("✅ Installateur lancé avec succès"));

            // Message de confirmation avant fermeture
            QMessageBox::information(this, tr("Mise à jour en cours"),
                                     tr("L'installateur de mise à jour a été lancé.\n\n"
                                        "OnBoarder va maintenant se fermer.\n"
                                        "Suivez les instructions de l'installateur pour terminer la mise à jour."));

            // Fermer l'application
            QApplication::quit();
        } else {
            QMessageBox::warning(this, tr("Erreur"),
                                 tr("Impossible de lancer l'installateur.\n\n"
                                    "Vous pouvez l'exécuter manuellement depuis :\n%1").arg(setupFilePath));
        }
    } else {
        QMessageBox::information(this, tr("Mise à jour reportée"),
                                 tr("La mise à jour a été sauvegardée dans :\n%1\n\n"
                                    "Vous pouvez l'installer plus tard en exécutant ce fichier.")
                                     .arg(setupFilePath));
    }
}
void MainWindow::loadApps() {
    installedWingetIds = getInstalledWingetIds();

    QFile file(":/data/apps.json");
    if (!file.open(QIODevice::ReadOnly)) {
        appendLog(tr("Erreur : impossible d'ouvrir apps.json"));
        return;
    }

    QByteArray data = file.readAll();
    qDebug() << "Taille des données JSON:" << data.size() << "bytes";
    file.close();

    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject()) {
        qDebug() << "ERREUR: JSON invalide";
        appendLog(tr("Erreur : apps.json invalide"));
        return;
    }

    QJsonArray appList = doc.object().value("apps").toArray();

    apps.clear();
    ui->listWidget->clear();

    for (const QJsonValue& val : appList) {
        QJsonObject obj = val.toObject();
        AppStatus app;
        app.name = obj.value("name").toString();
        app.icon = obj.value("icon").toString();

        QJsonObject cmds = obj.value("commands").toObject();
        app.installCommand = cmds.value(OS_KEY).toString();

        // Create item in listWidget FIRST
        QListWidgetItem *item = new QListWidgetItem(QIcon(":/icons/" + app.icon), "");
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Unchecked);
        item->setSizeHint(QSize(400, 45)); // Taille par défaut
        app.item = item;

        // Vérifier si c'est une installation personnalisée
        if (obj.contains("install_method") && obj.value("install_method").toString() == "custom") {
            app.uninstallCommand = obj.value("uninstall_command").toString();
            QString executablePath = obj.value("executable_path").toString();
            app.state = isCustomAppInstalled(app.name, executablePath) ? AppState::Installed : AppState::NotInstalled;
        } else {
            QString wingetId = extractWingetId(app.installCommand);
            if (!wingetId.isEmpty()) {
                app.uninstallCommand = QString("winget uninstall %1 --silent").arg(wingetId);
            } else {
                app.uninstallCommand = "";
            }
            app.state = isAppInstalledWinget(wingetId, app.name) ? AppState::Installed : AppState::NotInstalled;
        }

        // Ajouter l'item à la liste AVANT de le configurer
        ui->listWidget->addItem(item);

        // Vérification spécifique pour Visual Studio
        if (app.name.contains("Microsoft Visual Studio Community 2026", Qt::CaseInsensitive)) {
            // Charger la configuration sauvegardée
            QString savedConfig = settings.value("vsConfig", "").toString();
            if (!savedConfig.isEmpty()) {
                app.installCommand = savedConfig;
                app.customConfigData = savedConfig;
            }
            addConfigButtonToItem(app);
        } else {
            // Pour les autres applications, utiliser le texte normal
            updateItemText(app);
        }

        apps.append(app);
    }

    // Forcer une mise à jour de la liste
    ui->listWidget->setUniformItemSizes(false);
    ui->listWidget->update();
    updateButtons();
}

void MainWindow::hideStepIndicator(bool hide) {
    if (ui->sidebarWidget) {
        ui->sidebarWidget->setVisible(!hide);
    }
}

void MainWindow::onBackToMainClicked() {
    ui->stackedWidget->setCurrentIndex(0);
    hideStepIndicator(false);
    updateStepIndicator(1);
}

QString MainWindow::extractWingetId(const QString &installCommand) {
    QStringList parts = installCommand.split(" ", Qt::SkipEmptyParts);
    for (int i = 0; i < parts.size() - 1; ++i) {
        if (parts[i] == "--id") {
            return parts[i + 1];
        }
    }
    // Ancienne logique pour les commandes sans --id
    for (int i = 0; i < parts.size() - 1; ++i) {
        if (parts[i] == "install") {
            QString id = parts[i + 1];
            if (!id.startsWith("-")) {
                return id;
            }
        }
    }
    return "";
}

void MainWindow::updateStepIndicator(int currentStep) {
    int currentPage = ui->stackedWidget->currentIndex();
    if (currentPage == 3 || currentPage == 4) {
        hideStepIndicator(true);
        return;
    }

    hideStepIndicator(false);
    
    // Logique simplifiée : Texte vert pour actif/terminé, Gris pour inactif
    // Pas de cercles, juste du texte
    
    bool isDark = settings.value("darkTheme", false).toBool();
    QString activeColor = "#10b981"; // Vert
    QString inactiveColor = isDark ? "#a1a1aa" : "#6c757d"; // Gris adapté au thème
    
    QString activeStyle = QString("font-size: 13px; color: %1; font-weight: bold; background: transparent;").arg(activeColor);
    QString inactiveStyle = QString("font-size: 13px; color: %1; background: transparent;").arg(inactiveColor);

    // Étape 1
    ui->labelStep1->setStyleSheet(currentStep >= 1 ? activeStyle : inactiveStyle);
    
    // Étape 2 (Actif si step >= 2)
    ui->labelStep2->setStyleSheet(currentStep >= 2 ? activeStyle : inactiveStyle);
    
    // Étape 3 (Actif si step >= 3)
    ui->labelStep3->setStyleSheet(currentStep >= 3 ? activeStyle : inactiveStyle);
}

bool MainWindow::isAppInstalledWinget(const QString &wingetId, const QString &appName) {
#ifdef Q_OS_WIN
    if (wingetId.isEmpty()) return false;

    QProcess proc;
    // Plus simple : juste winget list sans filtres
    proc.start("winget", {"list"});
    if (!proc.waitForFinished(10000)) {
        appendLog(tr("⚠️ Timeout lors de la vérification de %1").arg(wingetId));
        return false;
    }

    QString output = proc.readAllStandardOutput();
    QString error = proc.readAllStandardError();

    appendLog(tr("🔍 Vérification de %1:").arg(wingetId));
    if (!error.isEmpty()) {
        appendLog(tr("⚠️ Erreur winget list: %1").arg(error.trimmed()));
    }

    // Chercher l'ID dans la sortie complète
    // C'est beaucoup plus robuste que l'ancienne méthode
    bool isInstalled = fullWingetOutput.contains(wingetId, Qt::CaseInsensitive);
    
    // Fallback : Vérifier par le nom de l'application si l'ID échoue
    if (!isInstalled && !appName.isEmpty()) {
        isInstalled = fullWingetOutput.contains(appName, Qt::CaseInsensitive);
        if (isInstalled) {
            appendLog(tr("🔍 %1 détecté via le nom (fallback)").arg(appName));
        }
    }
    
    appendLog(tr("🔍 Résultat: %1 %2").arg(wingetId, isInstalled ? tr("TROUVÉ") : tr("NON TROUVÉ")));

    return isInstalled;
#else
    Q_UNUSED(wingetId);
    return false;
#endif
}

void MainWindow::onItemChanged(QListWidgetItem *item) {
    Q_UNUSED(item);
    updateButtons();
}

void MainWindow::updateButtons() {
    bool hasInstallableSelected = false;
    bool hasUninstallableSelected = false;

    for (const AppStatus& app : apps) {
        if (app.item->checkState() == Qt::Checked) {
            // Pour VS, on permet "Installer" même si déjà présent (pour modifier les kits)
            if (app.state == AppState::NotInstalled || app.hasCustomConfig) {
                hasInstallableSelected = true;
            }
            
            if (app.state == AppState::Installed) {
                hasUninstallableSelected = true;
            }
        }
    }

    ui->installButton->setEnabled(hasInstallableSelected);
    ui->uninstallButton->setEnabled(hasUninstallableSelected);
}

void MainWindow::onInstallClicked() {
    uninstalling = false;
    currentAppIndex = 0;

    bool anySelected = false;
    for (AppStatus& app : apps) {
        // Pour VS, on autorise l'install si coché et (pas installé OU a une config perso)
        if (app.item->checkState() == Qt::Checked && (app.state == AppState::NotInstalled || app.hasCustomConfig)) {
            app.state = AppState::Installing;
            updateItemText(app);
            anySelected = true;
        }
    }

    if (!anySelected) {
        appendLog(tr("Aucune application sélectionnée pour l'installation."));
        return;
    }

    ui->progressBar->setValue(0);
    ui->logTextEdit->clear();
    ui->stackedWidget->setCurrentIndex(1); // go to install page
    updateStepIndicator(2);
    startNextInstall();
}
void MainWindow::startNextInstall() {
    while (currentAppIndex < apps.size()) {
        AppStatus &app = apps[currentAppIndex];
        if (app.item->checkState() == Qt::Checked && app.state == AppState::Installing) {
            break;
        }
        ++currentAppIndex;
    }

    if (currentAppIndex >= apps.size()) {
        appendLog(tr("✅ Toutes les installations sont terminées."));
        ui->progressBar->setValue(100);
        updateSummary();
        ui->stackedWidget->setCurrentIndex(2); // page résumé
        updateStepIndicator(3);
        return;
    }

    AppStatus &app = apps[currentAppIndex];
    appendLog(tr("▶️ Installation : %1").arg(app.name));

    if (process) {
        process->deleteLater();
    }
    process = new QProcess(this);

    connect(process, &QProcess::readyReadStandardOutput, this, &MainWindow::handleProcessOutput);
    connect(process, &QProcess::readyReadStandardError, this, &MainWindow::handleProcessError);
    connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, &MainWindow::handleProcessFinished);

#ifdef Q_OS_WIN
    // Utiliser splitCommand pour gérer correctement les guillemets et les arguments
    QStringList parts = QProcess::splitCommand(app.installCommand);
    if (!parts.isEmpty()) {
        QString program = parts.takeFirst();
        process->start(program, parts);
        appendLog(tr("🔍 Commande de lancement : %1 %2").arg(program, parts.join(" ")));
    } else {
        appendLog(tr("❌ Erreur : commande d'installation vide pour %1").arg(app.name));
        ++currentAppIndex;
        startNextInstall();
    }
#else
    process->start("bash", {"-c", app.installCommand});
#endif
}


void MainWindow::handleProcessOutput() {
    if (!process) return;

    QString out = process->readAllStandardOutput();
    if (!out.isEmpty()) {
        // Nettoyer et afficher la sortie
        QStringList lines = out.split('\n', Qt::SkipEmptyParts);
        for (const QString& line : lines) {
            QString cleanLine = line.trimmed();
            if (!cleanLine.isEmpty()) {
                appendLog("📤 " + cleanLine);
            }
        }
    }
}

void MainWindow::handleProcessError() {
    if (!process) return;

    QString err = process->readAllStandardError();
    if (!err.isEmpty()) {
        QStringList lines = err.split('\n', Qt::SkipEmptyParts);
        for (const QString& line : lines) {
            QString cleanLine = line.trimmed();
            if (!cleanLine.isEmpty()) {
                appendLog("⚠️ " + cleanLine);
            }
        }
    }
}

void MainWindow::handleProcessFinished(int exitCode, QProcess::ExitStatus exitStatus) {
    if (currentAppIndex >= apps.size()) return;

    AppStatus &app = apps[currentAppIndex];

    appendLog(tr("🔍 Debug - Fin de processus pour %1. Code : %2, Statut : %3").arg(app.name).arg(exitCode).arg(exitStatus == QProcess::NormalExit ? tr("Normal") : tr("Crash")));

    if (uninstalling) {
        // Pour la désinstallation, on est un peu plus souple sur les codes de succès
        bool isSuccess = (exitCode == 0 || exitCode == 3010 || exitCode == 1641);
        app.state = isSuccess ? AppState::NotInstalled : AppState::Installed;
        
        if (isSuccess) {
            appendLog(tr("✅ %1 désinstallé avec succès").arg(app.name));
        } else {
            appendLog(tr("❌ Échec de la désinstallation de %1 (code: %2)").arg(app.name).arg(exitCode));
        }
        
        updateItemText(app);
        currentAppIndex++;
        startNextUninstall();
        
    } else {
        // Liste des codes de succès pour les installeurs Windows (incluant reboot requis)
        // 0: Succès, 3010: Succès (Reboot requis), 1641: Succès (Reboot initié)
        bool isSuccess = (exitCode == 0 || exitCode == 3010 || exitCode == 1641);
        
        if (isSuccess && app.name.contains("Visual Studio", Qt::CaseInsensitive)) {
            // Pour Visual Studio, on vérifie si l'installateur tourne encore
            appendLog(tr("🔍 Le processus principal est terminé. Vérification de l'installateur en arrière-plan..."));
            // Attendre 5 secondes avant la première vérification pour laisser le temps au processus de démarrer
            QTimer::singleShot(5000, this, &MainWindow::checkVSInstallerRunning);
            return; // On ne passe pas tout de suite à la suite, checkVSInstallerRunning s'en chargera
        }

        app.state = isSuccess ? AppState::Installed : AppState::NotInstalled;
        if (isSuccess) {
            if (exitCode == 3010 || exitCode == 1641) {
                appendLog(tr("✅ %1 installé avec succès (Redémarrage requis)").arg(app.name));
            } else {
                appendLog(tr("✅ %1 installé avec succès").arg(app.name));
            }
        } else {
            appendLog(tr("❌ Échec de l'installation de %1 (code: %2)").arg(app.name).arg(exitCode));
            if (exitCode == 1) {
                appendLog(tr("💡 Note: Si l'application s'est lancée, cet échec peut être un faux positif (comportement fréquent avec certains installateurs type Velopack/Squirrel)."));
            }
        }
        
        updateItemText(app);
        currentAppIndex++;
        
        // Mettre à jour la barre de progression
        int totalSelected = 0;
        int completed = 0;
        for (int i = 0; i < apps.size(); ++i) {
            if (apps[i].item->checkState() == Qt::Checked) {
                totalSelected++;
                if (i < currentAppIndex) {
                    completed++;
                }
            }
        }

        if (totalSelected > 0) {
            ui->progressBar->setValue((completed * 100) / totalSelected);
        }

        startNextInstall();
    }
}

QString MainWindow::generateVSInstallCommand() {
    // Ajout de --force pour permettre la modification via réinstallation
    // Retrait de --disable-interactivity pour laisser l'interface de l'installer visible si besoin
    // Utilisation de --passive au lieu de --quiet pour voir la progression graphiquement
    QString baseCommand = "winget install --id Microsoft.VisualStudio.Community --force --accept-source-agreements --accept-package-agreements --override \"";
    QStringList workloads;

    if (ui->webDevCheckBox->isChecked())
        workloads << "--add Microsoft.VisualStudio.Workload.NetWeb";
    if (ui->desktopDevCheckBox->isChecked())
        workloads << "--add Microsoft.VisualStudio.Workload.NativeDesktop";
    if (ui->netDesktopCheckBox->isChecked())
        workloads << "--add Microsoft.VisualStudio.Workload.ManagedDesktop";
    if (ui->gameDevCheckBox->isChecked())
        workloads << "--add Microsoft.VisualStudio.Workload.ManagedGame";
    if (ui->mobileDevCheckBox->isChecked())
        workloads << "--add Microsoft.VisualStudio.Workload.NetCrossPlat";
    if (ui->pythonDevCheckBox->isChecked())
        workloads << "--add Microsoft.VisualStudio.Workload.Python";
    if (ui->dataDevCheckBox->isChecked())
        workloads << "--add Microsoft.VisualStudio.Workload.DataScience";
    if (ui->azureDevCheckBox->isChecked())
        workloads << "--add Microsoft.VisualStudio.Workload.Azure";
    if (ui->nodeJSDevCheckBox->isChecked())
        workloads << "--add Microsoft.VisualStudio.Workload.Node";

    QString workloadsStr = workloads.join(" ");

    if (ui->includeRecommendedCheckBox->isChecked())
        workloadsStr += " --includeRecommended";

    workloadsStr += " --passive --norestart";

    // Si aucun workload n'est sélectionné, installer juste l'IDE de base avec les accords acceptés
    if (workloads.isEmpty()) {
        return "winget install --id Microsoft.VisualStudio.Community --force --accept-source-agreements --accept-package-agreements --override \"--passive --norestart\"";
    }

    return baseCommand + workloadsStr + "\"";
}

QStringList MainWindow::getInstalledWingetIds() {
    QStringList ids;
#ifdef Q_OS_WIN
    appendLog(tr("🚀 Récupération des applications installées (winget list)..."));
    QProcess proc;
    // On augmente le timeout car winget peut être lent
    proc.start("winget", {"list"});
    if (!proc.waitForFinished(60000)) {
        appendLog(tr("⚠️ Timeout winget list"));
        return ids;
    }

    // On stocke TOUTE la sortie brute
    // C'est beaucoup plus fiable que d'essayer de parser les colonnes
    // car winget coupe les noms longs et mélange les colonnes parfois.
    fullWingetOutput = proc.readAllStandardOutput();
    
    // On loggue juste le succès, pas tout le contenu
    appendLog(tr("✅ Liste des applications récupérée avec succès (%1 octets)").arg(fullWingetOutput.size()));
    
    // On retourne une liste vide car on utilise maintenant fullWingetOutput
    // Mais pour compatibilité avec le reste du code existant qui pourrait l'utiliser...
    // En fait, on va modifier isAppInstalledWinget pour utiliser fullWingetOutput
#endif
    return ids;
}

void MainWindow::onUninstallClicked() {
    uninstalling = true;
    currentAppIndex = 0;

    bool anySelected = false;
    for (AppStatus& app : apps) {
        if (app.item->checkState() == Qt::Checked && app.state == AppState::Installed) {
            app.state = AppState::Installing; // reuse Installing state for "processing"
            updateItemText(app);
            anySelected = true;
        }
    }

    if (!anySelected) {
        appendLog(tr("Aucune application installée sélectionnée pour la désinstallation."));
        return;
    }

    ui->progressBar->setValue(0);
    ui->logTextEdit->clear();
    ui->stackedWidget->setCurrentIndex(1); // go to install page (reuse)
    updateStepIndicator(2);
    startNextUninstall();
}

void MainWindow::startNextUninstall() {
    while (currentAppIndex < apps.size()) {
        AppStatus &app = apps[currentAppIndex];
        if (app.item->checkState() == Qt::Checked && app.state == AppState::Installing) {
            break;
        }
        ++currentAppIndex;
    }

    if (currentAppIndex >= apps.size()) {
        appendLog(tr("✅ Toutes les désinstallations sont terminées."));
        ui->progressBar->setValue(100);
        updateSummary();
        ui->stackedWidget->setCurrentIndex(2);
        updateStepIndicator(3);
        return;
    }

    AppStatus &app = apps[currentAppIndex];

#ifdef Q_OS_WIN
    // Utilisation uniforme de splitCommand pour éviter les problèmes de guillemets
    QString commandToExecute = app.uninstallCommand;
    if (commandToExecute.isEmpty()) {
        QString wingetId = extractWingetId(app.installCommand);
        if (wingetId.isEmpty()) {
            appendLog(tr("❌ Erreur : impossible d'extraire l'ID winget pour %1").arg(app.name));
            app.state = AppState::Installed;
            updateItemText(app);
            ++currentAppIndex;
            startNextUninstall();
            return;
        }
        commandToExecute = QString("winget uninstall --id %1 --silent --accept-source-agreements").arg(wingetId);
    }

    appendLog(tr("▶️ Désinstallation : %1").arg(app.name));
    if (process) process->deleteLater();
    process = new QProcess(this);
    connect(process, &QProcess::readyReadStandardOutput, this, &MainWindow::handleProcessOutput);
    connect(process, &QProcess::readyReadStandardError, this, &MainWindow::handleProcessError);
    connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, &MainWindow::handleProcessFinished);

    QStringList parts = QProcess::splitCommand(commandToExecute);
    if (!parts.isEmpty()) {
        QString program = parts.takeFirst();
        process->start(program, parts);
        appendLog(tr("🔍 Commande de lancement : %1 %2").arg(program, parts.join(" ")));
    } else {
        appendLog(tr("❌ Erreur : commande de désinstallation invalide pour %1").arg(app.name));
        ++currentAppIndex;
        startNextUninstall();
    }
#else
    appendLog(tr("❌ Désinstallation non supportée sur cette plateforme pour %1").arg(app.name));
    app.state = AppState::Installed;
    updateItemText(app);
    ++currentAppIndex;
    startNextUninstall();
#endif
}

void MainWindow::updateStatusLabel(QLabel *statusLabel, const AppStatus &app) {
    QString statusStr;
    QString colorStyle;
    bool isDark = settings.value("darkTheme", false).toBool();
    switch (app.state) {
    case AppState::Installed:
        statusStr = tr("[installé]");
        colorStyle = QString("color: %1; font-weight: bold;").arg(isDark ? "#10b981" : "#059669");
        break;
    case AppState::Installing:
        statusStr = tr("[en cours...]");
        colorStyle = QString("color: %1; font-weight: bold;").arg(isDark ? "#3b82f6" : "#2563eb");
        break;
    case AppState::NotInstalled:
    default:
        statusStr = tr("[non installé]");
        colorStyle = QString("color: %1; font-weight: bold;").arg(isDark ? "#ef4444" : "#dc2626");
        break;
    }
    statusLabel->setText(statusStr);
    statusLabel->setStyleSheet(colorStyle);
}

void MainWindow::addConfigButtonToItem(AppStatus& app) {
    if (!app.name.contains("Microsoft Visual Studio Community 2026", Qt::CaseInsensitive)) {
        return;
    }

    if (!app.item) {
        return;
    }

    app.hasCustomConfig = true;

    // Supprimer l'ancien widget s'il existe
    QWidget *oldWidget = ui->listWidget->itemWidget(app.item);
    if (oldWidget) {
        oldWidget->deleteLater();
    }

    // Créer le widget personnalisé
    ClickableItemWidget *itemWidget = new ClickableItemWidget(app.item, ui->listWidget);
    itemWidget->setObjectName("vsItemWidget");
    
    QHBoxLayout *layout = new QHBoxLayout(itemWidget);
    layout->setContentsMargins(10, 5, 10, 5);
    layout->setSpacing(10);

    // Label pour le nom
    QLabel *nameLabel = new QLabel("Microsoft Visual Studio Community 2026", itemWidget);
    nameLabel->setObjectName("nameLabel");
    nameLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    // Label pour le statut
    QLabel *statusLabel = new QLabel(itemWidget);
    statusLabel->setObjectName("statusLabel");
    statusLabel->setMinimumWidth(100);
    statusLabel->setAlignment(Qt::AlignCenter);
    updateStatusLabel(statusLabel, app);

    // Bouton de configuration
    ConfigButton *configButton = new ConfigButton(itemWidget);
    style()->unpolish(configButton);
    style()->polish(configButton);

    layout->addWidget(nameLabel);
    layout->addStretch();
    layout->addWidget(statusLabel);
    layout->addWidget(configButton);

    // Appliquer le style au nameLabel après création car updateItemText l'utilise
    updateItemText(app);

    // Style important pour éviter le look "boxy"
    itemWidget->setStyleSheet("background: transparent; border: none;");
    
    ui->listWidget->setItemWidget(app.item, itemWidget);
    app.item->setSizeHint(QSize(300, 50));

    // Connecter le signal de clic pour basculer la sélection
    connect(itemWidget, &ClickableItemWidget::itemClicked, this, &MainWindow::updateButtons);

    // Connecter le bouton de configuration
    connect(configButton, &QPushButton::clicked, [this]() {
        for (int i = 0; i < apps.size(); ++i) {
            if (apps[i].name.contains("Microsoft Visual Studio Community 2026", Qt::CaseInsensitive)) {
                showVSConfigDialog(i);
                return;
            }
        }
    });
}

void MainWindow::showVSConfigDialog(int appIndex) {
    currentConfigAppIndex = appIndex;

    // Charger la configuration précédente si elle existe
    QString savedConfig = settings.value("vsConfig", "").toString();

    // Réinitialiser toutes les checkboxes
    ui->webDevCheckBox->setChecked(false);
    ui->desktopDevCheckBox->setChecked(false);
    ui->netDesktopCheckBox->setChecked(false);
    ui->gameDevCheckBox->setChecked(false);
    ui->mobileDevCheckBox->setChecked(false);
    ui->pythonDevCheckBox->setChecked(false);
    ui->dataDevCheckBox->setChecked(false);
    ui->azureDevCheckBox->setChecked(false);
    ui->nodeJSDevCheckBox->setChecked(false);
    ui->includeRecommendedCheckBox->setChecked(true);

    // Charger la configuration sauvegardée
    if (!savedConfig.isEmpty()) {
        if (savedConfig.contains("Microsoft.VisualStudio.Workload.NetWeb"))
            ui->webDevCheckBox->setChecked(true);
        if (savedConfig.contains("Microsoft.VisualStudio.Workload.NativeDesktop"))
            ui->desktopDevCheckBox->setChecked(true);
        if (savedConfig.contains("Microsoft.VisualStudio.Workload.ManagedDesktop"))
            ui->netDesktopCheckBox->setChecked(true);
        if (savedConfig.contains("Microsoft.VisualStudio.Workload.ManagedGame"))
            ui->gameDevCheckBox->setChecked(true);
        if (savedConfig.contains("Microsoft.VisualStudio.Workload.NetCrossPlat"))
            ui->mobileDevCheckBox->setChecked(true);
        if (savedConfig.contains("Microsoft.VisualStudio.Workload.Python"))
            ui->pythonDevCheckBox->setChecked(true);
        if (savedConfig.contains("Microsoft.VisualStudio.Workload.DataScience"))
            ui->dataDevCheckBox->setChecked(true);
        if (savedConfig.contains("Microsoft.VisualStudio.Workload.Azure"))
            ui->azureDevCheckBox->setChecked(true);
        if (savedConfig.contains("Microsoft.VisualStudio.Workload.Node"))
            ui->nodeJSDevCheckBox->setChecked(true);
        if (!savedConfig.contains("--includeRecommended"))
            ui->includeRecommendedCheckBox->setChecked(false);
    }

    ui->stackedWidget->setCurrentIndex(4); // pageVisualStudioConfig
    hideStepIndicator(true);
}

void MainWindow::onVSConfigOkClicked() {
    if (currentConfigAppIndex == -1 || currentConfigAppIndex >= apps.size()) {
        return;
    }

    QString customCommand = generateVSInstallCommand();
    apps[currentConfigAppIndex].installCommand = customCommand;
    apps[currentConfigAppIndex].customConfigData = customCommand;

    settings.setValue("vsConfig", customCommand);
    settings.sync();

    appendLog(tr("✅ Configuration Visual Studio sauvegardée"));
    appendLog(tr("🔧 Commande générée : %1").arg(customCommand));

    ui->stackedWidget->setCurrentIndex(0);
    hideStepIndicator(false);
    updateStepIndicator(1);
    currentConfigAppIndex = -1;
}

void MainWindow::onVSConfigCancelClicked() {
    ui->stackedWidget->setCurrentIndex(0);
    hideStepIndicator(false);
    updateStepIndicator(1);
    currentConfigAppIndex = -1;
}

void MainWindow::updateItemText(AppStatus &app) {
    bool isDark = settings.value("darkTheme", false).toBool();
    if (app.hasCustomConfig) {
        // Pour Visual Studio avec configuration personnalisée
        QWidget *itemWidget = ui->listWidget->itemWidget(app.item);
        if (itemWidget) {
            // Mettre à jour le label de statut
            QLabel *statusLabel = itemWidget->findChild<QLabel*>("statusLabel");
            if (statusLabel) {
                updateStatusLabel(statusLabel, app);
            }

            // Mettre à jour la couleur du nom selon l'état
            QLabel *nameLabel = itemWidget->findChild<QLabel*>("nameLabel");
            if (nameLabel) {
                QString textStyle;
                switch (app.state) {
                case AppState::Installed:
                    textStyle = QString("font-size: 13px; font-weight: 500; color: %1;").arg(isDark ? "#10b981" : "#059669"); 
                    break;
                case AppState::Installing:
                    textStyle = QString("font-size: 13px; font-weight: 500; color: %1;").arg(isDark ? "#3b82f6" : "#2563eb"); 
                    break;
                case AppState::NotInstalled:
                default:
                    textStyle = QString("font-size: 13px; font-weight: 500; color: %1;").arg(isDark ? "#ef4444" : "#dc2626"); 
                    break;
                }
                nameLabel->setStyleSheet(textStyle);
            }
        }
    } else {
        // Pour les éléments normaux, utiliser l'ancien système
        QString statusStr;
        QColor color;
        switch (app.state) {
        case AppState::Installed:
            statusStr = tr("[installé]");
            color = isDark ? QColor("#10b981") : Qt::darkGreen;
            break;
        case AppState::Installing:
            statusStr = tr("[en cours...]");
            color = isDark ? QColor("#3b82f6") : Qt::blue;
            break;
        case AppState::NotInstalled:
        default:
            statusStr = tr("[non installé]");
            // Rouge pour non installé
            color = isDark ? QColor("#ef4444") : Qt::red;
            break;
        }
        
        // on laisse comme avant mais on change juste la couleur globale de l'item
        app.item->setText(app.name + " " + statusStr);
        app.item->setForeground(QBrush(color));
    }
}

void MainWindow::checkVSInstallerRunning() {
    QProcess *checkProcess = new QProcess(this);
    connect(checkProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), [this, checkProcess](int exitCode, QProcess::ExitStatus exitStatus) {
        QString output = checkProcess->readAllStandardOutput();
        QString error = checkProcess->readAllStandardError();
        checkProcess->deleteLater();

        // Debug : afficher la sortie brute pour comprendre pourquoi ça rate
        if (!output.isEmpty()) {
            // On ne loggue pas tout pour ne pas spammer, mais on cherche
            // appendLog("Debug tasklist: " + output.left(100) + "..."); 
        }

        bool isRunning = output.contains("vs_installer.exe", Qt::CaseInsensitive) || 
                         output.contains("vs_setup_bootstrapper.exe", Qt::CaseInsensitive) ||
                         output.contains("setup.exe", Qt::CaseInsensitive); // Parfois setup.exe reste actif

        if (isRunning) {
            appendLog(tr("⏳ L'installateur Visual Studio est toujours actif... (Vérification dans 5s)"));
            // Vérifier à nouveau dans 5 secondes
            QTimer::singleShot(5000, this, &MainWindow::checkVSInstallerRunning);
        } else {
            appendLog(tr("✅ Aucun processus d'installation détecté via tasklist."));
            appendLog(tr("✅ Installation de Visual Studio confirmée terminée."));
            
            // Marquer comme installé et passer au suivant
            if (currentAppIndex < apps.size()) {
                AppStatus &app = apps[currentAppIndex];
                if (app.name.contains("Visual Studio", Qt::CaseInsensitive)) {
                    app.state = AppState::Installed;
                    updateItemText(app);
                    
                    // Continuer l'installation des autres apps
                    currentAppIndex++;
                    startNextInstall();
                }
            }
        }
    });
    
    // Utiliser CSV pour être sûr du format, mais vérifier si window title contient Visual Studio pourrait être mieux ?
    // Restons sur IMAGENAME pour l'instant mais sans filtre strict pour voir plus large si besoin
    // checkProcess->start("tasklist", QStringList() << "/FO" << "CSV" << "/NH");
    // Filtrons quand même pour éviter de parser 300 processus
    checkProcess->start("tasklist", QStringList() << "/FI" << "IMAGENAME eq vs_*" << "/FO" << "CSV" << "/NH");
}

void MainWindow::updateSummary() {
    QString summary = tr("=== Résumé des opérations ===") + "\n\n";

    int installed = 0, failed = 0, uninstalled = 0;

    for (const AppStatus& app : apps) {
        if (app.item->checkState() == Qt::Checked) {
            QString operation = uninstalling ? tr("Désinstallation") : tr("Installation");

            if ((uninstalling && app.state == AppState::NotInstalled) ||
                (!uninstalling && app.state == AppState::Installed)) {
                summary += tr("✅ %1 de %2 : Succès").arg(operation, app.name) + "\n";
                if (uninstalling) uninstalled++;
                else installed++;
            } else {
                summary += tr("❌ %1 de %2 : Échec").arg(operation, app.name) + "\n";
                failed++;
            }
        }
    }

    summary += "\n" + tr("=== Statistiques ===") + "\n";
    if (uninstalling) {
        summary += tr("Désinstallées : %1").arg(uninstalled) + "\n";
    } else {
        summary += tr("Installées : %1").arg(installed) + "\n";
    }
    summary += tr("Échecs : %1").arg(failed) + "\n";

    ui->summaryTextEdit->setPlainText(summary);
}

void MainWindow::appendLog(const QString &text) {
    ui->logTextEdit->append(text);
}

void MainWindow::onShowLogsToggled(bool checked) {
    ui->logTextEdit->setVisible(checked);
}

void MainWindow::onRestartClicked() {
    ui->stackedWidget->setCurrentIndex(0);
    updateStepIndicator(1);
    hideStepIndicator(false);
    ui->progressBar->setValue(0);
    ui->logTextEdit->clear();
    ui->summaryTextEdit->clear();

    // Reset states and check states
    for (AppStatus& app : apps) {
        app.item->setCheckState(Qt::Unchecked);

        if (app.hasCustomConfig) {
            // Pour Visual Studio avec configuration personnalisée
            QString wingetId = extractWingetId(app.installCommand);
            app.state = isAppInstalledWinget(wingetId, app.name) ? AppState::Installed : AppState::NotInstalled;
            updateItemText(app); // Cela mettra à jour le label de statut dans le widget personnalisé
        } else {
            // Pour les applications normales
            if (app.name.contains("Wrike", Qt::CaseInsensitive)) {
                // Application avec installation personnalisée
                QString executablePath = ""; // Vous devrez extraire cela du JSON si nécessaire
                app.state = isCustomAppInstalled(app.name, executablePath) ? AppState::Installed : AppState::NotInstalled;
            } else {
                // Applications winget normales
                QString wingetId = extractWingetId(app.installCommand);
                app.state = isAppInstalledWinget(wingetId, app.name) ? AppState::Installed : AppState::NotInstalled;
            }
            updateItemText(app);
        }
    }

    updateButtons();
}

void MainWindow::onQuitClicked() {
    close();
}
