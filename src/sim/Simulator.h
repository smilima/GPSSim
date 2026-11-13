#pragma once

#include "Gps.h"

#include <QMetaType>
#include <QString>

#include <atomic>
#include <functional>
#include <memory>
#include <vector>

namespace sim {

enum class SampleFormat { Int8IQ = 0, Int16IQ, Int8Real, Int16Real };

inline bool isComplex(SampleFormat f) { return f == SampleFormat::Int8IQ || f == SampleFormat::Int16IQ; }
inline int bytesPerSample(SampleFormat f)
{
    return f == SampleFormat::Int8IQ ? 2 : f == SampleFormat::Int16IQ ? 4 : f == SampleFormat::Int8Real ? 1 : 2;
}

struct Scenario
{
    // Receiver
    double latDeg = 40.0, lonDeg = -105.0, altM = 1650.0;
    double velEast = 0, velNorth = 0, velUp = 0;      // m/s, constant
    double clockBias = 1.5e-4;                        // receiver clock ahead of GPS time, s
    double clockDrift = 1e-7;                         // s/s

    // Time. The start is a whole second.
    int week = 2445;
    int tow = 345600;
    double seconds = 60;                              // 0 = until stopped

    // The satellites: those of an almanac that are healthy, or a made-up constellation when it is empty
    std::vector<AlmanacEntry> almanac;

    // Signal
    double fs = 5e6;
    double ifFreq = 0;
    SampleFormat format = SampleFormat::Int8IQ;
    double cn0 = 50;                                  // dB-Hz at the zenith
    double noiseSigma = 30;                           // per rail, in counts of an 8-bit sample
    double elevationMask = 5;                         // degrees
    bool iono = true, tropo = true;
    int excludedPrn = 0;                              // 0 = none
    int threads = 0;                                  // 0 = all

    bool realTime = false;                            // hold the output to the sample rate

    QString validate() const;                         // empty when usable
};

struct SatStatus
{
    int prn = 0;
    double az = 0, el = 0;                            // degrees
    double range = 0;                                 // pseudorange, m
    double doppler = 0;                               // Hz
    double cn0 = 0;                                   // dB-Hz
};

struct Progress
{
    double seconds = 0;                               // simulated so far
    double speed = 0;                                 // times real time
    qint64 bytes = 0;
    double clipped = 0;                               // fraction of samples clipped
    Vec3 truth;                                       // receiver position (ECEF)
    std::vector<SatStatus> sats;
};

// Where the samples go. write() returns false, with error() set, to end the run.
class IqSink
{
public:
    virtual ~IqSink() = default;
    virtual bool write(const char* data, qint64 size) = 0;
    virtual QString error() const = 0;
};

// The satellites a scenario simulates, indexed by PRN - 1 with PRN 0 where there is none: the healthy ones of its
// almanac, or the made-up constellation, with their ephemeris reference time the even hour nearest the start
std::vector<Ephemeris> scenarioSatellites(const Scenario& s);

// Generates the signal and hands it to the sink, on the calling thread (and a pool for the satellites), until the end of
// the scenario or until stop is set. Returns an error message, empty on success.
QString runSimulation(const Scenario& s, IqSink& sink, const std::atomic<bool>& stop,
                      const std::function<void(const Progress&)>& progress);

// What a receiver needs in order to read the samples back: "GPS_sim-fs5-if0-int8iq.bin"
QString suggestedFileName(const Scenario& s);

} // namespace sim

Q_DECLARE_METATYPE(sim::Progress)
