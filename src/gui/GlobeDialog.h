#pragma once

#include "sim/Gps.h"

#include <QDialog>

#include <vector>

class QNetworkAccessManager;

QT_BEGIN_NAMESPACE
namespace Ui {
class GlobeDialog;
}
QT_END_NAMESPACE

// A window with the scenario on a 3D globe: the Earth, the receiver, and the satellites with their orbits. The Earth's
// imagery is fetched from NASA the first time, and kept.
class GlobeDialog : public QDialog
{
    Q_OBJECT

public:
    explicit GlobeDialog(QWidget* parent = nullptr);
    ~GlobeDialog() override;

    // The satellites, indexed by PRN - 1 with PRN 0 where there is none, and the elevation they are simulated above
    void setSatellites(const std::vector<sim::Ephemeris>& sats, double elevationMask);
    // The moment shown, and where the receiver is then (ECEF)
    void setMoment(int week, double tow, const sim::Vec3& receiver);
    void lookAtReceiver();

protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    void loadImagery();
    void saveSettings() const;

    Ui::GlobeDialog* ui;
    QNetworkAccessManager* net_ = nullptr;
    bool fetching_ = false;
};
