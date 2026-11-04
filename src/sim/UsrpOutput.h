#pragma once

#include "IqOutput.h"

#include <QString>

#include <memory>
#include <vector>

namespace sim {

struct UsrpSettings
{
    QString args;                                     // UHD device address, such as "addr=192.168.10.2". Empty = the first USRP found
    double gainDb = 0;                                // TX gain
    bool externalRef = false;                         // lock to a 10 MHz reference on REF IN
    QString library;                                  // uhd.dll. Empty = UHD_PKG_PATH, the default install, then PATH
};

// Transmits the samples at L1 from a USRP. As in SoftGpsReceiver's USRP input, UHD is loaded at run time through its
// C API: its C++ API cannot be used across compilers, the Windows installer is built with MSVC, and the program must
// also run on computers without UHD. Used from one thread only.
class UsrpSink : public RadioSink
{
public:
    UsrpSink();
    ~UsrpSink() override;
    // Tunes so that the scenario's IF lands on L1. Fails if the device cannot run at the scenario's sample rate.
    bool open(const UsrpSettings& u, const Scenario& s);
    bool write(const char* data, qint64 size) override;
    QString error() const override { return error_; }

    QString description() const override { return description_; }
    QString library() const;                                // the uhd.dll in use, after open()
    int underflows() const override { return underflows_; }
    int lostPackets() const override { return lostPackets_; }

private:
    struct Api;
    QString lastError(int code) const;
    void readEvents();

    std::unique_ptr<Api> api_;
    void* usrp_ = nullptr;
    void* streamer_ = nullptr;
    void* metadata_ = nullptr;
    void* events_ = nullptr;
    bool wide_ = false, started_ = false;
    std::vector<int16_t> buf_;
    QString error_, description_;
    int underflows_ = 0, lostPackets_ = 0;
};

} // namespace sim
