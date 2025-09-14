#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QProgressDialog>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
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
    ui(new Ui::MainWindow),
    process(nullptr),
    currentAppIndex(0),
    uninstalling(false),
    settings("Sacdeneu", "OnBoarder"),
    networkManager(new QNetworkAccessManager(this)),
    autoUpdateEnabled(false),
    updateDownloadReply(nullptr)
{
    ui->setupUi(this);

    // Connect buttons
    connect(ui->installButton, &QPushButton::clicked, this, &MainWindow::onInstallClicked);
    connect(ui->uninstallButton, &QPushButton::clicked, this, &MainWindow::onUninstallClicked);
    connect(ui->showLogsCheckBox, &QCheckBox::toggled, this, &MainWindow::onShowLogsToggled);
    connect(ui->restartButton, &QPushButton::clicked, this, &MainWindow::onRestartClicked);
    connect(ui->quitButton, &QPushButton::clicked, this, &MainWindow::onQuitClicked);

    // Connect list widget selection changes
    connect(ui->listWidget, &QListWidget::itemChanged, this, &MainWindow::onItemChanged);

    connect(ui->settingsButton, &QPushButton::clicked, this, &MainWindow::onSettingsClicked);
    connect(ui->darkThemeCheckBox, &QCheckBox::toggled, this, &MainWindow::onDarkThemeToggled);
    connect(ui->autoUpdateCheckBox, &QCheckBox::toggled, this, &MainWindow::onAutoUpdateToggled);
    connect(ui->checkUpdateButton, &QPushButton::clicked, this, &MainWindow::onCheckUpdateClicked);

    loadSettings();

    if (autoUpdateEnabled) {
        checkForUpdates(false);
    }

    ui->progressBar->setValue(0);
    ui->logTextEdit->setVisible(false);
    ui->stackedWidget->setCurrentIndex(0); // Start at pageSelect

    // Initially disable buttons
    ui->installButton->setEnabled(false);
    ui->uninstallButton->setEnabled(false);

    // Set window title
    setWindowTitle(tr("Gestionnaire d'applications"));

    loadApps();
    updateStepIndicator(1);
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
        // Vérifier si l'exécutable existe directement (par exemple chemin utilisateur)
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
        qApp->setStyleSheet("QWidget { background-color: #121212; color: #e0e0e0; }");
    } else {
        qApp->setStyleSheet("");
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
    ui->stackedWidget->setCurrentIndex(3); // pageSettings
}

void MainWindow::onDarkThemeToggled(bool checked) {
    applyDarkTheme(checked);
    saveSettings();
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
    QFile file(":/data/apps.json");
    if (!file.open(QIODevice::ReadOnly)) {
        appendLog(tr("Erreur : impossible d'ouvrir apps.json"));
        return;
    }

    QByteArray data = file.readAll();
    file.close();

    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject()) {
        appendLog(tr("Erreur : apps.json invalide"));
        return;
    }

    QJsonArray appList = doc.object().value("apps").toArray();

    apps.clear();
    ui->listWidget->clear();

    appendLog(tr("=== Chargement des applications ==="));

    for (const QJsonValue& val : appList) {
        QJsonObject obj = val.toObject();

        AppStatus app;
        app.name = obj.value("name").toString();
        app.icon = obj.value("icon").toString();
        QJsonObject cmds = obj.value("commands").toObject();

        app.installCommand = cmds.value(OS_KEY).toString();

        // Vérifier si c'est une installation personnalisée
        if (obj.contains("install_method") && obj.value("install_method").toString() == "custom") {
            // Installation personnalisée
            app.uninstallCommand = obj.value("uninstall_command").toString();
            QString executablePath = obj.value("executable_path").toString();

            appendLog(tr("🔍 %1 - Installation personnalisée détectée").arg(app.name));
            app.state = isCustomAppInstalled(app.name, executablePath) ? AppState::Installed : AppState::NotInstalled;
        } else {
            // Installation via winget classique
            QString wingetId = extractWingetId(app.installCommand);
            appendLog(tr("🔍 %1 - ID extrait: %2").arg(app.name, wingetId.isEmpty() ? tr("VIDE") : wingetId));

            if (!wingetId.isEmpty()) {
                app.uninstallCommand = QString("winget uninstall %1 --silent").arg(wingetId);
            } else {
                app.uninstallCommand = "";
            }

            // Determine state
            app.state = isAppInstalledWinget(wingetId) ? AppState::Installed : AppState::NotInstalled;
        }

        // Create item in listWidget
        QListWidgetItem *item = new QListWidgetItem(QIcon(":/icons/" + app.icon), "");
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Unchecked);

        app.item = item;
        updateItemText(app);

        ui->listWidget->addItem(item);
        apps.append(app);
    }

    appendLog(tr("=== Fin du chargement ==="));
    updateButtons();
}

QString MainWindow::extractWingetId(const QString &installCommand) {
    // Extract ID from winget install command
    QStringList parts = installCommand.split(" ", Qt::SkipEmptyParts);

    // Find "install" keyword and get the next part
    for (int i = 0; i < parts.size() - 1; ++i) {
        if (parts[i] == "install") {
            QString id = parts[i + 1];
            // Remove any flags that might be attached (like -e)
            if (id.startsWith("-")) {
                continue; // skip flags and get next part
            }
            return id;
        }
    }

    return "";
}

void MainWindow::updateStepIndicator(int currentStep) {
    // Style pour l'étape active (bleue)
    QString activeStyle = "QLabel {"
                          "background-color: #0d6efd;"
                          "color: white;"
                          "border-radius: 20px;"
                          "font-weight: bold;"
                          "font-size: 14px;"
                          "}";

    // Style pour les étapes inactives (grises)
    QString inactiveStyle = "QLabel {"
                            "background-color: #6c757d;"
                            "color: white;"
                            "border-radius: 20px;"
                            "font-weight: bold;"
                            "font-size: 14px;"
                            "}";

    // Style pour les étapes terminées (verte)
    QString completedStyle = "QLabel {"
                             "background-color: #198754;"
                             "color: white;"
                             "border-radius: 20px;"
                             "font-weight: bold;"
                             "font-size: 14px;"
                             "}";

    // Réinitialiser tous les cercles
    ui->labelStepCircle1->setStyleSheet(currentStep > 1 ? completedStyle : (currentStep == 1 ? activeStyle : inactiveStyle));
    ui->labelStepCircle2->setStyleSheet(currentStep > 2 ? completedStyle : (currentStep == 2 ? activeStyle : inactiveStyle));
    ui->labelStepCircle3->setStyleSheet(currentStep == 3 ? activeStyle : inactiveStyle);

    // Mettre à jour les couleurs des textes
    QString activeTextStyle = "font-size: 12px; color: #0d6efd; font-weight: bold;";
    QString inactiveTextStyle = "font-size: 12px; color: #6c757d;";
    QString completedTextStyle = "font-size: 12px; color: #198754; font-weight: bold;";

    ui->labelStep1->setStyleSheet(currentStep > 1 ? completedTextStyle : (currentStep == 1 ? activeTextStyle : inactiveTextStyle));
    ui->labelStep2->setStyleSheet(currentStep > 2 ? completedTextStyle : (currentStep == 2 ? activeTextStyle : inactiveTextStyle));
    ui->labelStep3->setStyleSheet(currentStep == 3 ? activeTextStyle : inactiveTextStyle);
}

bool MainWindow::isAppInstalledWinget(const QString &wingetId) {
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
    bool isInstalled = output.contains(wingetId, Qt::CaseInsensitive);
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
            if (app.state == AppState::NotInstalled) {
                hasInstallableSelected = true;
            } else if (app.state == AppState::Installed) {
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
        if (app.item->checkState() == Qt::Checked && app.state == AppState::NotInstalled) {
            app.state = AppState::Installing;
            updateItemText(app);
            anySelected = true;
        }
    }

    if (!anySelected) {
        appendLog(tr("Aucune application non installée sélectionnée pour l'installation."));
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
    if (app.installCommand.startsWith("powershell", Qt::CaseInsensitive)) {
        // Extraire arguments de la commande Powershell
        QStringList parts = QProcess::splitCommand(app.installCommand);
        if (!parts.isEmpty()) {
            QString exe = parts.takeFirst();
            process->start(exe, parts);
        } else {
            appendLog(tr("❌ Erreur : commande PowerShell vide pour %1").arg(app.name));
        }
    } else {
        process->start("cmd", {"/C", app.installCommand});
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

    appendLog(tr("🔍 Debug - Code de sortie : %1, Statut : %2").arg(exitCode).arg(exitStatus == QProcess::NormalExit ? tr("Normal") : tr("Crash")));

    if (uninstalling) {
        app.state = (exitCode == 0) ? AppState::NotInstalled : AppState::Installed;
        if (exitCode == 0) {
            appendLog(tr("✅ %1 désinstallé avec succès").arg(app.name));
        } else {
            appendLog(tr("❌ Échec de la désinstallation de %1 (code: %2)").arg(app.name).arg(exitCode));
            // Afficher les dernières erreurs si disponibles
            if (process) {
                QString finalError = process->readAllStandardError();
                if (!finalError.isEmpty()) {
                    appendLog(tr("❌ Erreur finale : %1").arg(finalError.trimmed()));
                }
            }
        }
    } else {
        app.state = (exitCode == 0) ? AppState::Installed : AppState::NotInstalled;
        if (exitCode == 0) {
            appendLog(tr("✅ %1 installé avec succès").arg(app.name));
        } else {
            appendLog(tr("❌ Échec de l'installation de %1 (code: %2)").arg(app.name).arg(exitCode));
        }
    }

    updateItemText(app);

    ++currentAppIndex;

    // Calculate progress based on selected items
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

    if (uninstalling) {
        startNextUninstall();
    } else {
        startNextInstall();
    }
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
    if (!app.uninstallCommand.isEmpty() && app.uninstallCommand.contains("powershell", Qt::CaseInsensitive)) {
        appendLog(tr("▶️ Désinstallation personnalisée : %1").arg(app.name));

        if (process) {
            process->deleteLater();
        }
        process = new QProcess(this);

        connect(process, &QProcess::readyReadStandardOutput, this, &MainWindow::handleProcessOutput);
        connect(process, &QProcess::readyReadStandardError, this, &MainWindow::handleProcessError);
        connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, &MainWindow::handleProcessFinished);

        QStringList parts = QProcess::splitCommand(app.uninstallCommand);
        if (!parts.isEmpty()) {
            QString exe = parts.takeFirst();
            process->start(exe, parts);
        } else {
            appendLog(tr("❌ Erreur : commande PowerShell vide pour la désinstallation de %1").arg(app.name));
            ++currentAppIndex;
            startNextUninstall();
            return;
        }

    } else {
        // Désinstallation winget classique
        QString wingetId = extractWingetId(app.installCommand);
        appendLog(tr("🔍 Debug - Commande d'installation : %1").arg(app.installCommand));
        appendLog(tr("🔍 Debug - ID winget extrait : %1").arg(wingetId));

        if (wingetId.isEmpty()) {
            appendLog(tr("❌ Erreur : impossible d'extraire l'ID winget pour %1").arg(app.name));
            app.state = AppState::Installed;
            updateItemText(app);
            ++currentAppIndex;
            startNextUninstall();
            return;
        }

        appendLog(tr("▶️ Désinstallation : %1 (ID: %2)").arg(app.name, wingetId));

        if (process) {
            process->deleteLater();
        }
        process = new QProcess(this);

        connect(process, &QProcess::readyReadStandardOutput, this, &MainWindow::handleProcessOutput);
        connect(process, &QProcess::readyReadStandardError, this, &MainWindow::handleProcessError);
        connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, &MainWindow::handleProcessFinished);

        QStringList args = {"uninstall", wingetId, "--silent"};
        appendLog(tr("🔍 Debug - Commande complète : winget %1").arg(args.join(" ")));

        process->start("winget", args);
    }
#else
    appendLog(tr("❌ Désinstallation non supportée sur cette plateforme pour %1").arg(app.name));
    app.state = AppState::Installed;
    updateItemText(app);
    ++currentAppIndex;
    startNextUninstall();
#endif
}


void MainWindow::updateItemText(AppStatus &app) {
    QString statusStr;
    QColor color;

    switch (app.state) {
    case AppState::Installed:
        statusStr = tr("[installé]");
        color = Qt::darkGreen;
        break;
    case AppState::Installing:
        statusStr = tr("[en cours...]");
        color = Qt::blue;
        break;
    case AppState::NotInstalled:
    default:
        statusStr = tr("[non installé]");
        color = Qt::red;
        break;
    }

    app.item->setText(app.name + " " + statusStr);
    app.item->setForeground(QBrush(color));
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
    ui->progressBar->setValue(0);
    ui->logTextEdit->clear();
    ui->summaryTextEdit->clear();

    // Reset states and check states
    for (AppStatus& app : apps) {
        QString wingetId = extractWingetId(app.installCommand);
        app.state = isAppInstalledWinget(wingetId) ? AppState::Installed : AppState::NotInstalled;
        app.item->setCheckState(Qt::Unchecked);
        updateItemText(app);
    }

    updateButtons();
}

void MainWindow::onQuitClicked() {
    close();
}
