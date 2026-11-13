#include "GlobeView.h"

#include <QCursor>
#include <QFontMetrics>
#include <QLineF>
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
constexpr double kMinRange = 5e3 / kWgsA;              // how near the eye comes to the ground it looks at, in Earth radii:
                                                       // Landsat's 31 m pixels are then a few on the screen
constexpr double kMaxRange = 15.0;                     // far enough to see every orbit whole
constexpr double kMaxTilt = 85.0 * kDeg;               // short of the horizon, so the eye stays above the ground
constexpr double kTiltPerPixel = 0.3 * kDeg;
constexpr float kAir = 1.05f;                          // the outer edge of the glow, in Earth radii: thicker than the air, to be seen
constexpr float kAboveGround = 1.0003f;                // the grid floats this far out, about 2 km, clear of the surface
constexpr int kOrbitSteps = 180;
constexpr int kFloatsPerVertex = 8;                    // position, normal, texture coordinates
constexpr int kGridSteps = 16;                         // a patch of ground is a grid of 16 x 16 squares
constexpr int kTilePixels = 512;
constexpr double kPixelsPerTexel = 1.2;                // a tile is split once its pixels show larger than this
constexpr size_t kMaxTextures = 192;                   // tiles kept on the graphics card, about 1.3 MB each
constexpr int kMaxPatches = 400;
constexpr int kUploadsPerFrame = 8;

const QColor kInView(90, 220, 130), kBelowMask(150, 150, 150), kReceiver(235, 60, 50);

QVector3D units(const Vec3& p)
{
    return QVector3D(float(p.x / kWgsA), float(p.y / kWgsA), float(p.z / kWgsA));
}

// Up from the ground at geocentric latitude lat and longitude lon, and north along it
QVector3D upAt(double lat, double lon)
{
    return QVector3D(float(std::cos(lat) * std::cos(lon)), float(std::cos(lat) * std::sin(lon)), float(std::sin(lat)));
}

QVector3D northAt(double lat, double lon)
{
    return QVector3D(float(-std::sin(lat) * std::cos(lon)), float(-std::sin(lat) * std::sin(lon)), float(std::cos(lat)));
}

// How far the ground is from the centre of the Earth, in Earth radii, at geocentric latitude psi
double groundRadius(double psi)
{
    const double b = 1.0 - kWgsF, c = b * std::cos(psi), s = std::sin(psi);
    return b / std::sqrt(c * c + s * s);
}

// A patch of the WGS-84 ellipsoid between two meridians and two parallels. grid runs east and south across it from 0 to
// 1; its third value is 1 on the skirt, a wall hanging under the edge that hides the cracks between patches drawn at
// different sharpness.
const char* const kTileVertex = R"(
attribute vec3 grid;
uniform mat4 viewProjection;
uniform vec4 bounds;
uniform vec4 underRect;
uniform vec4 overRect;
uniform float skirt;
uniform float e2;
varying vec3 vNormal;
varying vec2 vUnder;
varying vec2 vOver;
void main()
{
    float lon = bounds.x + grid.x * bounds.z;
    float lat = bounds.y - grid.y * bounds.w;
    float s = sin(lat);
    float c = cos(lat);
    float n = 1.0 / sqrt(1.0 - e2 * s * s);
    vNormal = vec3(c * cos(lon), c * sin(lon), s);
    vUnder = underRect.xy + grid.xy * underRect.zw;
    vOver = overRect.xy + grid.xy * overRect.zw;
    vec3 p = vec3(n * c * cos(lon), n * c * sin(lon), n * (1.0 - e2) * s) - grid.z * skirt * vNormal;
    gl_Position = viewProjection * vec4(p, 1.0);
}
)";

// Landsat over Blue Marble, where Landsat has data: it is black where it has none
const char* const kTileFragment = R"(
uniform sampler2D under;
uniform sampler2D over;
uniform float hasUnder;
uniform float hasOver;
uniform vec3 light;
varying vec3 vNormal;
varying vec2 vUnder;
varying vec2 vOver;
void main()
{
    vec3 color = mix(vec3(0.12, 0.25, 0.45), texture2D(under, vUnder).rgb, hasUnder);
    vec3 o = texture2D(over, vOver).rgb;
    color = mix(color, o, hasOver * smoothstep(0.015, 0.05, max(o.r, max(o.g, o.b))));
    float lit = 0.4 + 0.8 * max(dot(normalize(vNormal), light), 0.0);
    gl_FragColor = vec4(min(color * lit, vec3(1.0)), 1.0);
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

// lift raises lines on the ground just clear of it, so the ground never hides them
const char* const kLineVertex = R"(
attribute vec3 position;
uniform mat4 viewProjection;
uniform float lift;
void main()
{
    gl_Position = viewProjection * vec4(position * (1.0 + lift), 1.0);
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
    setContextMenuPolicy(Qt::PreventContextMenu);      // the right button tilts
    setMinimumSize(300, 240);
}

GlobeView::~GlobeView()
{
    makeCurrent();
    texture_.reset();
    textures_.clear();
    tileProgram_.reset();
    airProgram_.reset();
    lineProgram_.reset();
    sphere_.destroy();
    sphereIndex_.destroy();
    grid_.destroy();
    gridIndex_.destroy();
    lineBuffer_.destroy();
    borderBuffer_.destroy();
    doneCurrent();
}

void GlobeView::setBorders(BorderLines lines)
{
    pendingBorders_ = std::move(lines);
    bordersChanged_ = true;
    update();
}

void GlobeView::setShowBorders(bool on)
{
    showBorders_ = on;
    update();
}

void GlobeView::setEarthImage(const QImage& image)
{
    pendingImage_ = image;
    update();
}

void GlobeView::setTileStore(TileStore* store)
{
    tiles_ = store;
    connect(store, &TileStore::tileReady, this, [this](const TileKey& key, const QImage& image) {
        arrived_.insert(key, image);
        update();
    });
    connect(store, &TileStore::changed, this, qOverload<>(&GlobeView::update));
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

void GlobeView::setTrack(const std::vector<Vec3>& points)
{
    track_.clear();
    track_.reserve(points.size());
    for (const Vec3& p : points) track_.push_back(units(p));
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

void GlobeView::setEditing(bool on)
{
    editing_ = on;
    movingReceiver_ = movingReceiver_ && on;
    setMouseTracking(on);      // to show over the dot that it can be dragged
    setIdleCursor(mapFromGlobal(QCursor::pos()));
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
    if (track_.size() >= 2) add(GL_LINE_STRIP, QColor(kReceiver.red(), kReceiver.green(), kReceiver.blue(), 230), track_);
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
    if (!build(&tileProgram_, kTileVertex, kTileFragment) || !build(&airProgram_, kAirVertex, kAirFragment)
        || !build(&lineProgram_, kLineVertex, kLineFragment))
        return;

    // The grid of a patch, then its skirt: each edge vertex again, hanging
    std::vector<float> g;
    const int side = kGridSteps + 1;
    for (int i = 0; i < side; ++i)
        for (int j = 0; j < side; ++j) g.insert(g.end(), {float(j) / kGridSteps, float(i) / kGridSteps, 0.0f});
    std::vector<GLushort> gi;
    const auto at = [side](int i, int j) { return GLushort(i * side + j); };
    for (int i = 0; i < kGridSteps; ++i)
        for (int j = 0; j < kGridSteps; ++j) gi.insert(gi.end(), {at(i, j), at(i, j + 1), at(i + 1, j), at(i, j + 1), at(i + 1, j + 1), at(i + 1, j)});
    // Around the edge, as a ring of side * 4 - 4 vertices
    std::vector<GLushort> ring;
    for (int j = 0; j < kGridSteps; ++j) ring.push_back(at(0, j));
    for (int i = 0; i < kGridSteps; ++i) ring.push_back(at(i, kGridSteps));
    for (int j = kGridSteps; j > 0; --j) ring.push_back(at(kGridSteps, j));
    for (int i = kGridSteps; i > 0; --i) ring.push_back(at(i, 0));
    const GLushort firstSkirt = GLushort(side * side);
    for (GLushort v : ring) g.insert(g.end(), {g[v * 3], g[v * 3 + 1], 1.0f});
    for (size_t k = 0; k < ring.size(); ++k) {
        const GLushort a = ring[k], b = ring[(k + 1) % ring.size()];
        const GLushort a2 = GLushort(firstSkirt + k), b2 = GLushort(firstSkirt + (k + 1) % ring.size());
        gi.insert(gi.end(), {a, b, a2, b, b2, a2});
    }
    gridIndexCount_ = int(gi.size());
    grid_.create();
    grid_.bind();
    grid_.allocate(g.data(), int(g.size() * sizeof(float)));
    grid_.release();
    gridIndex_.create();
    gridIndex_.bind();
    gridIndex_.allocate(gi.data(), int(gi.size() * sizeof(GLushort)));
    gridIndex_.release();

    // The WGS-84 ellipsoid, a vertex every degree, for the air around it
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
    borderBuffer_.create();
}

// Uploaded row by row as it is, so the image's top row is at texture coordinate 0
std::unique_ptr<QOpenGLTexture> GlobeView::makeTexture(const QImage& rgba)
{
    auto texture = std::make_unique<QOpenGLTexture>(QOpenGLTexture::Target2D);
    texture->setSize(rgba.width(), rgba.height());
    texture->setFormat(QOpenGLTexture::RGBA8_UNorm);
    texture->setMipLevels(texture->maximumMipLevels());
    texture->allocateStorage(QOpenGLTexture::RGBA, QOpenGLTexture::UInt8);
    texture->setData(QOpenGLTexture::RGBA, QOpenGLTexture::UInt8, rgba.constBits());
    texture->generateMipMaps();
    texture->setMinMagFilters(QOpenGLTexture::LinearMipMapLinear, QOpenGLTexture::Linear);
    texture->setWrapMode(QOpenGLTexture::ClampToEdge);
    if (QOpenGLTexture::hasFeature(QOpenGLTexture::AnisotropicFiltering)) texture->setMaximumAnisotropy(8.0f);
    return texture;
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
    texture_ = makeTexture(image);
    textured_ = true;
}

// A few of the tiles that have arrived each frame, so that drawing stays smooth, and the ones not drawn for longest
// dropped from the card
void GlobeView::uploadTiles()
{
    for (int n = 0; n < kUploadsPerFrame && !arrived_.isEmpty(); ++n) {
        const auto first = arrived_.begin();
        textures_[first.key()] = {makeTexture(first.value()), frame_};
        arrived_.erase(first);
    }
    if (!arrived_.isEmpty()) update();
    if (textures_.size() <= kMaxTextures) return;
    std::vector<std::pair<qint64, TileKey>> byUse;
    for (const auto& [key, t] : textures_)
        if (t.usedIn < frame_ - 1) byUse.emplace_back(t.usedIn, key);
    std::sort(byUse.begin(), byUse.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    for (size_t i = 0; i < byUse.size() && textures_.size() > kMaxTextures; ++i) textures_.erase(byUse[i].second);
}

QOpenGLTexture* GlobeView::tileTexture(const TileKey& key)
{
    const auto t = textures_.find(key);
    if (t == textures_.end()) return nullptr;
    t->second.usedIn = frame_;
    return t->second.texture.get();
}

// Draws the tile, or the four of the level below when its pixels would show too large. Tiles over the horizon or out
// of view are left out.
void GlobeView::visit(const TileKey& key, const QMatrix4x4& vp, std::vector<Patch>* patches, std::vector<TileKey>* wanted)
{
    const double span = 288.0 / double(1 << key.level) * kDeg;
    const double west = -kPi + key.col * span, north = kPi / 2 - key.row * span, south = north - span;
    const double midLat = north - span / 2, midLon = west + span / 2;
    const double nearEquator = north > 0 && south < 0 ? 0.0 : std::min(std::fabs(north), std::fabs(south));
    const double reach = 0.5 * std::hypot(span, span * std::cos(nearEquator));      // from the middle to the corners
    const QVector3D middle = units(llhToEcef(midLat, midLon, 0)), e = eye();      // tiles are in geodetic latitude

    // Over the horizon
    const double eyeDistance = e.length(), horizon = std::acos(std::min(1.0, (1.0 - kWgsF) / eyeDistance));
    const double away = std::acos(std::clamp(double(QVector3D::dotProduct(middle.normalized(), e.normalized())), -1.0, 1.0));
    if (away - reach > horizon) return;

    // Out of the view: outside one of its planes
    const float radius = float(2.0 * std::sin(std::min(reach, kPi / 2) / 2.0) + 0.02 * span);
    for (int i = 0; i < 3; ++i)
        for (const float sign : {1.0f, -1.0f}) {
            const QVector4D plane = vp.row(3) + sign * vp.row(i);
            const float length = plane.toVector3D().length();
            if (QVector3D::dotProduct(plane.toVector3D(), middle) + plane.w() < -radius * length) return;
        }

    // Split when its pixels would show larger than a few on the screen
    const double distance = std::max(double((middle - e).length()) - radius, 1e-6);
    const double pixelsPerRadian = height() / (2.0 * std::tan(kFov / 2.0 * kDeg));
    if (key.level < kLastTileLevel && span / kTilePixels / distance * pixelsPerRadian > kPixelsPerTexel
        && int(patches->size()) < kMaxPatches) {
        for (int r = 0; r < 2; ++r)
            for (int c = 0; c < 2; ++c) visit({key.level + 1, key.row * 2 + r, key.col * 2 + c}, vp, patches, wanted);
        return;
    }

    // The sharpest imagery at hand, and what to fetch for it
    Patch p;
    p.key = key;
    const auto rectIn = [&key](const TileKey& outer) {
        const int up = key.level - outer.level;
        const float scale = 1.0f / float(1 << up);
        return QVector4D(float(key.col - (outer.col << up)) * scale, float(key.row - (outer.row << up)) * scale, scale, scale);
    };
    for (int level = std::min(key.level, kLastBlueMarbleLevel); level >= kFirstTileLevel && !p.under; --level) {
        const TileKey outer = key.ancestor(key.level - level);
        if ((p.under = tileTexture(outer))) p.underRect = rectIn(outer);
    }
    if (!p.under && texture_) {
        p.under = texture_.get();
        p.underRect = QVector4D(float((west + kPi) / (2 * kPi)), float((kPi / 2 - north) / kPi), float(span / (2 * kPi)), float(span / kPi));
    }
    for (int level = key.level; level > kLastBlueMarbleLevel && !p.over; --level) {
        const TileKey outer = key.ancestor(key.level - level);
        if ((p.over = tileTexture(outer))) p.overRect = rectIn(outer);
    }
    if (key.level >= kFirstTileLevel) {
        const TileKey blueMarble = key.ancestor(std::max(0, key.level - kLastBlueMarbleLevel));
        if (!tileTexture(blueMarble)) wanted->push_back(blueMarble);
        if (key.level > kLastBlueMarbleLevel && !tileTexture(key)) wanted->push_back(key);
    }
    patches->push_back(p);
}

// The ground, from the patches the view needs, lit from over the viewer's left shoulder
void GlobeView::drawEarth(const QMatrix4x4& vp, const QVector3D& light)
{
    std::vector<Patch> patches;
    std::vector<TileKey> wanted;
    for (int row = 0; row < 5; ++row)
        for (int col = 0; col < 10; ++col) visit({3, row, col}, vp, &patches, &wanted);
    patchCount_ = int(patches.size());
    if (tiles_) {
        // Each once, the coarsest first, so the view fills in quickly and then sharpens
        std::sort(wanted.begin(), wanted.end(), [](const TileKey& a, const TileKey& b) {
            return a.level != b.level ? a.level < b.level : (a.row != b.row ? a.row < b.row : a.col < b.col);
        });
        wanted.erase(std::unique(wanted.begin(), wanted.end()), wanted.end());
        tiles_->want(wanted);
    }

    grid_.bind();
    gridIndex_.bind();
    tileProgram_->bind();
    tileProgram_->setUniformValue("viewProjection", vp);
    tileProgram_->setUniformValue("light", light);
    tileProgram_->setUniformValue("e2", float(kWgsF * (2.0 - kWgsF)));
    tileProgram_->setUniformValue("under", 0);
    tileProgram_->setUniformValue("over", 1);
    tileProgram_->enableAttributeArray("grid");
    tileProgram_->setAttributeBuffer("grid", GL_FLOAT, 0, 3, 3 * sizeof(float));
    for (const Patch& p : patches) {
        const double span = 288.0 / double(1 << p.key.level) * kDeg;
        tileProgram_->setUniformValue("bounds", QVector4D(float(-kPi + p.key.col * span), float(kPi / 2 - p.key.row * span), float(span), float(span)));
        tileProgram_->setUniformValue("skirt", float(0.02 * span));
        tileProgram_->setUniformValue("hasUnder", p.under ? 1.0f : 0.0f);
        tileProgram_->setUniformValue("hasOver", p.over ? 1.0f : 0.0f);
        tileProgram_->setUniformValue("underRect", p.underRect);
        tileProgram_->setUniformValue("overRect", p.overRect);
        if (p.under) p.under->bind(0);
        if (p.over) p.over->bind(1);
        glDrawElements(GL_TRIANGLES, gridIndexCount_, GL_UNSIGNED_SHORT, nullptr);
    }
    tileProgram_->disableAttributeArray("grid");
    tileProgram_->release();
    gridIndex_.release();
    grid_.release();
    for (const GLenum unit : {GL_TEXTURE1, GL_TEXTURE0}) {
        glActiveTexture(unit);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
}

QVector3D GlobeView::target() const
{
    return float(groundRadius(camLat_)) * upAt(camLat_, camLon_);
}

// Above the target, swung back from the north by the tilt
QVector3D GlobeView::eye() const
{
    return target() + float(range_) * (float(std::cos(tilt_)) * upAt(camLat_, camLon_) - float(std::sin(tilt_)) * northAt(camLat_, camLon_));
}

QVector3D GlobeView::up() const
{
    return float(std::sin(tilt_)) * upAt(camLat_, camLon_) + float(std::cos(tilt_)) * northAt(camLat_, camLon_);
}

double GlobeView::eyeHeight() const
{
    const QVector3D e = eye();
    return double(e.length()) - groundRadius(std::asin(std::clamp(double(e.z() / e.length()), -1.0, 1.0)));
}

QMatrix4x4 GlobeView::viewProjection() const
{
    // No ground is nearer than the eye's height, even with the view tilted towards the horizon
    const QVector3D e = eye();
    QMatrix4x4 m;
    m.perspective(kFov, float(width()) / float(std::max(1, height())), float(0.4 * std::max(eyeHeight(), 1e-4)), e.length() + 7.0f);
    m.lookAt(e, target(), up());
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
    const QVector3D eyeAt = eye(), screenUp = up(), east = QVector3D::crossProduct(screenUp, eyeAt.normalized());

    painter.beginNativePainting();
    glViewport(0, 0, int(width() * devicePixelRatioF()), int(height() * devicePixelRatioF()));
    glClearColor(0.0f, 0.0f, 0.02f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    ++frame_;
    if (!pendingImage_.isNull()) uploadImage();
    uploadTiles();
    if (linesChanged_) {
        lineBuffer_.bind();
        lineBuffer_.allocate(lineVertices_.data(), int(lineVertices_.size() * sizeof(QVector3D)));
        lineBuffer_.release();
        linesChanged_ = false;
    }
    if (bordersChanged_) {
        // Countries, then states; kept on the card only
        std::vector<QVector3D>& all = pendingBorders_.countries;
        countryVertices_ = int(all.size());
        all.insert(all.end(), pendingBorders_.states.begin(), pendingBorders_.states.end());
        borderVertices_ = int(all.size());
        borderBuffer_.bind();
        borderBuffer_.allocate(all.data(), int(all.size() * sizeof(QVector3D)));
        borderBuffer_.release();
        pendingBorders_ = BorderLines();
        bordersChanged_ = false;
    }

    drawEarth(vp, (eyeAt.normalized() + 0.5f * screenUp - 0.4f * east).normalized());

    // Grid, orbits and lines of sight, hidden where the Earth stands in front of them
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    lineBuffer_.bind();
    lineProgram_->bind();
    lineProgram_->setUniformValue("viewProjection", vp);
    lineProgram_->setUniformValue("lift", 0.0f);
    lineProgram_->enableAttributeArray("position");
    lineProgram_->setAttributeBuffer("position", GL_FLOAT, 0, 3, sizeof(QVector3D));
    for (const Lines& l : lines_) {
        lineProgram_->setUniformValue("color", l.color);
        glDrawArrays(l.mode, l.first, l.count);
    }
    lineBuffer_.release();
    if (showsBorders()) {
        // States fade out between 3000 and 8000 km up, where they would only crowd the view
        const double km = eyeHeight() * kWgsA / 1e3, states = std::clamp((8000.0 - km) / 5000.0, 0.0, 1.0);
        borderBuffer_.bind();
        lineProgram_->setAttributeBuffer("position", GL_FLOAT, 0, 3, sizeof(QVector3D));
        lineProgram_->setUniformValue("lift", float(std::max(2e-4 * eyeHeight(), 2e-6)));
        if (states > 0) {
            lineProgram_->setUniformValue("color", QColor(255, 255, 255, int(120 * states)));
            glDrawArrays(GL_LINES, countryVertices_, borderVertices_ - countryVertices_);
        }
        lineProgram_->setUniformValue("color", QColor(255, 225, 120, 220));
        glDrawArrays(GL_LINES, 0, countryVertices_);
        borderBuffer_.release();
    }
    lineProgram_->disableAttributeArray("position");
    lineProgram_->release();

    // The air, as a glow around the edge of the Earth
    const int stride = kFloatsPerVertex * sizeof(float);
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

// Where the receiver shows; false when it is over the horizon or behind the camera
bool GlobeView::receiverOnScreen(const QMatrix4x4& vp, QPointF* at) const
{
    const QVector3D rx = units(receiver_);
    return QVector3D::dotProduct(rx.normalized(), eye() - rx) > 0 && toScreen(vp, rx, at);
}

bool GlobeView::overReceiver(const QPointF& pos) const
{
    QPointF at;
    return receiverOnScreen(viewProjection(), &at) && QLineF(at, pos).length() <= 12.0;
}

// An open hand, or in edit mode over the receiver's dot, arrows to move it
void GlobeView::setIdleCursor(const QPointF& pos)
{
    if (dragButton_ != Qt::NoButton) return;
    setCursor(editing_ && overReceiver(pos) ? Qt::SizeAllCursor : Qt::OpenHandCursor);
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

    // The satellites in front of the Earth, the nearest last
    std::vector<std::pair<const Sat*, QPointF>> shown;
    const QVector3D e = eye();
    for (const Sat& s : sats_) {
        QPointF where;
        if (!behindEarth(s.pos) && toScreen(vp, s.pos, &where)) shown.emplace_back(&s, where);
    }
    std::sort(shown.begin(), shown.end(), [&e](const auto& a, const auto& b) { return (a.first->pos - e).length() > (b.first->pos - e).length(); });

    // Cities under everything else, kept clear of the receiver, the satellites, the key and the lines at the bottom
    QPointF at;
    const bool receiverShown = receiverOnScreen(vp, &at);
    if (showsCities()) {
        const int line = QFontMetrics(bold).height();
        std::vector<QRectF> taken = {QRectF(0, 0, 250, 3 * (line + 4) + 12), QRectF(0, height() - line - 14, width(), line + 14)};
        if (receiverShown) taken.push_back(QRectF(at.x() - 12, at.y() - 12, 24 + 10 + QFontMetrics(bold).horizontalAdvance(tr("Receiver")), 24));
        for (const auto& [s, where] : shown) taken.push_back(QRectF(where.x() - 11, where.y() - 11, 22, 22));
        drawCities(p, vp, &taken);
    }

    // The receiver, unless it is over the horizon. In edit mode a ring shows it can be dragged.
    if (receiverShown) {
        if (editing_) {
            p.setPen(QPen(QColor(255, 255, 255, 220), 2));
            p.setBrush(Qt::NoBrush);
            p.drawEllipse(at, 11, 11);
        }
        p.setPen(QPen(Qt::white, 1.5));
        p.setBrush(kReceiver);
        p.drawEllipse(at, 6, 6);
        p.setFont(bold);
        shadowed(at + QPointF(10, 4), tr("Receiver"), Qt::white);
    }

    p.setFont(bold);
    for (const auto& [s, where] : shown) {
        p.setPen(QPen(QColor(0, 0, 0, 160), 1));
        p.setBrush(s->el >= mask_ ? kInView : kBelowMask);
        p.drawEllipse(where, 10, 10);
        p.setPen(Qt::black);
        p.drawText(QRectF(where.x() - 10, where.y() - 10, 20, 20), Qt::AlignCenter, QString::number(s->prn));
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
    const double km = eyeHeight() * kWgsA / 1e3;
    QString state = tr("Eye %1 km above the ground").arg(QLocale().toString(km, 'f', km < 100 ? 1 : 0));
    if (tiles_ && tiles_->offline()) state += tr("  ·  Offline: showing the imagery kept");
    else if (tiles_ && tiles_->fetching() > 0) state += tr("  ·  Fetching %n tile(s) from NASA", nullptr, tiles_->fetching());
    shadowed(QPointF(10, height() - 10), state, QColor(230, 230, 230));
    if (textured_ || !textures_.empty()) {
        QString credit = tr("Imagery: NASA Blue Marble and Landsat (WELD), from NASA GIBS");
        if (showsBorders() && showsCities()) credit += tr("  ·  Borders and cities: Natural Earth");
        else if (showsBorders()) credit += tr("  ·  Borders: Natural Earth");
        else if (showsCities()) credit += tr("  ·  Cities: Natural Earth");
        shadowed(QPointF(width() - 10 - p.fontMetrics().horizontalAdvance(credit), height() - 10), credit, QColor(200, 200, 200));
    }
}

// The cities near enough to name, the most important first, each with its name to the right or else to the left of its
// dot. One whose name would cover something already drawn is left out, dot and all. Natural Earth gives each city the
// web-map zoom from which to show it; the zoom here is that of a web map as sharp as the view is at the city, plus one
// so that the largest cities show from afar.
void GlobeView::drawCities(QPainter& p, const QMatrix4x4& vp, std::vector<QRectF>* taken)
{
    QFont font = this->font();
    font.setPointSizeF(font.pointSizeF() * 0.85);
    QFont capital = font;
    capital.setBold(true);
    const QFontMetricsF plain(font), bold(capital);
    if (nameWidths_.size() != cities_.size()) nameWidths_.assign(cities_.size(), -1.0f);

    const QVector3D e = eye();
    const double metresPerRadian = kWgsA * 2.0 * std::tan(kFov / 2.0 * kDeg) / std::max(1, height());
    const QRectF screen = QRectF(rect()).adjusted(-40, -10, 40, 10);
    struct Candidate
    {
        size_t index;
        QPointF at;
    };
    std::vector<Candidate> nearby;
    for (size_t i = 0; i < cities_.size(); ++i) {
        const City& c = cities_[i];
        const QVector3D toEye = e - c.pos;
        if (QVector3D::dotProduct(c.pos, toEye) <= 0) continue;      // over the horizon
        const double metresPerPixel = toEye.length() * metresPerRadian;
        if (c.minZoom > std::log2(40075016.686 / (256.0 * metresPerPixel)) + 1.0) continue;
        QPointF at;
        if (toScreen(vp, c.pos, &at) && screen.contains(at)) nearby.push_back({i, at});
    }
    std::sort(nearby.begin(), nearby.end(), [this](const Candidate& a, const Candidate& b) {
        const City &x = cities_[a.index], &y = cities_[b.index];
        return x.minZoom != y.minZoom ? x.minZoom < y.minZoom : x.population > y.population;
    });

    const auto clear = [taken](const QRectF& r) {
        return std::none_of(taken->begin(), taken->end(), [&r](const QRectF& t) { return t.intersects(r); });
    };
    int drawn = 0;
    for (const Candidate& c : nearby) {
        if (drawn >= 300) break;
        const City& city = cities_[c.index];
        const bool isCapital = city.kind == City::Capital;
        const QFontMetricsF& m = isCapital ? bold : plain;
        float& width = nameWidths_[c.index];
        if (width < 0) width = float(m.horizontalAdvance(city.name));
        const double r = isCapital ? 3.5 : city.kind == City::StateCapital ? 3.0 : 2.5;
        const QRectF dot(c.at.x() - r - 1, c.at.y() - r - 1, 2 * r + 2, 2 * r + 2);
        const QRectF right(c.at.x() + r + 3, c.at.y() - m.height() / 2, width + 2, m.height());
        const QRectF left(c.at.x() - r - 5 - width, c.at.y() - m.height() / 2, width + 2, m.height());
        const QRectF* label = clear(right) ? &right : clear(left) ? &left : nullptr;
        if (!label || !clear(dot)) continue;
        taken->push_back(dot);
        taken->push_back(*label);
        ++drawn;

        p.setPen(QPen(QColor(0, 0, 0, 200), isCapital ? 1.5 : 1.0));
        p.setBrush(isCapital ? QColor(255, 225, 120) : QColor(245, 245, 245));
        p.drawEllipse(c.at, r, r);
        p.setFont(isCapital ? capital : font);
        const QPointF baseline(label->left(), c.at.y() + m.ascent() / 2 - 1);
        p.setPen(QColor(0, 0, 0, 210));
        p.drawText(baseline + QPointF(1, 1), city.name);
        p.setPen(isCapital ? QColor(255, 235, 160) : QColor(240, 240, 240));
        p.drawText(baseline, city.name);
    }
}

void GlobeView::setCities(std::vector<City> cities)
{
    cities_ = std::move(cities);
    nameWidths_.clear();
    update();
}

void GlobeView::setShowCities(bool on)
{
    showCities_ = on;
    update();
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

// Where the line of sight under pos meets the WGS-84 ellipsoid (ECEF, metres)
bool GlobeView::pickGround(const QPointF& pos, Vec3* ecef) const
{
    const QMatrix4x4 inverse = viewProjection().inverted();
    const float x = float(2.0 * pos.x() / width() - 1.0), y = float(1.0 - 2.0 * pos.y() / height());
    const QVector3D from = inverse.map(QVector3D(x, y, -1.0f)), to = inverse.map(QVector3D(x, y, 1.0f));
    // Squeezed along the axis, the ellipsoid is the unit sphere
    const double b = 1.0 - kWgsF;
    const double o[3] = {from.x(), from.y(), from.z() / b};
    double d[3] = {to.x() - from.x(), to.y() - from.y(), (to.z() - from.z()) / b};
    const double n = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    for (double& v : d) v /= n;
    const double half = o[0] * d[0] + o[1] * d[1] + o[2] * d[2], c = o[0] * o[0] + o[1] * o[1] + o[2] * o[2] - 1.0;
    const double disc = half * half - c;
    if (disc < 0) return false;
    const double t = -half - std::sqrt(disc);
    if (t < 0) return false;
    *ecef = {(o[0] + t * d[0]) * kWgsA, (o[1] + t * d[1]) * kWgsA, (o[2] + t * d[2]) * b * kWgsA};
    return true;
}

void GlobeView::mousePressEvent(QMouseEvent* event)
{
    if (dragButton_ != Qt::NoButton || (event->button() != Qt::LeftButton && event->button() != Qt::RightButton))
        return QOpenGLWidget::mousePressEvent(event);
    dragButton_ = event->button();
    lastMouse_ = event->position();
    movingReceiver_ = editing_ && dragButton_ == Qt::LeftButton && overReceiver(lastMouse_);
    setCursor(movingReceiver_ ? Qt::SizeAllCursor : dragButton_ == Qt::LeftButton ? Qt::ClosedHandCursor : Qt::SizeVerCursor);
}

void GlobeView::mouseMoveEvent(QMouseEvent* event)
{
    if (dragButton_ == Qt::NoButton) {
        setIdleCursor(event->position());
        return QOpenGLWidget::mouseMoveEvent(event);
    }
    const QPointF pos = event->position();
    if (movingReceiver_) {
        // The receiver goes to the ground under the pointer, at its own height; the globe stays still
        Vec3 ground;
        double lat = 0, lon = 0, h = 0;
        if (pickGround(pos, &ground)) {
            ecefToLlh(ground, &lat, &lon, &h);
            emit receiverDragged(lat / kDeg, lon / kDeg);
        }
        lastMouse_ = pos;
        return;
    }
    double lat0 = 0, lon0 = 0, lat1 = 0, lon1 = 0;
    if (dragButton_ == Qt::RightButton) {
        // Up tilts the view towards the horizon, down back to straight down
        tilt_ = std::clamp(tilt_ - (pos.y() - lastMouse_.y()) * kTiltPerPixel, 0.0, kMaxTilt);
    } else if (pick(lastMouse_, &lat0, &lon0) && pick(pos, &lat1, &lon1)) {
        // The ground that was under the pointer follows it
        camLat_ += lat0 - lat1;
        camLon_ += std::remainder(lon0 - lon1, 2.0 * kPi);
    } else {
        // Off the Earth: turn by about the angle the view spans
        const double perPixel = std::min(2.0 * std::tan(kFov / 2.0 * kDeg) * range_, 2.5) / std::max(1, height());
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
    if (event->button() != dragButton_) return QOpenGLWidget::mouseReleaseEvent(event);
    dragButton_ = Qt::NoButton;
    movingReceiver_ = false;
    setIdleCursor(event->position());
}

// Each notch of the wheel takes the eye 15 % nearer the ground it looks at, or back
void GlobeView::wheelEvent(QWheelEvent* event)
{
    const double steps = event->angleDelta().y() / 120.0;
    if (steps == 0) return QOpenGLWidget::wheelEvent(event);
    range_ = std::clamp(range_ * std::pow(0.85, steps), kMinRange, kMaxRange);
    event->accept();
    update();
}
