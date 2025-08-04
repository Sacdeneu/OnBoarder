#include "mainwindow.h"
#include "ui_mainwindow.h"
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
    uninstalling(false)
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

    ui->progressBar->setValue(0);
    ui->logTextEdit->setVisible(false);
    ui->stackedWidget->setCurrentIndex(0); // Start at pageSelect

    // Initially disable buttons
    ui->installButton->setEnabled(false);
    ui->uninstallButton->setEnabled(false);

    // Set window title
    setWindowTitle(tr("Gestionnaire d'applications"));

    loadApps();
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

    startNextUninstall();
}void MainWindow::startNextUninstall() {
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
