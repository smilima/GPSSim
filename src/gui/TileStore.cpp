#include "TileStore.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QtConcurrent/QtConcurrentRun>

namespace {

constexpr int kParallel = 6;                   // fetches at once
constexpr qint64 kRetryMs = 60000;
constexpr int kTimeoutMs = 30000;

QImage decode(const QString& file)
{
    return QImage(file).convertToFormat(QImage::Format_RGBA8888);
}

} // namespace

TileStore::TileStore(const QString& folder, QObject* parent) : QObject(parent), folder_(folder), net_(new QNetworkAccessManager(this))
{
}

QUrl TileStore::url(const TileKey& key)
{
    const bool landsat = key.level > kLastBlueMarbleLevel;
    return QUrl(QStringLiteral("https://gibs.earthdata.nasa.gov/wmts/epsg4326/best/%1/default/%2/%3/%4/%5.jpeg")
                    .arg(landsat ? QStringLiteral("Landsat_WELD_CorrectedReflectance_TrueColor_Global_Annual")
                                 : QStringLiteral("BlueMarble_NextGeneration"),
                         landsat ? QStringLiteral("default/31.25m") : QStringLiteral("500m"))
                    .arg(key.level)
                    .arg(key.row)
                    .arg(key.col));
}

QString TileStore::path(const TileKey& key) const
{
    return QStringLiteral("%1/%2/%3/%4/%5.jpg")
        .arg(folder_, key.level > kLastBlueMarbleLevel ? QStringLiteral("landsat") : QStringLiteral("bluemarble"))
        .arg(key.level)
        .arg(key.row)
        .arg(key.col);
}

void TileStore::want(const std::vector<TileKey>& keys)
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    queue_.clear();
    for (const TileKey& k : keys) {
        if (busy_.contains(k)) continue;
        const auto failed = failed_.constFind(k);
        if (failed != failed_.cend() && (failed.value() == 0 || now - failed.value() < kRetryMs)) continue;
        const QString file = path(k);
        if (QFileInfo::exists(file)) read(k, file);
        else queue_.push_back(k);
    }
    fetchMore();      // no changed() here: the view asks while it draws, and shows the count then
}

// Decoded on another thread. A file that will not decode is fetched again.
void TileStore::read(const TileKey& key, const QString& file)
{
    busy_.insert(key);
    auto* watcher = new QFutureWatcher<QImage>(this);
    connect(watcher, &QFutureWatcher<QImage>::finished, this, [this, watcher, key, file] {
        watcher->deleteLater();
        busy_.remove(key);
        const QImage image = watcher->result();
        if (image.isNull()) {
            QFile::remove(file);
            return;
        }
        emit tileReady(key, image);
    });
    watcher->setFuture(QtConcurrent::run(decode, file));
}

void TileStore::fetchMore()
{
    while (inFlight_ < kParallel && !queue_.empty()) {
        const TileKey key = queue_.front();
        queue_.erase(queue_.begin());
        busy_.insert(key);
        ++inFlight_;
        QNetworkRequest request(url(key));
        request.setTransferTimeout(kTimeoutMs);
        request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("GPSSim"));
        QNetworkReply* reply = net_->get(request);
        connect(reply, &QNetworkReply::finished, this, [this, reply, key] {
            reply->deleteLater();
            --inFlight_;
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const QByteArray data = reply->readAll();
            const bool image = reply->header(QNetworkRequest::ContentTypeHeader).toString().startsWith(QLatin1String("image/"));
            if (status == 0) {
                // No answer at all: no connection, or NASA did not answer in time
                offline_ = reply->error() != QNetworkReply::OperationCanceledError && reply->error() != QNetworkReply::TimeoutError;
                failed_.insert(key, QDateTime::currentMSecsSinceEpoch());
                busy_.remove(key);
            } else if (status != 200 || !image) {
                offline_ = false;
                failed_.insert(key, status >= 400 && status < 500 ? 0 : QDateTime::currentMSecsSinceEpoch());
                busy_.remove(key);
            } else {
                offline_ = false;
                const QString file = path(key);
                QDir().mkpath(QFileInfo(file).absolutePath());
                QSaveFile out(file);
                if (out.open(QIODevice::WriteOnly) && out.write(data) == data.size() && out.commit()) {
                    busy_.remove(key);
                    read(key, file);
                } else {
                    // Shown, though it cannot be kept
                    busy_.remove(key);
                    QImage shown;
                    if (shown.loadFromData(data)) emit tileReady(key, shown.convertToFormat(QImage::Format_RGBA8888));
                }
            }
            fetchMore();
            emit changed();
        });
    }
}
