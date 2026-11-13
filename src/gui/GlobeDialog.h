#pragma once

#include "sim/Gps.h"

#include <QDialog>

#include <vector>

class BorderStore;
class QNetworkAccessManager;

QT_BEGIN_NAMESPACE
namespace Ui {
class GlobeDialog;
}
QT_END_NAMESPACE

// A window with the scenario on a 3D globe: the Earth, the receiver, and the satellites with their orbits. The Earth's
// imagery is fetched from NASA the first time, and kept. In edit mode the receiver can be dragged to a new place.
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
    // Whether Edit may be pressed: not while a run goes on. Turning it off leaves edit mode.
    void setEditable(bool on);

signals:
    // In edit mode the receiver's dot was dragged over this geodetic latitude and longitude, in degrees
    void receiverMoved(double latDeg, double lonDeg);

protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    void loadImagery();
    void loadBorders();
    void saveSettings() const;
    void showHint();               // how to move around, or in edit mode how to move the receiver

    Ui::GlobeDialog* ui;
    QNetworkAccessManager* net_ = nullptr;
    bool fetching_ = false;
    QString viewHint_;
    QString imageryNote_, bordersNote_;    // on its way, or why it could not be had
    BorderStore* borders_ = nullptr;
};
