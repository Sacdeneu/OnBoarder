#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "qlabel.h"
#include <QMainWindow>
#include <QJsonArray>
#include <QProcess>
#include <QVector>
#include <QListWidgetItem>
#include <QSettings>
#include <QNetworkAccessManager>
#include <QNetworkReply>


QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

enum class AppState { NotInstalled, Installing, Installed };

struct AppStatus {
    QString name;
    QString icon;
    QString installCommand;
    QString uninstallCommand;
    QString version;
    AppState state;
    QListWidgetItem* item;
};

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void onUpdateDownloadFinished();
    void onSettingsClicked();
    void onDarkThemeToggled(bool checked);
    void onAutoUpdateToggled(bool checked);
    void onCheckUpdateClicked();
    void onUpdateButtonClicked();
    void checkForUpdates(bool manual = false);
    void handleUpdateReply(QNetworkReply *reply);
    void downloadAndInstallUpdate(const QString &url);
    void onInstallClicked();
    void onUninstallClicked();
    void handleProcessOutput();
    void handleProcessError();
    void handleProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onShowLogsToggled(bool checked);
    void onRestartClicked();
    void onQuitClicked();
    void onItemChanged(QListWidgetItem *item);

private:
    QLabel *updateIcon;
    void applyDarkTheme(bool enabled);
    void loadSettings();
    void saveSettings();
    QNetworkReply *updateDownloadReply;
    QString pendingUpdateVersion;
    QSettings settings;
    QNetworkAccessManager *networkManager;
    bool autoUpdateEnabled;
    void updateStepIndicator(int currentStep);
    bool isCustomAppInstalled(const QString& appName, const QString& executablePath);
    void loadApps();
    void startNextInstall();
    void startNextUninstall();
    bool isAppInstalledWinget(const QString& wingetId);
    void updateItemText(AppStatus& app);
    void appendLog(const QString& text);
    void updateButtons();
    void updateSummary();
    QString extractWingetId(const QString& installCommand);

    Ui::MainWindow *ui;
    QVector<AppStatus> apps;
    QProcess *process;
    int currentAppIndex;
    bool uninstalling; // false = installing, true = uninstalling
};

#endif // MAINWINDOW_H
