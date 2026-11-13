#include "GlobeDialog.h"
#include "ui_GlobeDialog.h"

#include "sim/GpsTime.h"

#include <QDir>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QUrl>

#include <cmath>

using namespace sim;

namespace {

// NASA's Blue Marble of the whole Earth, 4096 x 2048 pixels (10 km each at the equator), from its map service. The
// request is the same for everyone: nothing of the scenario goes with it.
const char kImageryUrl[] = "https://gibs.earthdata.nasa.gov/wms/epsg4326/best/wms.cgi?SERVICE=WMS&REQUEST=GetMap&VERSION=1.1.1"
                           "&LAYERS=BlueMarble_NextGeneration&STYLES=&SRS=EPSG:4326&BBOX=-180,-90,180,90&WIDTH=4096&HEIGHT=2048"
                           "&FORMAT=image/jpeg";

QString imageryPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + QStringLiteral("/imagery/blue_marble_4096.jpg");
}

} // namespace

GlobeDialog::GlobeDialog(QWidget* parent) : QDialog(parent), ui(new Ui::GlobeDialog)
{
    ui->setupUi(this);
    const QSettings q(QStringLiteral("GPSSim"), QStringLiteral("GPSSim"));
    restoreGeometry(q.value(QStringLiteral("globe/geometry")).toByteArray());
    ui->orbitsCheck->setChecked(q.value(QStringLiteral("globe/orbits"), true).toBool());
    ui->sightCheck->setChecked(q.value(QStringLiteral("globe/sightLines"), true).toBool());
    ui->gridCheck->setChecked(q.value(QStringLiteral("globe/grid"), false).toBool());
    ui->globeView->setShowOrbits(ui->orbitsCheck->isChecked());
    ui->globeView->setShowSightLines(ui->sightCheck->isChecked());
    ui->globeView->setShowGrid(ui->gridCheck->isChecked());

    connect(ui->orbitsCheck, &QCheckBox::toggled, ui->globeView, &GlobeView::setShowOrbits);
    connect(ui->sightCheck, &QCheckBox::toggled, ui->globeView, &GlobeView::setShowSightLines);
    connect(ui->gridCheck, &QCheckBox::toggled, ui->globeView, &GlobeView::setShowGrid);
    connect(ui->receiverButton, &QPushButton::clicked, ui->globeView, &GlobeView::lookAtReceiver);
    connect(ui->closeButton, &QPushButton::clicked, this, &QDialog::close);

    // Edit mode: the button reads View until pressed again
    viewHint_ = ui->statusLabel->text();
    connect(ui->editButton, &QPushButton::toggled, this, [this](bool on) {
        ui->editButton->setText(on ? tr("View") : tr("Edit"));
        ui->globeView->setEditing(on);
        if (!fetching_) showHint();
    });
    connect(ui->globeView, &GlobeView::receiverDragged, this, &GlobeDialog::receiverMoved);
}

void GlobeDialog::setEditable(bool on)
{
    if (!on) ui->editButton->setChecked(false);
    ui->editButton->setEnabled(on);
}

void GlobeDialog::showHint()
{
    ui->statusLabel->setText(ui->editButton->isChecked()
                                 ? tr("Edit: drag the red dot to move the receiver; its height stays. Press View to only look again.")
                                 : viewHint_);
}

GlobeDialog::~GlobeDialog()
{
    saveSettings();
    delete ui;
}

void GlobeDialog::setSatellites(const std::vector<Ephemeris>& sats, double elevationMask)
{
    ui->globeView->setSatellites(sats, elevationMask);
}

void GlobeDialog::setMoment(int week, double tow, const Vec3& receiver)
{
    const int weeks = int(std::floor(tow / kSecondsPerWeek)), seconds = int(std::floor(tow - double(weeks) * kSecondsPerWeek));
    ui->timeLabel->setText(tr("%1 UTC (GPS week %2, %3 s)")
                               .arg(gpsToUtc(week + weeks, seconds).toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")))
                               .arg(week + weeks).arg(seconds));
    ui->globeView->setMoment(tow, receiver);
}

void GlobeDialog::lookAtReceiver()
{
    ui->globeView->lookAtReceiver();
}

void GlobeDialog::showEvent(QShowEvent* event)
{
    QDialog::showEvent(event);
    if (!ui->globeView->hasEarthImage() && !fetching_) loadImagery();
}

void GlobeDialog::hideEvent(QHideEvent* event)
{
    ui->editButton->setChecked(false);      // it opens again to look
    saveSettings();
    QDialog::hideEvent(event);
}

void GlobeDialog::saveSettings() const
{
    QSettings q(QStringLiteral("GPSSim"), QStringLiteral("GPSSim"));
    q.setValue(QStringLiteral("globe/geometry"), saveGeometry());
    q.setValue(QStringLiteral("globe/orbits"), ui->orbitsCheck->isChecked());
    q.setValue(QStringLiteral("globe/sightLines"), ui->sightCheck->isChecked());
    q.setValue(QStringLiteral("globe/grid"), ui->gridCheck->isChecked());
}

// The Earth's imagery from the copy kept, or else from NASA, kept for next time
void GlobeDialog::loadImagery()
{
    const QString path = imageryPath();
    const QImage kept(path);
    if (!kept.isNull()) {
        ui->globeView->setEarthImage(kept);
        return;
    }
    if (!net_) net_ = new QNetworkAccessManager(this);
    QNetworkRequest request{QUrl(QString::fromLatin1(kImageryUrl))};
    request.setTransferTimeout(90000);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("GPSSim"));
    QNetworkReply* reply = net_->get(request);
    fetching_ = true;
    ui->statusLabel->setText(tr("Fetching the Earth's imagery from NASA (0.7 MB)…"));
    connect(reply, &QNetworkReply::finished, this, [this, reply, path] {
        reply->deleteLater();
        fetching_ = false;
        const QByteArray data = reply->readAll();
        QImage image;
        QString error;
        if (reply->error() != QNetworkReply::NoError) error = reply->errorString();
        else if (!image.loadFromData(data)) error = tr("the reply is not an image");
        if (!error.isEmpty()) {
            ui->statusLabel->setText(tr("No imagery (%1), so the Earth is plain. It is fetched again the next time the globe opens.")
                                         .arg(error));
            return;
        }
        QDir().mkpath(QFileInfo(path).absolutePath());
        QSaveFile file(path);
        if (file.open(QIODevice::WriteOnly) && file.write(data) == data.size()) file.commit();
        ui->globeView->setEarthImage(image);
        showHint();
    });
}
