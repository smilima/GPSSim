#pragma once

#include <QObject>
#include <QString>
#include <QVector3D>

#include <vector>

class QNetworkAccessManager;

struct City
{
    enum Kind : quint8 { Town, StateCapital, Capital };
    QString name;
    QVector3D pos;                 // on the WGS-84 ellipsoid, in Earth radii
    float minZoom = 0;             // the web-map zoom from which Natural Earth shows it: 2 for the largest, 9 for small towns
    quint32 population = 0;
    Kind kind = Town;
};

// Natural Earth's populated places (public domain, 7342 of them: capitals and the larger towns), fetched once and kept
// in a folder in a compact form, so that they show without a connection
class CityStore : public QObject
{
    Q_OBJECT

public:
    explicit CityStore(const QString& folder, QObject* parent = nullptr);

    // Reads the cities kept, or else fetches them (5 MB) and keeps them. ready or failed tells how it went.
    void load();
    bool busy() const { return busy_; }
    bool loaded() const { return loaded_; }
    bool kept() const;             // read from the folder, without fetching
    std::vector<City> take();      // after ready

signals:
    void ready();
    void failed(const QString& error);

private:
    void finish(std::vector<City> cities, const QString& error);

    QString folder_;
    QNetworkAccessManager* net_ = nullptr;
    bool busy_ = false, loaded_ = false;
    std::vector<City> cities_;
};
