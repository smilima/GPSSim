#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "src/sim/Simulator.h"

#include <QMainWindow>

#include <atomic>

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
    void onClearAlmanac();
    void onProgress(const sim::Progress &p);
    void onFinished(const QString &error);
    void onRadioChanged();
    void updateControls();

private:
    sim::Scenario scenario() const;
    QString loadAlmanac(const QString &path, bool setStart);
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
};
#endif // MAINWINDOW_H
