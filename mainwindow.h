#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QJsonArray>
#include <QProcess>
#include <QVector>
#include <QListWidgetItem>

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
