#pragma once

#include <QHash>
#include <QImage>
#include <QObject>
#include <QSet>
#include <QString>
#include <QUrl>

#include <vector>

class QNetworkAccessManager;

// A tile of NASA GIBS's imagery in latitude and longitude (EPSG:4326): 512 pixels a side, level 0 spanning 288°, each
// level halving the span, row 0 at 90° N and column 0 at 180° W. Level 3 has 10 x 5 tiles that cover the Earth
// exactly, and each tile holds the four of the level below it.
struct TileKey
{
    int level = 0, row = 0, col = 0;
    bool operator==(const TileKey& o) const { return level == o.level && row == o.row && col == o.col; }
    TileKey ancestor(int up) const { return {level - up, row >> up, col >> up}; }
};

inline size_t qHash(const TileKey& k, size_t seed = 0)
{
    return qHashMulti(seed, k.level, k.row, k.col);
}

// Blue Marble covers the Earth from level 4 to 7 (490 m a pixel at the equator). Landsat covers the land from level 8
// to 11 (31 m), and is black where it has no data, such as the sea.
constexpr int kFirstTileLevel = 4, kLastBlueMarbleLevel = 7, kLastTileLevel = 11;

// The tiles, read from a folder, or else fetched from NASA and kept in the folder, so that a place seen once shows
// again without a connection
class TileStore : public QObject
{
    Q_OBJECT

public:
    explicit TileStore(const QString& folder, QObject* parent = nullptr);

    // The tiles wanted now, the most needed first. Each comes with tileReady: one kept in the folder at once, one
    // fetched as it arrives. One no longer in the list is not fetched; one NASA does not have is not asked for again,
    // and one that could not be fetched is asked for again after a minute.
    void want(const std::vector<TileKey>& keys);
    int fetching() const { return int(queue_.size()) + inFlight_; }
    bool offline() const { return offline_; }      // the last fetch found no connection
    QString folder() const { return folder_; }
    static QUrl url(const TileKey& key);

signals:
    void tileReady(const TileKey& key, const QImage& image);      // RGBA8888
    void changed();                                                // what fetching() and offline() say

private:
    QString path(const TileKey& key) const;
    void read(const TileKey& key, const QString& file);
    void fetchMore();

    QString folder_;
    QNetworkAccessManager* net_ = nullptr;
    QSet<TileKey> busy_;                     // being read or fetched
    std::vector<TileKey> queue_;             // to fetch, the most needed first
    int inFlight_ = 0;
    QHash<TileKey, qint64> failed_;          // when each failed, in ms since 1970: 0 for a tile NASA does not have
    bool offline_ = false;
};
