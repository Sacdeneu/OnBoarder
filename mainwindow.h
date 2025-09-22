#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "qlabel.h"
#include "qpushbutton.h"
#include <QMainWindow>
#include <QJsonArray>
#include <QProcess>
#include <QVector>
#include <QListWidgetItem>
#include <QSettings>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QMouseEvent>
#include <QHBoxLayout>
#include <QWidget>

class ClickableItemWidget : public QWidget {
    Q_OBJECT
public:
    ClickableItemWidget(QListWidgetItem* item, QWidget* parent = nullptr)
        : QWidget(parent), listItem(item) {
        setFixedHeight(40);
    }

signals:
    void itemClicked();

protected:
    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton) {
            // Basculer l'état de la checkbox
            Qt::CheckState currentState = listItem->checkState();
            listItem->setCheckState(currentState == Qt::Checked ? Qt::Unchecked : Qt::Checked);
            emit itemClicked();
        }
        QWidget::mousePressEvent(event);
    }

private:
    QListWidgetItem* listItem;
};

class ConfigButton : public QPushButton {
    Q_OBJECT
public:
    ConfigButton(QWidget* parent = nullptr) : QPushButton(parent) {
        setFixedSize(20, 20);
        setIcon(QIcon(":/icons/settings.svg"));
        setIconSize(QSize(16, 16));
        setFlat(true);
        setStyleSheet("QPushButton { border: none; background: transparent; padding: 2px; }");
        setCursor(Qt::PointingHandCursor);
        setToolTip("Configurer les modules Visual Studio");

        // Créer l'icône gris très discret par défaut
        createGrayIcon();
    }

protected:
    void enterEvent(QEnterEvent* event) override {
        // Gris plus visible au hover
        createDarkGrayIcon();
        QPushButton::enterEvent(event);
    }

    void leaveEvent(QEvent* event) override {
        // Revenir au gris très discret
        createGrayIcon();
        QPushButton::leaveEvent(event);
    }

private:
    void createGrayIcon() {
        QPixmap originalPixmap = QIcon(":/icons/settings.svg").pixmap(16, 16);
        QPixmap grayPixmap(originalPixmap.size());
        grayPixmap.fill(QColor("#888888")); // Gris très discret
        grayPixmap.setMask(originalPixmap.createMaskFromColor(Qt::transparent));
        setIcon(QIcon(grayPixmap));
    }

    void createDarkGrayIcon() {
        QPixmap originalPixmap = QIcon(":/icons/settings.svg").pixmap(16, 16);
        QPixmap darkGrayPixmap(originalPixmap.size());
        darkGrayPixmap.fill(QColor("#CCCCCC")); // Gris plus visible au hover
        darkGrayPixmap.setMask(originalPixmap.createMaskFromColor(Qt::transparent));
        setIcon(QIcon(darkGrayPixmap));
    }
};

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
    bool hasCustomConfig = false;
    QString customConfigData;
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
    void onBackToMainClicked();
    void onVSConfigOkClicked();
    void onVSConfigCancelClicked();
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
    void hideStepIndicator(bool hide);
    void addConfigButtonToItem(AppStatus& app);
    void updateStatusLabel(QLabel *statusLabel, const AppStatus &app);
    QString generateVSInstallCommand();
    void showVSConfigDialog(int appIndex);
    QNetworkReply *updateDownloadReply;
    QString pendingUpdateVersion;
    QSettings settings;
    QNetworkAccessManager *networkManager;
    bool autoUpdateEnabled;
    int currentConfigAppIndex = -1;
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
    bool uninstalling;
};

#endif // MAINWINDOW_H
