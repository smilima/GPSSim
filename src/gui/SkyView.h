#pragma once

#include "sim/Simulator.h"

#include <QWidget>

// Where the simulated satellites are, as seen from the receiver: north up, the zenith in the centre.
class SkyView : public QWidget
{
    Q_OBJECT

public:
    explicit SkyView(QWidget* parent = nullptr);
    void setSatellites(const std::vector<sim::SatStatus>& sats);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    std::vector<sim::SatStatus> sats_;
};
