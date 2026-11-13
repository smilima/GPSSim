#pragma once

#include <QObject>
#include <QString>
#include <QVector3D>

#include <vector>

class QNetworkAccessManager;

// Borders as lines on the WGS-84 ellipsoid, in Earth radii, two points to a segment
struct BorderLines
{
    std::vector<QVector3D> countries, states;
};

// Natural Earth's country borders and state and province borders (public domain, at 1:10 million), fetched once and
// kept in a folder in a compact form, so that they show without a connection
class BorderStore : public QObject
{
    Q_OBJECT

public:
    explicit BorderStore(const QString& folder, QObject* parent = nullptr);

    // Reads the borders kept, or else fetches them (23 MB) and keeps them. ready or failed tells how it went.
    void load();
    bool busy() const { return busy_; }
    bool loaded() const { return loaded_; }
    bool kept() const;             // read from the folder, without fetching
    BorderLines take();            // after ready

signals:
    void ready();
    void failed(const QString& error);

private:
    void finish(BorderLines lines, const QString& error);

    QString folder_;
    QNetworkAccessManager* net_ = nullptr;
    bool busy_ = false, loaded_ = false;
    BorderLines lines_;
};
