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
    void onProgress(const sim::Progress &p);
    void onFinished(const QString &error);
    void updateControls();

private:
    sim::Scenario scenario() const;
    void log(const QString &text);
    void loadSettings();
    void saveSettings() const;

    Ui::MainWindow *ui;
    QThread *worker_ = nullptr;
    std::atomic<bool> stop_{false};
    bool nameEdited_ = false;
};
#endif // MAINWINDOW_H
