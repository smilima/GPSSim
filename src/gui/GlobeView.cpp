#include "GlobeView.h"

#include <QLocale>
#include <QMouseEvent>
#include <QOpenGLShaderProgram>
#include <QOpenGLTexture>
#include <QPainter>
#include <QSurfaceFormat>
#include <QVector4D>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

using namespace sim;

namespace {

constexpr double kDeg = kPi / 180.0;
constexpr float kFov = 45.0f;                          // vertical field of view, degrees
constexpr double kMinAltitude = 200e3 / kWgsA;         // how near the eye comes to the ground, in Earth radii: the imagery
                                                       // is 10 km a pixel, so nearer shows only a blur
constexpr double kMaxAltitude = 15.0;                  // far enough to see every orbit whole
constexpr float kAir = 1.05f;                          // the outer edge of the glow, in Earth radii: thicker than the air, to be seen
constexpr float kAboveGround = 1.0003f;                // the grid floats this far out, about 2 km, clear of the surface
constexpr int kOrbitSteps = 180;
constexpr int kFloatsPerVertex = 8;                    // position, normal, texture coordinates

const QColor kInView(90, 220, 130), kBelowMask(150, 150, 150), kReceiver(235, 60, 50);

QVector3D units(const Vec3& p)
{
    return QVector3D(float(p.x / kWgsA), float(p.y / kWgsA), float(p.z / kWgsA));
}

// How far the ground is from the centre of the Earth, in Earth radii, at geocentric latitude psi
double groundRadius(double psi)
{
    const double b = 1.0 - kWgsF, c = b * std::cos(psi), s = std::sin(psi);
    return b / std::sqrt(c * c + s * s);
}

const char* const kEarthVertex = R"(
attribute vec3 position;
attribute vec3 normal;
attribute vec2 texCoord;
uniform mat4 viewProjection;
varying vec3 vNormal;
varying vec2 vTexCoord;
void main()
{
    vNormal = normal;
    vTexCoord = texCoord;
    gl_Position = viewProjection * vec4(position, 1.0);
}
)";

const char* const kEarthFragment = R"(
uniform sampler2D earth;
uniform float textured;
uniform vec3 light;
varying vec3 vNormal;
varying vec2 vTexCoord;
void main()
{
    vec3 base = mix(vec3(0.12, 0.25, 0.45), texture2D(earth, vTexCoord).rgb, textured);
    float lit = 0.4 + 0.8 * max(dot(normalize(vNormal), light), 0.0);
    gl_FragColor = vec4(min(base * lit, vec3(1.0)), 1.0);
}
)";

const char* const kAirVertex = R"(
attribute vec3 position;
attribute vec3 normal;
uniform mat4 viewProjection;
uniform float scale;
varying vec3 vNormal;
varying vec3 vPosition;
void main()
{
    vNormal = normal;
    vPosition = position * scale;
    gl_Position = viewProjection * vec4(vPosition, 1.0);
}
)";

// Only the far side of the shell shows: brightest just above the Earth's edge, fading out evenly to the shell's own.
// There away runs from 0 to sqrt(1 - 1/scale^2) as the square root of the distance from the edge, so its square
// makes the fade even.
const char* const kAirFragment = R"(
uniform vec3 eye;
uniform float scale;
varying vec3 vNormal;
varying vec3 vPosition;
void main()
{
    float away = max(-dot(normalize(vNormal), normalize(eye - vPosition)), 0.0);
    float t = clamp(away * away / (1.0 - 1.0 / (scale * scale)), 0.0, 1.0);
    gl_FragColor = vec4(0.35, 0.6, 1.0, 0.65 * t * t);
}
)";

const char* const kLineVertex = R"(
attribute vec3 position;
uniform mat4 viewProjection;
void main()
{
    gl_Position = viewProjection * vec4(position, 1.0);
}
)";

const char* const kLineFragment = R"(
uniform vec4 color;
void main()
{
    gl_FragColor = color;
}
)";

} // namespace

GlobeView::GlobeView(QWidget* parent) : QOpenGLWidget(parent)
{
    QSurfaceFormat f = format();
    f.setDepthBufferSize(24);
    f.setSamples(4);
    setFormat(f);
    setCursor(Qt::OpenHandCursor);
    setMinimumSize(300, 240);
}

GlobeView::~GlobeView()
{
    makeCurrent();
    texture_.reset();
    earthProgram_.reset();
    airProgram_.reset();
    lineProgram_.reset();
    sphere_.destroy();
    sphereIndex_.destroy();
    lineBuffer_.destroy();
    doneCurrent();
}

void GlobeView::setEarthImage(const QImage& image)
{
    pendingImage_ = image;
    update();
}

void GlobeView::setSatellites(const std::vector<Ephemeris>& sats, double elevationMask)
{
    ephemerides_ = sats;
    mask_ = elevationMask;
    rebuild();
}

void GlobeView::setMoment(double tow, const Vec3& receiver)
{
    tow_ = tow;
    receiver_ = receiver;
    rebuild();
}

void GlobeView::setShowOrbits(bool on)
{
    showOrbits_ = on;
    rebuild();
}

void GlobeView::setShowSightLines(bool on)
{
    showSight_ = on;
    rebuild();
}

void GlobeView::setShowGrid(bool on)
{
    showGrid_ = on;
    rebuild();
}

void GlobeView::lookAtReceiver()
{
    camLat_ = std::atan2(receiver_.z, std::hypot(receiver_.x, receiver_.y));
    camLon_ = std::atan2(receiver_.y, receiver_.x);
    update();
}

void GlobeView::rebuild()
{
    double rxLat = 0, rxLon = 0, rxH = 0;
    ecefToLlh(receiver_, &rxLat, &rxLon, &rxH);
    sats_.clear();
    for (size_t i = 0; i < ephemerides_.size(); ++i) {
        const Ephemeris& e = ephemerides_[i];
        if (e.prn == 0) continue;
        const Vec3 p = satPosition(e, tow_);
        double az = 0, el = 0;
        azEl(rxLat, rxLon, receiver_, p, &az, &el);
        sats_.push_back({e.prn, i, units(p), el / kDeg});
    }

    lineVertices_.clear();
    lines_.clear();
    const auto add = [this](GLenum mode, const QColor& color, const std::vector<QVector3D>& points) {
        lines_.push_back({mode, int(lineVertices_.size()), int(points.size()), color});
        lineVertices_.insert(lineVertices_.end(), points.begin(), points.end());
    };
    std::vector<QVector3D> points;
    if (showGrid_) {
        const QColor grid(255, 255, 255, 60);
        for (int lat = -75; lat <= 75; lat += 15) {
            points.clear();
            for (int lon = -180; lon <= 180; ++lon) points.push_back(kAboveGround * units(llhToEcef(lat * kDeg, lon * kDeg, 0)));
            add(GL_LINE_STRIP, grid, points);
        }
        for (int lon = -180; lon < 180; lon += 15) {
            points.clear();
            for (int lat = -90; lat <= 90; ++lat) points.push_back(kAboveGround * units(llhToEcef(lat * kDeg, lon * kDeg, 0)));
            add(GL_LINE_STRIP, grid, points);
        }
    }
    if (showOrbits_) {
        // Each orbit as it lies in space now: a point of it dt from now is turned back by the Earth's rotation in dt
        for (const Sat& s : sats_) {
            const Ephemeris& e = ephemerides_[s.index];
            const double a = e.sqrtA * e.sqrtA, period = 2.0 * kPi * std::sqrt(a * a * a / kMu);
            points.clear();
            for (int k = 0; k <= kOrbitSteps; ++k) {
                const double dt = period * k / kOrbitSteps, w = kOmegaE * dt, c = std::cos(w), sn = std::sin(w);
                const Vec3 q = satPosition(e, tow_ + dt);
                points.push_back(units({q.x * c - q.y * sn, q.x * sn + q.y * c, q.z}));
            }
            add(GL_LINE_STRIP, s.el >= mask_ ? QColor(120, 200, 255, 150) : QColor(120, 160, 220, 60), points);
        }
    }
    if (showSight_) {
        points.clear();
        for (const Sat& s : sats_)
            if (s.el >= mask_) points.insert(points.end(), {units(receiver_), s.pos});
        add(GL_LINES, QColor(kInView.red(), kInView.green(), kInView.blue(), 200), points);
    }
    linesChanged_ = true;
    update();
}

void GlobeView::initializeGL()
{
    initializeOpenGLFunctions();
    const auto build = [this](std::unique_ptr<QOpenGLShaderProgram>* program, const char* vertex, const char* fragment) {
        *program = std::make_unique<QOpenGLShaderProgram>();
        if ((*program)->addShaderFromSourceCode(QOpenGLShader::Vertex, vertex)
            && (*program)->addShaderFromSourceCode(QOpenGLShader::Fragment, fragment) && (*program)->link())
            return true;
        glError_ = tr("OpenGL could not build the globe: %1").arg((*program)->log());
        return false;
    };
    if (!build(&earthProgram_, kEarthVertex, kEarthFragment) || !build(&airProgram_, kAirVertex, kAirFragment)
        || !build(&lineProgram_, kLineVertex, kLineFragment))
        return;

    // The WGS-84 ellipsoid, a vertex every degree. Rows run from the south pole; the image's top row is the north.
    std::vector<float> v;
    v.reserve(181 * 361 * kFloatsPerVertex);
    for (int i = 0; i <= 180; ++i) {
        const double lat = (i - 90) * kDeg;
        for (int j = 0; j <= 360; ++j) {
            const double lon = (j - 180) * kDeg;
            const QVector3D p = units(llhToEcef(lat, lon, 0));
            v.insert(v.end(), {p.x(), p.y(), p.z(), float(std::cos(lat) * std::cos(lon)), float(std::cos(lat) * std::sin(lon)),
                               float(std::sin(lat)), float(j / 360.0), float((180 - i) / 180.0)});
        }
    }
    std::vector<GLushort> index;
    index.reserve(180 * 360 * 6);
    for (int i = 0; i < 180; ++i)
        for (int j = 0; j < 360; ++j) {
            const GLushort a = GLushort(i * 361 + j), b = GLushort(a + 1), c = GLushort(a + 361), d = GLushort(c + 1);
            index.insert(index.end(), {a, b, c, b, d, c});
        }
    sphereIndexCount_ = int(index.size());
    sphere_.create();
    sphere_.bind();
    sphere_.allocate(v.data(), int(v.size() * sizeof(float)));
    sphere_.release();
    sphereIndex_.create();
    sphereIndex_.bind();
    sphereIndex_.allocate(index.data(), int(index.size() * sizeof(GLushort)));
    sphereIndex_.release();
    lineBuffer_.create();
    lineBuffer_.setUsagePattern(QOpenGLBuffer::DynamicDraw);
    linesChanged_ = true;
}

void GlobeView::uploadImage()
{
    QImage image = pendingImage_.convertToFormat(QImage::Format_RGBA8888);
    pendingImage_ = QImage();
    GLint most = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &most);
    if (most > 0 && (image.width() > most || image.height() > most))
        image = image.scaled(std::min(image.width(), int(most)), std::min(image.height(), int(most)), Qt::IgnoreAspectRatio,
                             Qt::SmoothTransformation);
    // Uploaded row by row as it is, so the image's top row is at texture coordinate 0
    texture_ = std::make_unique<QOpenGLTexture>(QOpenGLTexture::Target2D);
    texture_->setSize(image.width(), image.height());
    texture_->setFormat(QOpenGLTexture::RGBA8_UNorm);
    texture_->setMipLevels(texture_->maximumMipLevels());
    texture_->allocateStorage(QOpenGLTexture::RGBA, QOpenGLTexture::UInt8);
    texture_->setData(QOpenGLTexture::RGBA, QOpenGLTexture::UInt8, image.constBits());
    texture_->generateMipMaps();
    texture_->setMinMagFilters(QOpenGLTexture::LinearMipMapLinear, QOpenGLTexture::Linear);
    texture_->setWrapMode(QOpenGLTexture::DirectionS, QOpenGLTexture::Repeat);
    texture_->setWrapMode(QOpenGLTexture::DirectionT, QOpenGLTexture::ClampToEdge);
    if (QOpenGLTexture::hasFeature(QOpenGLTexture::AnisotropicFiltering)) texture_->setMaximumAnisotropy(8.0f);
    textured_ = true;
}

QVector3D GlobeView::eye() const
{
    return float(groundRadius(camLat_) + altitude_) * QVector3D(float(std::cos(camLat_) * std::cos(camLon_)), float(std::cos(camLat_) * std::sin(camLon_)),
                                        float(std::sin(camLat_)));
}

QVector3D GlobeView::north() const
{
    return QVector3D(float(-std::sin(camLat_) * std::cos(camLon_)), float(-std::sin(camLat_) * std::sin(camLon_)), float(std::cos(camLat_)));
}

QMatrix4x4 GlobeView::viewProjection() const
{
    QMatrix4x4 m;
    m.perspective(kFov, float(width()) / float(std::max(1, height())), float(0.4 * altitude_), float(altitude_ + 7.0));
    m.lookAt(eye(), QVector3D(), north());
    return m;
}

void GlobeView::paintGL()
{
    QPainter painter(this);
    if (!glError_.isEmpty()) {
        painter.fillRect(rect(), Qt::black);
        painter.setPen(Qt::white);
        painter.drawText(rect().adjusted(20, 20, -20, -20), Qt::AlignCenter | Qt::TextWordWrap, glError_);
        return;
    }
    const QMatrix4x4 vp = viewProjection();
    const QVector3D eyeAt = eye(), up = north(), east = QVector3D::crossProduct(up, eyeAt.normalized());

    painter.beginNativePainting();
    glViewport(0, 0, int(width() * devicePixelRatioF()), int(height() * devicePixelRatioF()));
    glClearColor(0.0f, 0.0f, 0.02f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    if (!pendingImage_.isNull()) uploadImage();
    if (linesChanged_) {
        lineBuffer_.bind();
        lineBuffer_.allocate(lineVertices_.data(), int(lineVertices_.size() * sizeof(QVector3D)));
        lineBuffer_.release();
        linesChanged_ = false;
    }

    // The Earth, lit from over the viewer's left shoulder
    const int stride = kFloatsPerVertex * sizeof(float);
    sphere_.bind();
    sphereIndex_.bind();
    earthProgram_->bind();
    earthProgram_->setUniformValue("viewProjection", vp);
    earthProgram_->setUniformValue("light", (eyeAt.normalized() + 0.5f * up - 0.4f * east).normalized());
    earthProgram_->setUniformValue("textured", textured_ ? 1.0f : 0.0f);
    earthProgram_->setUniformValue("earth", 0);
    if (texture_) texture_->bind(0);
    for (const char* name : {"position", "normal", "texCoord"}) earthProgram_->enableAttributeArray(name);
    earthProgram_->setAttributeBuffer("position", GL_FLOAT, 0, 3, stride);
    earthProgram_->setAttributeBuffer("normal", GL_FLOAT, 3 * sizeof(float), 3, stride);
    earthProgram_->setAttributeBuffer("texCoord", GL_FLOAT, 6 * sizeof(float), 2, stride);
    glDrawElements(GL_TRIANGLES, sphereIndexCount_, GL_UNSIGNED_SHORT, nullptr);
    for (const char* name : {"position", "normal", "texCoord"}) earthProgram_->disableAttributeArray(name);
    if (texture_) texture_->release(0);
    earthProgram_->release();

    // Grid, orbits and lines of sight, hidden where the Earth stands in front of them
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    sphereIndex_.release();
    sphere_.release();
    lineBuffer_.bind();
    lineProgram_->bind();
    lineProgram_->setUniformValue("viewProjection", vp);
    lineProgram_->enableAttributeArray("position");
    lineProgram_->setAttributeBuffer("position", GL_FLOAT, 0, 3, sizeof(QVector3D));
    for (const Lines& l : lines_) {
        lineProgram_->setUniformValue("color", l.color);
        glDrawArrays(l.mode, l.first, l.count);
    }
    lineProgram_->disableAttributeArray("position");
    lineProgram_->release();
    lineBuffer_.release();

    // The air, as a glow around the edge of the Earth
    sphere_.bind();
    sphereIndex_.bind();
    airProgram_->bind();
    airProgram_->setUniformValue("viewProjection", vp);
    airProgram_->setUniformValue("scale", kAir);
    airProgram_->setUniformValue("eye", eyeAt);
    airProgram_->enableAttributeArray("position");
    airProgram_->enableAttributeArray("normal");
    airProgram_->setAttributeBuffer("position", GL_FLOAT, 0, 3, stride);
    airProgram_->setAttributeBuffer("normal", GL_FLOAT, 3 * sizeof(float), 3, stride);
    glDrawElements(GL_TRIANGLES, sphereIndexCount_, GL_UNSIGNED_SHORT, nullptr);
    airProgram_->disableAttributeArray("position");
    airProgram_->disableAttributeArray("normal");
    airProgram_->release();
    sphereIndex_.release();
    sphere_.release();

    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    painter.endNativePainting();
    drawOverlay(painter, vp);
}

// Where p shows in the widget; false when it is behind the camera
bool GlobeView::toScreen(const QMatrix4x4& vp, const QVector3D& p, QPointF* at) const
{
    const QVector4D c = vp * QVector4D(p, 1.0f);
    if (c.w() <= 0) return false;
    *at = QPointF((c.x() / c.w() + 1.0) * 0.5 * width(), (1.0 - c.y() / c.w()) * 0.5 * height());
    return true;
}

// Whether the Earth stands between the camera and p
bool GlobeView::behindEarth(const QVector3D& p) const
{
    const QVector3D e = eye(), d = p - e;
    const float r = 0.995f;      // a little inside the surface, so that what stands on it shows
    const float a = QVector3D::dotProduct(d, d), b = QVector3D::dotProduct(e, d), c = QVector3D::dotProduct(e, e) - r * r;
    const float disc = b * b - a * c;
    if (disc <= 0) return false;
    const float t = (-b - std::sqrt(disc)) / a;
    return t > 0 && t < 1;
}

void GlobeView::drawOverlay(QPainter& p, const QMatrix4x4& vp)
{
    p.setRenderHint(QPainter::Antialiasing);
    QFont small = font();
    small.setPointSizeF(font().pointSizeF() * 0.9);
    QFont bold = small;
    bold.setBold(true);
    const auto shadowed = [&p](const QPointF& at, const QString& text, const QColor& color) {
        p.setPen(QColor(0, 0, 0, 200));
        p.drawText(at + QPointF(1, 1), text);
        p.setPen(color);
        p.drawText(at, text);
    };

    // The receiver, unless it is over the horizon
    QPointF at;
    const QVector3D rx = units(receiver_);
    if (QVector3D::dotProduct(rx.normalized(), eye() - rx) > 0 && toScreen(vp, rx, &at)) {
        p.setPen(QPen(Qt::white, 1.5));
        p.setBrush(kReceiver);
        p.drawEllipse(at, 6, 6);
        p.setFont(bold);
        shadowed(at + QPointF(10, 4), tr("Receiver"), Qt::white);
    }

    // The satellites in front of the Earth, the nearest last
    std::vector<const Sat*> shown;
    for (const Sat& s : sats_)
        if (!behindEarth(s.pos)) shown.push_back(&s);
    const QVector3D e = eye();
    std::sort(shown.begin(), shown.end(), [&e](const Sat* a, const Sat* b) { return (a->pos - e).length() > (b->pos - e).length(); });
    p.setFont(bold);
    for (const Sat* s : shown) {
        if (!toScreen(vp, s->pos, &at)) continue;
        const bool inView = s->el >= mask_;
        p.setPen(QPen(QColor(0, 0, 0, 160), 1));
        p.setBrush(inView ? kInView : kBelowMask);
        p.drawEllipse(at, 10, 10);
        p.setPen(Qt::black);
        p.drawText(QRectF(at.x() - 10, at.y() - 10, 20, 20), Qt::AlignCenter, QString::number(s->prn));
    }

    // Key, height of the eye and credit
    p.setFont(small);
    const int line = p.fontMetrics().height();
    const QString keys[3] = {tr("In view: above the elevation mask"), tr("Below the mask"), tr("Receiver")};
    const QColor colors[3] = {kInView, kBelowMask, kReceiver};
    for (int i = 0; i < 3; ++i) {
        const QPointF dot(14, 14 + i * (line + 4));
        p.setPen(Qt::NoPen);
        p.setBrush(colors[i]);
        p.drawEllipse(dot, 5, 5);
        shadowed(dot + QPointF(11, line / 2.0 - 3), keys[i], QColor(230, 230, 230));
    }
    const double km = altitude_ * kWgsA / 1e3;
    shadowed(QPointF(10, height() - 10), tr("Eye %1 km above the ground").arg(QLocale().toString(km, 'f', km < 100 ? 1 : 0)),
             QColor(230, 230, 230));
    if (textured_) {
        const QString credit = tr("Imagery: NASA Blue Marble, from NASA GIBS");
        shadowed(QPointF(width() - 10 - p.fontMetrics().horizontalAdvance(credit), height() - 10), credit, QColor(200, 200, 200));
    }
}

// Where the line of sight under pos meets the ground, taken as a sphere as high as the ground under the eye. The
// latitude is geocentric, as the eye's is.
bool GlobeView::pick(const QPointF& pos, double* lat, double* lon) const
{
    const float r = float(groundRadius(camLat_));
    const QMatrix4x4 inverse = viewProjection().inverted();
    const float x = float(2.0 * pos.x() / width() - 1.0), y = float(1.0 - 2.0 * pos.y() / height());
    const QVector3D from = inverse.map(QVector3D(x, y, -1.0f)), to = inverse.map(QVector3D(x, y, 1.0f));
    const QVector3D d = (to - from).normalized();
    const float b = QVector3D::dotProduct(from, d), c = QVector3D::dotProduct(from, from) - r * r, disc = b * b - c;
    if (disc < 0) return false;
    const QVector3D hit = from + (-b - std::sqrt(disc)) * d;
    *lat = std::asin(std::clamp(double(hit.z() / hit.length()), -1.0, 1.0));
    *lon = std::atan2(double(hit.y()), double(hit.x()));
    return true;
}

void GlobeView::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) return QOpenGLWidget::mousePressEvent(event);
    dragging_ = true;
    lastMouse_ = event->position();
    setCursor(Qt::ClosedHandCursor);
}

void GlobeView::mouseMoveEvent(QMouseEvent* event)
{
    if (!dragging_) return QOpenGLWidget::mouseMoveEvent(event);
    const QPointF pos = event->position();
    double lat0 = 0, lon0 = 0, lat1 = 0, lon1 = 0;
    if (pick(lastMouse_, &lat0, &lon0) && pick(pos, &lat1, &lon1)) {
        // The ground that was under the pointer follows it
        camLat_ += lat0 - lat1;
        camLon_ += std::remainder(lon0 - lon1, 2.0 * kPi);
    } else {
        // Off the Earth: turn by about the angle the view spans
        const double perPixel = std::min(2.0 * std::tan(kFov / 2.0 * kDeg) * altitude_, 2.5) / std::max(1, height());
        camLon_ -= (pos.x() - lastMouse_.x()) * perPixel;
        camLat_ += (pos.y() - lastMouse_.y()) * perPixel;
    }
    camLat_ = std::clamp(camLat_, -kPi / 2, kPi / 2);
    camLon_ = std::remainder(camLon_, 2.0 * kPi);
    lastMouse_ = pos;
    update();
}

void GlobeView::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) return QOpenGLWidget::mouseReleaseEvent(event);
    dragging_ = false;
    setCursor(Qt::OpenHandCursor);
}

// Each notch of the wheel takes the eye 15 % nearer the ground, or back
void GlobeView::wheelEvent(QWheelEvent* event)
{
    const double steps = event->angleDelta().y() / 120.0;
    if (steps == 0) return QOpenGLWidget::wheelEvent(event);
    altitude_ = std::clamp(altitude_ * std::pow(0.85, steps), kMinAltitude, kMaxAltitude);
    event->accept();
    update();
}
