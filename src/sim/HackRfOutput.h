#pragma once

#include "IqOutput.h"

#include <QString>

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <vector>

namespace sim {

struct HackRfSettings
{
    QString serial;                                   // or its last digits. Empty = the first HackRF found
    double gainDb = 0;                                // TX VGA, 0 to 47 dB
    bool amp = false;                                 // the 14 dB RF amplifier
    double ppm = 0;                                   // error of the HackRF's crystal, positive when it runs fast
    bool externalRef = false;                         // insist on a 10 MHz reference on CLKIN (used whenever present)
    QString library;                                  // hackrf.dll. Empty = next to the program, then PATH
};

// Transmits the samples at L1 from a HackRF One. libhackrf is loaded at run time, so the program runs on computers
// without it. The HackRF asks for samples from a thread of its own: write() fills a ring that it drains, and waits
// while the ring is full. Its crystal is good to about 20 ppm, 30 kHz at L1, which the ppm setting corrects by
// retuning both the carrier and the sample rate. Used from one thread only, apart from the HackRF's own.
class HackRfSink : public RadioSink
{
public:
    HackRfSink();
    ~HackRfSink() override;
    // Tunes so that the scenario's IF lands on L1
    bool open(const HackRfSettings& h, const Scenario& s);
    bool write(const char* data, qint64 size) override;
    QString error() const override { return error_; }

    QString description() const override { return description_; }
    int underflows() const override { return underflows_.load(); }

private:
    struct Api;
    static int onTransfer(void* transfer);
    QString failure(const QString& what, int code) const;

    std::unique_ptr<Api> api_;
    void* device_ = nullptr;
    bool initialised_ = false, wide_ = false, streaming_ = false;
    std::vector<int8_t> converted_;
    QString error_, description_;

    // The ring between write() and the HackRF's thread
    std::mutex lock_;
    std::condition_variable drained_;
    std::vector<int8_t> ring_;
    size_t readAt_ = 0, held_ = 0;
    std::atomic<int> underflows_{0};
};

} // namespace sim
