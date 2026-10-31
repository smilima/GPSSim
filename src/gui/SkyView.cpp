#include "SkyView.h"

#include <QPainter>

#include <algorithm>
#include <cmath>

SkyView::SkyView(QWidget* parent) : QWidget(parent)
{
    setMinimumSize(220, 220);
}

void SkyView::setSatellites(const std::vector<sim::SatStatus>& sats)
{
    sats_ = sats;
    update();
}

void SkyView::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QPointF c(width() / 2.0, height() / 2.0);
    const double radius = std::min(width(), height()) / 2.0 - 18;

    p.setPen(palette().color(QPalette::Mid));
    for (int el : {0, 30, 60}) {
        const double r = radius * (90 - el) / 90.0;
        p.drawEllipse(c, r, r);
    }
    p.drawLine(c + QPointF(-radius, 0), c + QPointF(radius, 0));
    p.drawLine(c + QPointF(0, -radius), c + QPointF(0, radius));
    p.setPen(palette().color(QPalette::WindowText));
    p.drawText(QRectF(c.x() - 10, c.y() - radius - 17, 20, 16), Qt::AlignCenter, QStringLiteral("N"));

    for (const sim::SatStatus& s : sats_) {
        const double r = radius * (90 - s.el) / 90.0, az = s.az * sim::kPi / 180.0;
        const QPointF at = c + QPointF(r * std::sin(az), -r * std::cos(az));
        p.setPen(Qt::NoPen);
        p.setBrush(palette().color(QPalette::Highlight));
        p.drawEllipse(at, 11, 11);
        p.setPen(palette().color(QPalette::HighlightedText));
        p.drawText(QRectF(at.x() - 11, at.y() - 11, 22, 22), Qt::AlignCenter, QString::number(s.prn));
    }
}
