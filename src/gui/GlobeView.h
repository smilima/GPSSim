#pragma once

#include "sim/Gps.h"

#include <QImage>
#include <QMatrix4x4>
#include <QOpenGLBuffer>
#include <QOpenGLFunctions>
#include <QOpenGLWidget>
#include <QPointF>
#include <QVector3D>

#include <memory>
#include <vector>

class QOpenGLShaderProgram;
class QOpenGLTexture;

// The Earth, the receiver and the satellites in 3D, in the Earth-fixed frame of the moment shown. Dragging with the
// left button turns the globe, dragging with the right tilts the view, and the wheel zooms. In edit mode, dragging the
// receiver's red dot moves the receiver instead.
class GlobeView : public QOpenGLWidget, protected QOpenGLFunctions
{
    Q_OBJECT

public:
    explicit GlobeView(QWidget* parent = nullptr);
    ~GlobeView() override;

    // The surface: an equirectangular image, north up, from 180° W on the left. Without one the Earth is plain.
    void setEarthImage(const QImage& image);
    bool hasEarthImage() const { return textured_ || !pendingImage_.isNull(); }
    // The satellites, indexed by PRN - 1 with PRN 0 where there is none, and the elevation they are simulated above
    void setSatellites(const std::vector<sim::Ephemeris>& sats, double elevationMask);
    // GPS time of week (it may run past the end of the week), and where the receiver is then (ECEF)
    void setMoment(double tow, const sim::Vec3& receiver);
    void setShowOrbits(bool on);
    void setShowSightLines(bool on);
    void setShowGrid(bool on);
    // Turns the globe to put the receiver in the middle
    void lookAtReceiver();
    // In edit mode the receiver's dot can be dragged. The view does not move it: it tells where to with receiverDragged,
    // and the receiver moves when setMoment says so.
    void setEditing(bool on);

signals:
    // The receiver's dot was dragged over this geodetic latitude and longitude, in degrees
    void receiverDragged(double latDeg, double lonDeg);

protected:
    void initializeGL() override;
    void paintGL() override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    struct Sat
    {
        int prn = 0;
        size_t index = 0;          // into ephemerides_
        QVector3D pos;             // Earth radii, ECEF
        double el = 0;             // degrees, from the receiver
    };
    struct Lines
    {
        GLenum mode;
        int first, count;
        QColor color;
    };

    void rebuild();                // the satellites and the lines, for the moment shown
    void uploadImage();
    QVector3D target() const;      // the ground in the middle of the view
    QVector3D eye() const;
    QVector3D up() const;          // up on the screen
    double eyeHeight() const;      // above the ground, in Earth radii
    QMatrix4x4 viewProjection() const;
    bool pick(const QPointF& pos, double* lat, double* lon) const;
    bool pickGround(const QPointF& pos, sim::Vec3* ecef) const;
    bool behindEarth(const QVector3D& p) const;
    bool toScreen(const QMatrix4x4& vp, const QVector3D& p, QPointF* at) const;
    bool receiverOnScreen(const QMatrix4x4& vp, QPointF* at) const;
    bool overReceiver(const QPointF& pos) const;
    void setIdleCursor(const QPointF& pos);
    void drawOverlay(QPainter& p, const QMatrix4x4& vp);

    // What is shown
    std::vector<sim::Ephemeris> ephemerides_;
    double mask_ = 5, tow_ = 0;
    sim::Vec3 receiver_{sim::kWgsA, 0, 0};
    bool showOrbits_ = true, showSight_ = true, showGrid_ = false;
    std::vector<Sat> sats_;
    std::vector<QVector3D> lineVertices_;
    std::vector<Lines> lines_;
    bool linesChanged_ = true;
    QImage pendingImage_;          // waiting for the context
    bool textured_ = false;

    // The eye looks at the ground at geocentric latitude camLat_ and longitude camLon_ (radians) from range_ Earth radii
    // away: straight down, or tilted tilt_ (radians) from there towards the northern horizon
    double camLat_ = 0, camLon_ = 0, range_ = 10, tilt_ = 0;
    QPointF lastMouse_;
    Qt::MouseButton dragButton_ = Qt::NoButton;     // the left turns the globe, the right tilts the view
    bool editing_ = false;
    bool movingReceiver_ = false;                   // the left drags the receiver's dot, in edit mode

    std::unique_ptr<QOpenGLShaderProgram> earthProgram_, airProgram_, lineProgram_;
    std::unique_ptr<QOpenGLTexture> texture_;
    QOpenGLBuffer sphere_{QOpenGLBuffer::VertexBuffer}, sphereIndex_{QOpenGLBuffer::IndexBuffer};
    QOpenGLBuffer lineBuffer_{QOpenGLBuffer::VertexBuffer};
    int sphereIndexCount_ = 0;
    QString glError_;              // why nothing can be drawn
};
