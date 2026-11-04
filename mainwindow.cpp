#include "mainwindow.h"
#include "./ui_mainwindow.h"

#include "src/sim/HackRfOutput.h"
#include "src/sim/IqOutput.h"
#include "src/sim/UsrpOutput.h"

#include <QCloseEvent>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QSettings>
#include <QThread>
#include <QTime>

#include <memory>

using namespace sim;

namespace {
enum Radio { NoRadio = 0, UsrpRadio, HackRfRadio };      // the entries of radioCombo
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    qRegisterMetaType<sim::Progress>();
    ui->satTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);

    connect(ui->startButton, &QPushButton::clicked, this, &MainWindow::onStart);
    connect(ui->stopButton, &QPushButton::clicked, this, &MainWindow::onStop);
    connect(ui->browseButton, &QPushButton::clicked, this, &MainWindow::onBrowse);
    connect(ui->fileCheck, &QCheckBox::toggled, this, &MainWindow::updateControls);
    connect(ui->streamCheck, &QCheckBox::toggled, this, &MainWindow::updateControls);
    connect(ui->formatCombo, &QComboBox::currentIndexChanged, this, &MainWindow::updateControls);
    connect(ui->fsSpin, &QDoubleSpinBox::valueChanged, this, &MainWindow::updateControls);
    connect(ui->ifSpin, &QDoubleSpinBox::valueChanged, this, &MainWindow::updateControls);
    connect(ui->fileEdit, &QLineEdit::textEdited, this, [this] { nameEdited_ = true; });

    loadSettings();
    connect(ui->radioCombo, &QComboBox::currentIndexChanged, this, &MainWindow::onRadioChanged);
    updateControls();
}

void MainWindow::onRadioChanged()
{
    devices_[radio_] = ui->radioDeviceEdit->text();
    radio_ = ui->radioCombo->currentIndex();
    showRadio();
    updateControls();
}

void MainWindow::showRadio()
{
    const bool hackrf = radio_ == HackRfRadio;
    ui->radioDeviceEdit->setText(devices_[radio_]);
    ui->radioDeviceEdit->setPlaceholderText(hackrf ? tr("serial number, or its last digits (empty: the first one found)")
                                                   : tr("addr=192.168.10.2 (empty: the first one found)"));
    ui->radioGainSpin->setMaximum(hackrf ? 47 : 90);      // a HackRF's TX VGA; UHD clips to the daughterboard's range
    ui->radioGainSpin->setSingleStep(hackrf ? 1 : 0.5);
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    onStop();
    if (worker_) {
        worker_->wait();
        delete worker_;
        worker_ = nullptr;
    }
    saveSettings();
    event->accept();
}

Scenario MainWindow::scenario() const
{
    Scenario s;
    s.latDeg = ui->latSpin->value();
    s.lonDeg = ui->lonSpin->value();
    s.altM = ui->altSpin->value();
    s.velEast = ui->velEastSpin->value();
    s.velNorth = ui->velNorthSpin->value();
    s.velUp = ui->velUpSpin->value();
    s.clockBias = ui->biasSpin->value() * 1e-6;
    s.clockDrift = ui->driftSpin->value() * 1e-9;
    s.week = ui->weekSpin->value();
    s.tow = ui->towSpin->value();
    s.seconds = ui->secondsSpin->value();
    s.fs = ui->fsSpin->value() * 1e6;
    s.ifFreq = ui->ifSpin->value() * 1e6;
    s.format = SampleFormat(ui->formatCombo->currentIndex());
    s.cn0 = ui->cn0Spin->value();
    s.noiseSigma = ui->noiseSpin->value();
    s.elevationMask = ui->maskSpin->value();
    s.iono = ui->ionoCheck->isChecked();
    s.tropo = ui->tropoCheck->isChecked();
    s.realTime = ui->streamCheck->isChecked() && ui->radioCombo->currentIndex() == NoRadio;      // a radio sets the pace itself
    return s;
}

void MainWindow::updateControls()
{
    const bool running = worker_ != nullptr;
    ui->scenarioBox->setEnabled(!running);
    ui->signalBox->setEnabled(!running);
    ui->outputBox->setEnabled(!running);
    ui->fileEdit->setEnabled(ui->fileCheck->isChecked());
    ui->browseButton->setEnabled(ui->fileCheck->isChecked());
    ui->portSpin->setEnabled(ui->streamCheck->isChecked());
    const int radio = ui->radioCombo->currentIndex();
    ui->radioDeviceEdit->setEnabled(radio != NoRadio);
    ui->radioGainSpin->setEnabled(radio != NoRadio);
    ui->radioRefCheck->setEnabled(radio != NoRadio);
    ui->radioAmpCheck->setEnabled(radio == HackRfRadio);
    ui->radioPpmSpin->setEnabled(radio == HackRfRadio);
    ui->startButton->setEnabled(!running);
    ui->stopButton->setEnabled(running);

    if (!nameEdited_ && !running) {
        const QString dir = ui->fileEdit->text().isEmpty() ? QDir::homePath() : QFileInfo(ui->fileEdit->text()).absolutePath();
        ui->fileEdit->setText(QDir::toNativeSeparators(QDir(dir).filePath(suggestedFileName(scenario()))));
    }
}

void MainWindow::onBrowse()
{
    const QString path = QFileDialog::getSaveFileName(this, tr("I/Q file"), ui->fileEdit->text(), tr("I/Q samples (*.bin);;All files (*)"));
    if (path.isEmpty()) return;
    ui->fileEdit->setText(QDir::toNativeSeparators(path));
    nameEdited_ = true;
}

void MainWindow::onStart()
{
    const Scenario s = scenario();
    const bool toFile = ui->fileCheck->isChecked(), toStream = ui->streamCheck->isChecked();
    const int radio = ui->radioCombo->currentIndex();
    QString problem = s.validate();
    if (problem.isEmpty() && !toFile && !toStream && radio == NoRadio) problem = tr("Choose a file, a stream or a radio.");
    if (problem.isEmpty() && toStream && !isComplex(s.format)) problem = tr("Streaming to the receiver needs an I/Q format.");
    if (problem.isEmpty() && radio != NoRadio && !isComplex(s.format)) problem = tr("Transmitting needs an I/Q format.");
    if (problem.isEmpty() && toFile && s.seconds <= 0) problem = tr("A file needs a duration: 0 s runs until stopped, for streaming only.");
    if (!problem.isEmpty()) {
        QMessageBox::warning(this, tr("GPSSim"), problem);
        return;
    }
    if (toFile && QFileInfo::exists(ui->fileEdit->text())
        && QMessageBox::question(this, tr("GPSSim"), tr("Replace %1?").arg(ui->fileEdit->text())) != QMessageBox::Yes)
        return;
    if (radio != NoRadio && !transmitConfirmed_) {
        QMessageBox box(QMessageBox::Warning, tr("GPSSim"),
                        tr("GPSSim is about to transmit a GPS signal at 1575.42 MHz.\n\n"
                           "Connect the radio to the receiver by cable, through enough attenuation, or work in a shielded "
                           "enclosure. Radiating a GPS signal is illegal in most countries and misleads every GPS receiver "
                           "within range."), QMessageBox::Cancel, this);
        const QAbstractButton *transmit = box.addButton(tr("Transmit"), QMessageBox::AcceptRole);
        box.setDefaultButton(QMessageBox::Cancel);
        box.exec();
        if (box.clickedButton() != transmit) return;
        transmitConfirmed_ = true;
    }

    saveSettings();
    stop_ = false;
    ui->logEdit->clear();
    ui->progressBar->setRange(0, s.seconds > 0 ? 1000 : 0);
    ui->progressBar->setValue(0);
    const QString path = ui->fileEdit->text();
    const quint16 port = quint16(ui->portSpin->value());
    UsrpSettings u;
    u.args = ui->radioDeviceEdit->text().trimmed();
    u.gainDb = ui->radioGainSpin->value();
    u.externalRef = ui->radioRefCheck->isChecked();
    HackRfSettings h;
    h.serial = u.args;
    h.gainDb = u.gainDb;
    h.amp = ui->radioAmpCheck->isChecked();
    h.ppm = ui->radioPpmSpin->value();
    h.externalRef = u.externalRef;

    worker_ = QThread::create([this, s, path, toFile, toStream, port, radio, u, h] {
        TeeSink tee;
        FileSink file;
        std::unique_ptr<RadioSink> transmitter;
        TcpSink tcp;
        QString error;
        if (toFile) {
            if (file.open(path)) tee.add(&file);
            else error = file.error();
        }
        if (error.isEmpty() && radio == UsrpRadio) {
            QMetaObject::invokeMethod(this, [this] { log(tr("Opening the USRP")); });
            auto usrp = std::make_unique<UsrpSink>();
            if (usrp->open(u, s)) transmitter = std::move(usrp);
            else error = usrp->error();
        } else if (error.isEmpty() && radio == HackRfRadio) {
            QMetaObject::invokeMethod(this, [this] { log(tr("Opening the HackRF")); });
            auto hackrf = std::make_unique<HackRfSink>();
            if (hackrf->open(h, s)) transmitter = std::move(hackrf);
            else error = hackrf->error();
        }
        if (transmitter) {
            QMetaObject::invokeMethod(this, [this, d = transmitter->description()] { log(d); });
            tee.add(transmitter.get());
        }
        if (error.isEmpty() && toStream) {
            if (tcp.listen(port)) {
                QMetaObject::invokeMethod(this, [this, port] { log(tr("Waiting for the receiver on port %1").arg(port)); });
                if (tcp.waitForClient(s, stop_)) {
                    QMetaObject::invokeMethod(this, [this] { log(tr("Receiver connected")); });
                    tee.add(&tcp);
                }
            } else {
                error = tcp.error();
            }
        }
        int underflows = 0, lost = 0;
        if (error.isEmpty() && !stop_.load())
            error = runSimulation(s, tee, stop_, [&](const Progress &p) {
                QMetaObject::invokeMethod(this, "onProgress", Qt::QueuedConnection, Q_ARG(sim::Progress, p));
                if (!transmitter || (transmitter->underflows() == underflows && transmitter->lostPackets() == lost)) return;
                underflows = transmitter->underflows();
                lost = transmitter->lostPackets();
                QMetaObject::invokeMethod(this, [this, underflows, lost] {
                    log(tr("Radio: %1 underflows (the simulation fell behind the device), %2 packets lost on the way").arg(underflows).arg(lost));
                });
            });
        QMetaObject::invokeMethod(this, "onFinished", Qt::QueuedConnection, Q_ARG(QString, error));
    });
    worker_->start();
    updateControls();
    log(tr("Started: %1 MS/s, %2").arg(s.fs / 1e6).arg(ui->formatCombo->currentText()));
}

void MainWindow::onStop()
{
    stop_ = true;
}

void MainWindow::onProgress(const Progress &p)
{
    const Scenario s = scenario();
    if (s.seconds > 0) ui->progressBar->setValue(int(p.seconds / s.seconds * 1000.0));
    ui->statusLabel->setText(tr("%1 s simulated, %2x real time, %3 MB, %4 satellites, %5% of samples clipped")
                                 .arg(p.seconds, 0, 'f', 1).arg(p.speed, 0, 'f', 1).arg(double(p.bytes) / 1e6, 0, 'f', 0)
                                 .arg(p.sats.size()).arg(p.clipped * 100.0, 0, 'f', 3));
    ui->skyView->setSatellites(p.sats);
    ui->satTable->setRowCount(int(p.sats.size()));
    for (int r = 0; r < int(p.sats.size()); ++r) {
        const SatStatus &t = p.sats[size_t(r)];
        const QString cells[6] = {QString::number(t.prn), QString::number(t.az, 'f', 1), QString::number(t.el, 'f', 1),
                                  QString::number(t.doppler, 'f', 0), QString::number(t.cn0, 'f', 1), QString::number(t.range / 1e3, 'f', 3)};
        for (int c = 0; c < 6; ++c) {
            auto *item = ui->satTable->item(r, c);
            if (!item) {
                item = new QTableWidgetItem;
                ui->satTable->setItem(r, c, item);
            }
            item->setText(cells[c]);
        }
    }
}

void MainWindow::onFinished(const QString &error)
{
    worker_->wait();
    delete worker_;
    worker_ = nullptr;
    ui->progressBar->setRange(0, 1000);
    log(error.isEmpty() ? (stop_.load() ? tr("Stopped") : tr("Finished")) : tr("Failed: %1").arg(error));
    updateControls();
}

void MainWindow::log(const QString &text)
{
    ui->logEdit->appendPlainText(QTime::currentTime().toString(QStringLiteral("HH:mm:ss  ")) + text);
}

void MainWindow::saveSettings() const
{
    QSettings q(QStringLiteral("GPSSim"), QStringLiteral("GPSSim"));
    const auto spins = findChildren<QDoubleSpinBox *>();
    for (const QDoubleSpinBox *w : spins) q.setValue(w->objectName(), w->value());
    const auto ints = findChildren<QSpinBox *>();
    for (const QSpinBox *w : ints) q.setValue(w->objectName(), w->value());
    const auto checks = findChildren<QCheckBox *>();
    for (const QCheckBox *w : checks) q.setValue(w->objectName(), w->isChecked());
    q.setValue(QStringLiteral("format"), ui->formatCombo->currentIndex());
    q.setValue(QStringLiteral("file"), ui->fileEdit->text());
    q.setValue(QStringLiteral("nameEdited"), nameEdited_);
    q.setValue(QStringLiteral("radio"), ui->radioCombo->currentIndex());
    const auto device = [this](int radio) { return radio == radio_ ? ui->radioDeviceEdit->text() : devices_[radio]; };
    q.setValue(QStringLiteral("usrpArgs"), device(UsrpRadio));
    q.setValue(QStringLiteral("hackrfSerial"), device(HackRfRadio));
}

void MainWindow::loadSettings()
{
    QSettings q(QStringLiteral("GPSSim"), QStringLiteral("GPSSim"));
    const auto spins = findChildren<QDoubleSpinBox *>();
    for (QDoubleSpinBox *w : spins) w->setValue(q.value(w->objectName(), w->value()).toDouble());
    const auto ints = findChildren<QSpinBox *>();
    for (QSpinBox *w : ints) w->setValue(q.value(w->objectName(), w->value()).toInt());
    const auto checks = findChildren<QCheckBox *>();
    for (QCheckBox *w : checks) w->setChecked(q.value(w->objectName(), w->isChecked()).toBool());
    ui->formatCombo->setCurrentIndex(q.value(QStringLiteral("format"), 0).toInt());
    ui->fileEdit->setText(q.value(QStringLiteral("file")).toString());
    nameEdited_ = q.value(QStringLiteral("nameEdited"), false).toBool();
    devices_[UsrpRadio] = q.value(QStringLiteral("usrpArgs")).toString();
    devices_[HackRfRadio] = q.value(QStringLiteral("hackrfSerial")).toString();
    ui->radioCombo->setCurrentIndex(q.value(QStringLiteral("radio"), int(NoRadio)).toInt());
    radio_ = ui->radioCombo->currentIndex();
    showRadio();
}
