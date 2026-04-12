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
#include <QTimer>
#include <QSet>
#include <QMap>
#include <QPainter>
#include <QPen>

// ─── Spinning circle loader ───────────────────────────────────────────────────
class SpinnerWidget : public QWidget {
    Q_OBJECT
public:
    SpinnerWidget(QWidget* parent = nullptr) : QWidget(parent), angle(0) {
        setFixedSize(64, 64);
        timer = new QTimer(this);
        connect(timer, &QTimer::timeout, this, [this]() {
            angle = (angle + 18) % 360;
            update();
        });
    }
    void start() { angle = 0; timer->start(30); }
    void stop()  { timer->stop(); }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        QRect rect(6, 6, width() - 12, height() - 12);
        // Background ring
        p.setPen(QPen(QColor("#27272a"), 5, Qt::SolidLine, Qt::RoundCap));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(rect);
        // Spinning arc
        p.setPen(QPen(QColor("#10b981"), 5, Qt::SolidLine, Qt::RoundCap));
        p.drawArc(rect, (90 - angle) * 16, 100 * 16);
    }

private:
    QTimer* timer;
    int angle;
};

// ─── Category header widget ───────────────────────────────────────────────────
class CategoryHeader : public QWidget {
    Q_OBJECT
public:
    CategoryHeader(const QString& name, QWidget* parent = nullptr)
        : QWidget(parent) {
        setFixedHeight(36);
        setCursor(Qt::PointingHandCursor);

        QHBoxLayout* layout = new QHBoxLayout(this);
        layout->setContentsMargins(12, 4, 12, 4);
        layout->setSpacing(8);

        arrowLabel = new QLabel("▼", this);
        arrowLabel->setFixedWidth(14);
        arrowLabel->setStyleSheet("color: #6b7280; font-size: 10px;");

        nameLabel = new QLabel(name.toUpper(), this);
        nameLabel->setStyleSheet(
            "font-weight: bold; font-size: 10px; letter-spacing: 1px; color: #6b7280;");

        layout->addWidget(arrowLabel);
        layout->addWidget(nameLabel);
        layout->addStretch();

        setStyleSheet("background: transparent; border: none;");
    }

    void setExpanded(bool expanded) {
        arrowLabel->setText(expanded ? "▼" : "▶");
    }

signals:
    void toggled();

protected:
    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton)
            emit toggled();
        QWidget::mousePressEvent(event);
    }

private:
    QLabel* arrowLabel;
    QLabel* nameLabel;
};

// ─── Clickable item widget (VS row) ──────────────────────────────────────────
class ClickableItemWidget : public QWidget {
    Q_OBJECT
public:
    ClickableItemWidget(QListWidgetItem* item, QWidget* parent = nullptr)
        : QWidget(parent), listItem(item) {
        setFixedHeight(50);
    }

signals:
    void itemClicked();

protected:
    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton) {
            Qt::CheckState currentState = listItem->checkState();
            listItem->setCheckState(currentState == Qt::Checked ? Qt::Unchecked : Qt::Checked);
            emit itemClicked();
        }
        QWidget::mousePressEvent(event);
    }

private:
    QListWidgetItem* listItem;
};

// ─── Config button (gear icon) ───────────────────────────────────────────────
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
        createGrayIcon();
    }

protected:
    void enterEvent(QEnterEvent* event) override {
        createDarkGrayIcon();
        QPushButton::enterEvent(event);
    }

    void leaveEvent(QEvent* event) override {
        createGrayIcon();
        QPushButton::leaveEvent(event);
    }

private:
    void createGrayIcon() {
        QPixmap originalPixmap = QIcon(":/icons/settings.svg").pixmap(16, 16);
        QPixmap grayPixmap(originalPixmap.size());
        grayPixmap.fill(QColor("#888888"));
        grayPixmap.setMask(originalPixmap.createMaskFromColor(Qt::transparent));
        setIcon(QIcon(grayPixmap));
    }

    void createDarkGrayIcon() {
        QPixmap originalPixmap = QIcon(":/icons/settings.svg").pixmap(16, 16);
        QPixmap darkGrayPixmap(originalPixmap.size());
        darkGrayPixmap.fill(QColor("#CCCCCC"));
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
    void checkVSInstallerRunning();
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
    void toggleCategory(QListWidgetItem* headerItem);
    void startLoadingApps();
    void finishLoadingApps();

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
    QString extractWingetId(const QString& installCommand);
    QStringList getInstalledWingetIds();
    bool isAppInstalledWinget(const QString &wingetId, const QString &appName = "");
    void updateItemText(AppStatus& app);
    void appendLog(const QString& text);
    void updateButtons();
    void updateSummary();

    // Category tracking
    QMap<QListWidgetItem*, QList<QListWidgetItem*>> categoryItemsMap;
    QSet<QListWidgetItem*> categoryHeaderSet;
    QMap<QListWidgetItem*, bool> categoryExpandedMap;

    // Install-page spinner animation
    QTimer *spinnerTimer;
    int spinnerFrame;

    // Loading-page spinner
    SpinnerWidget *loadingSpinner;
    int loadingPageIndex;

    Ui::MainWindow *ui;
    QVector<AppStatus> apps;
    QString fullWingetOutput;
    QStringList installedWingetIds;
    QProcess *process;
    int currentAppIndex;
    bool uninstalling;
};

#endif // MAINWINDOW_H