#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "src/sim/Simulator.h"

#include <QMainWindow>

#include <atomic>

class GlobeDialog;
class QNetworkAccessManager;
class QNetworkReply;
class QThread;

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void onStart();
    void onStop();
    void onBrowse();
    void onLoadAlmanac();
    void onFetchAlmanac();
    void onAlmanacFetched();
    void onClearAlmanac();
    void onStartDateChanged(const QDateTime &utc);
    void showStartDate();
    void onLocationEdited();
    void showLocation();
    void onShowGlobe();
    void onReceiverMoved(double latDeg, double lonDeg);
    void clearTrack();
    void onProgress(const sim::Progress &p);
    void onFinished(const QString &error);
    void onRadioChanged();
    void updateControls();

private:
    sim::Scenario scenario() const;
    QString loadAlmanac(const QString &path, bool setStart);
    void updateGlobe(bool turnToReceiver);
    void showReceiverAt(const sim::Vec3 &ecef);
    void restoreStart();
    void showRadio();
    void log(const QString &text);
    void loadSettings();
    void saveSettings() const;

    Ui::MainWindow *ui;
    QThread *worker_ = nullptr;
    std::atomic<bool> stop_{false};
    bool nameEdited_ = false;
    bool transmitConfirmed_ = false;
    int radio_ = 0;                      // the one the device field shows
    QString devices_[3];                 // each radio keeps its own address
    std::vector<sim::AlmanacEntry> almanac_;
    QNetworkAccessManager *net_ = nullptr;
    QNetworkReply *fetch_ = nullptr;     // the download of the current almanac, while it runs
    GlobeDialog *globe_ = nullptr;       // made the first time it is opened
    double startLat_ = 0, startLon_ = 0, startAlt_ = 0;      // the scenario's start, while a run shows where the receiver is
    std::vector<sim::Vec3> track_;       // where the receiver has been in the last run (ECEF)
};
#endif // MAINWINDOW_H
