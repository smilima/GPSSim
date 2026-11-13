#include "BorderStore.h"

#include "sim/Gps.h"

#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QtConcurrent/QtConcurrentRun>

#include <memory>
#include <utility>

namespace {

// Natural Earth's own release, so the files do not change under GPSSim
const char kSource[] = "https://raw.githubusercontent.com/nvkelso/natural-earth-vector/v5.1.2/geojson/";
const char* const kFiles[2] = {"ne_10m_admin_0_boundary_lines_land.geojson", "ne_10m_admin_1_states_provinces_lines.geojson"};
constexpr quint32 kMagic = 0x42535047;      // "GPSB"
constexpr quint32 kVersion = 1;

// Lines as latitude and longitude in degrees, by turns
using Polylines = std::vector<std::vector<float>>;

struct Result
{
    BorderLines lines;
    QString error;
};

QString parse(const QByteArray& json, Polylines* out)
{
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &error);
    if (doc.isNull()) return QStringLiteral("the borders are not GeoJSON: %1").arg(error.errorString());
    const auto addLine = [out](const QJsonArray& points) {
        std::vector<float> line;
        line.reserve(size_t(points.size()) * 2);
        for (const QJsonValue& p : points) {
            const QJsonArray lonLat = p.toArray();
            line.push_back(float(lonLat.at(1).toDouble()));
            line.push_back(float(lonLat.at(0).toDouble()));
        }
        if (line.size() >= 4) out->push_back(std::move(line));
    };
    for (const QJsonValue& feature : doc.object().value(QLatin1String("features")).toArray()) {
        const QJsonObject geometry = feature.toObject().value(QLatin1String("geometry")).toObject();
        const QString type = geometry.value(QLatin1String("type")).toString();
        const QJsonArray coordinates = geometry.value(QLatin1String("coordinates")).toArray();
        if (type == QLatin1String("LineString")) addLine(coordinates);
        else if (type == QLatin1String("MultiLineString"))
            for (const QJsonValue& line : coordinates) addLine(line.toArray());
    }
    return out->empty() ? QStringLiteral("the borders hold no lines") : QString();
}

void write(QDataStream& s, const Polylines& lines)
{
    s << quint32(lines.size());
    for (const std::vector<float>& line : lines) {
        s << quint32(line.size() / 2);
        for (float v : line) s << v;
    }
}

bool read(QDataStream& s, Polylines* lines)
{
    quint32 count = 0;
    s >> count;
    for (quint32 i = 0; i < count && s.status() == QDataStream::Ok; ++i) {
        quint32 points = 0;
        s >> points;
        if (points > 10'000'000) return false;
        std::vector<float> line(size_t(points) * 2);
        for (float& v : line) s >> v;
        lines->push_back(std::move(line));
    }
    return s.status() == QDataStream::Ok;
}

std::vector<QVector3D> segments(const Polylines& lines)
{
    std::vector<QVector3D> out;
    for (const std::vector<float>& line : lines) {
        QVector3D last;
        for (size_t i = 0; i < line.size(); i += 2) {
            const sim::Vec3 p = sim::llhToEcef(double(line[i]) * sim::kPi / 180.0, double(line[i + 1]) * sim::kPi / 180.0, 0);
            const QVector3D here(float(p.x / sim::kWgsA), float(p.y / sim::kWgsA), float(p.z / sim::kWgsA));
            if (i > 0) out.insert(out.end(), {last, here});
            last = here;
        }
    }
    return out;
}

QString cachePath(const QString& folder)
{
    return folder + QStringLiteral("/borders.bin");
}

// The borders kept, on another thread
Result readKept(const QString& file)
{
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly)) return {{}, f.errorString()};
    QDataStream s(&f);
    s.setFloatingPointPrecision(QDataStream::SinglePrecision);
    quint32 magic = 0, version = 0;
    s >> magic >> version;
    Polylines countries, states;
    if (magic != kMagic || version != kVersion || !read(s, &countries) || !read(s, &states))
        return {{}, QStringLiteral("%1 is not a borders file").arg(QDir::toNativeSeparators(file))};
    return {{segments(countries), segments(states)}, QString()};
}

// The borders fetched, kept for next time, on another thread
Result keep(const QByteArray& countryJson, const QByteArray& stateJson, const QString& file)
{
    Polylines countries, states;
    QString error = parse(countryJson, &countries);
    if (error.isEmpty()) error = parse(stateJson, &states);
    if (!error.isEmpty()) return {{}, error};
    QDir().mkpath(QFileInfo(file).absolutePath());
    QSaveFile out(file);
    if (out.open(QIODevice::WriteOnly)) {
        QDataStream s(&out);
        s.setFloatingPointPrecision(QDataStream::SinglePrecision);
        s << kMagic << kVersion;
        write(s, countries);
        write(s, states);
        out.commit();      // shown even if it cannot be kept
    }
    return {{segments(countries), segments(states)}, QString()};
}

} // namespace

BorderStore::BorderStore(const QString& folder, QObject* parent) : QObject(parent), folder_(folder), net_(new QNetworkAccessManager(this))
{
}

bool BorderStore::kept() const
{
    return QFileInfo::exists(cachePath(folder_));
}

BorderLines BorderStore::take()
{
    return std::exchange(lines_, BorderLines());
}

void BorderStore::load()
{
    if (busy_ || loaded_) return;
    busy_ = true;
    const auto run = [this](auto job) {
        auto* watcher = new QFutureWatcher<Result>(this);
        connect(watcher, &QFutureWatcher<Result>::finished, this, [this, watcher] {
            watcher->deleteLater();
            Result r = watcher->result();
            finish(std::move(r.lines), r.error);
        });
        watcher->setFuture(QtConcurrent::run(job));
    };
    const QString file = cachePath(folder_);
    if (QFileInfo::exists(file)) {
        run([file] { return readKept(file); });
        return;
    }

    // Both files at once; the borders are made when the second arrives
    auto got = std::make_shared<std::pair<QByteArray, QByteArray>>();
    auto left = std::make_shared<int>(2);
    auto error = std::make_shared<QString>();
    for (int i = 0; i < 2; ++i) {
        QNetworkRequest request(QUrl(QString::fromLatin1(kSource) + QLatin1String(kFiles[i])));
        request.setTransferTimeout(120000);
        request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("GPSSim"));
        QNetworkReply* reply = net_->get(request);
        connect(reply, &QNetworkReply::finished, this, [=] {
            reply->deleteLater();
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (reply->error() != QNetworkReply::NoError) *error = reply->errorString();
            else if (status != 200) *error = QStringLiteral("HTTP %1").arg(status);
            else (i == 0 ? got->first : got->second) = reply->readAll();
            if (--*left > 0) return;
            if (!error->isEmpty()) {
                finish({}, *error);
                return;
            }
            run([got, file] { return keep(got->first, got->second, file); });
        });
    }
}

void BorderStore::finish(BorderLines lines, const QString& error)
{
    busy_ = false;
    if (!error.isEmpty()) {
        emit failed(error);
        return;
    }
    lines_ = std::move(lines);
    loaded_ = true;
    emit ready();
}
