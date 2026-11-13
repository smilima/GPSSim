#include "CityStore.h"

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

#include <algorithm>
#include <utility>

namespace {

// Natural Earth's own release, so the file does not change under GPSSim
const char kSource[] = "https://raw.githubusercontent.com/nvkelso/natural-earth-vector/v5.1.2/geojson/ne_10m_populated_places_simple.geojson";
constexpr quint32 kMagic = 0x43535047;      // "GPSC"
constexpr quint32 kVersion = 1;

struct Result
{
    std::vector<City> cities;
    QString error;
};

// Kept as latitude and longitude; placed on the ellipsoid when read
struct Place
{
    QString name;
    float lat = 0, lon = 0, minZoom = 0;
    quint32 population = 0;
    quint8 kind = City::Town;
};

QString parse(const QByteArray& json, std::vector<Place>* out)
{
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &error);
    if (doc.isNull()) return QStringLiteral("the cities are not GeoJSON: %1").arg(error.errorString());
    for (const QJsonValue& feature : doc.object().value(QLatin1String("features")).toArray()) {
        const QJsonObject p = feature.toObject().value(QLatin1String("properties")).toObject();
        const QString kind = p.value(QLatin1String("featurecla")).toString();
        Place place;
        place.name = p.value(QLatin1String("name")).toString();
        place.lat = float(p.value(QLatin1String("latitude")).toDouble());
        place.lon = float(p.value(QLatin1String("longitude")).toDouble());
        place.minZoom = float(p.value(QLatin1String("min_zoom")).toDouble(9));
        place.population = quint32(std::max(0.0, p.value(QLatin1String("pop_max")).toDouble()));
        place.kind = kind.startsWith(QLatin1String("Admin-0 capital")) ? City::Capital
                     : kind.startsWith(QLatin1String("Admin-1")) ? City::StateCapital : City::Town;
        if (!place.name.isEmpty()) out->push_back(place);
    }
    return out->empty() ? QStringLiteral("the cities file holds no places") : QString();
}

std::vector<City> placed(const std::vector<Place>& places)
{
    std::vector<City> cities;
    cities.reserve(places.size());
    for (const Place& p : places) {
        const sim::Vec3 e = sim::llhToEcef(double(p.lat) * sim::kPi / 180.0, double(p.lon) * sim::kPi / 180.0, 0);
        cities.push_back({p.name, QVector3D(float(e.x / sim::kWgsA), float(e.y / sim::kWgsA), float(e.z / sim::kWgsA)), p.minZoom,
                          p.population, City::Kind(p.kind)});
    }
    return cities;
}

QString cachePath(const QString& folder)
{
    return folder + QStringLiteral("/cities.bin");
}

// The cities kept, on another thread
Result readKept(const QString& file)
{
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly)) return {{}, f.errorString()};
    QDataStream s(&f);
    s.setFloatingPointPrecision(QDataStream::SinglePrecision);
    quint32 magic = 0, version = 0, count = 0;
    s >> magic >> version >> count;
    if (magic != kMagic || version != kVersion || count > 1'000'000)
        return {{}, QStringLiteral("%1 is not a cities file").arg(QDir::toNativeSeparators(file))};
    std::vector<Place> places(count);
    for (Place& p : places) s >> p.name >> p.lat >> p.lon >> p.minZoom >> p.population >> p.kind;
    if (s.status() != QDataStream::Ok) return {{}, QStringLiteral("%1 is not a cities file").arg(QDir::toNativeSeparators(file))};
    return {placed(places), QString()};
}

// The cities fetched, kept for next time, on another thread
Result keep(const QByteArray& json, const QString& file)
{
    std::vector<Place> places;
    const QString error = parse(json, &places);
    if (!error.isEmpty()) return {{}, error};
    QDir().mkpath(QFileInfo(file).absolutePath());
    QSaveFile out(file);
    if (out.open(QIODevice::WriteOnly)) {
        QDataStream s(&out);
        s.setFloatingPointPrecision(QDataStream::SinglePrecision);
        s << kMagic << kVersion << quint32(places.size());
        for (const Place& p : places) s << p.name << p.lat << p.lon << p.minZoom << p.population << p.kind;
        out.commit();      // shown even if it cannot be kept
    }
    return {placed(places), QString()};
}

} // namespace

CityStore::CityStore(const QString& folder, QObject* parent) : QObject(parent), folder_(folder), net_(new QNetworkAccessManager(this))
{
}

bool CityStore::kept() const
{
    return QFileInfo::exists(cachePath(folder_));
}

std::vector<City> CityStore::take()
{
    return std::exchange(cities_, std::vector<City>());
}

void CityStore::load()
{
    if (busy_ || loaded_) return;
    busy_ = true;
    const auto run = [this](auto job) {
        auto* watcher = new QFutureWatcher<Result>(this);
        connect(watcher, &QFutureWatcher<Result>::finished, this, [this, watcher] {
            watcher->deleteLater();
            Result r = watcher->result();
            finish(std::move(r.cities), r.error);
        });
        watcher->setFuture(QtConcurrent::run(job));
    };
    const QString file = cachePath(folder_);
    if (QFileInfo::exists(file)) {
        run([file] { return readKept(file); });
        return;
    }
    QNetworkRequest request{QUrl(QString::fromLatin1(kSource))};
    request.setTransferTimeout(120000);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("GPSSim"));
    QNetworkReply* reply = net_->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, run, file] {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() != QNetworkReply::NoError) finish({}, reply->errorString());
        else if (status != 200) finish({}, QStringLiteral("HTTP %1").arg(status));
        else {
            const QByteArray json = reply->readAll();
            run([json, file] { return keep(json, file); });
        }
    });
}

void CityStore::finish(std::vector<City> cities, const QString& error)
{
    busy_ = false;
    if (!error.isEmpty()) {
        emit failed(error);
        return;
    }
    cities_ = std::move(cities);
    loaded_ = true;
    emit ready();
}
