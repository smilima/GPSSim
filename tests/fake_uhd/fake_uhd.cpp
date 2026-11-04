// A stand-in for uhd.dll, for testing the USRP output without a radio. It exports the same C API (the structures below
// are copied from the UHD headers) and behaves like an N200 that transmits: rates are 100 MHz over a whole number from
// 4 to 512, samples are taken at the real rate through a 100 ms buffer, and running dry is reported as an underflow.
// What it transmits comes back on its receive side, as through a cable, for usrp_loopback. It prints what it is asked
// for, and writes the samples it is given to a file. Like UHD, opening it again in the same process gives the same
// device.
//
//   FAKE_UHD_FILE        where the interleaved int16 I/Q goes (optional)
//   FAKE_UHD_UNDERFLOW_AT  seconds into the stream: report an underflow there, as when the computer falls behind
//   FAKE_UHD_NO_DEVICE   when set, opening the device fails as it does with no USRP on the network
//   FAKE_UHD_NO_LOCK     when set, the external reference never locks

#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#define API extern "C" __declspec(dllexport)

using uhd_error = int;
enum { UHD_ERROR_NONE = 0, UHD_ERROR_RUNTIME = 41 };

typedef struct { char* cpu_format; char* otw_format; char* args; size_t* channel_list; int n_channels; } uhd_stream_args_t;
typedef struct { int stream_mode; size_t num_samps; bool stream_now; int64_t time_spec_full_secs; double time_spec_frac_secs; } uhd_stream_cmd_t;
typedef struct { double target_freq; int rf_freq_policy; double rf_freq; int dsp_freq_policy; double dsp_freq; char* args; } uhd_tune_request_t;
typedef struct { double clipped_rf_freq; double target_rf_freq; double actual_rf_freq; double target_dsp_freq; double actual_dsp_freq; } uhd_tune_result_t;
typedef struct { char* mboard_id; char* mboard_name; char* mboard_serial; char* tx_id; char* tx_subdev_name; char* tx_subdev_spec; char* tx_serial; char* tx_antenna; } uhd_usrp_tx_info_t;

namespace {

using Clock = std::chrono::steady_clock;
constexpr double kMasterClock = 100e6, kBuffer = 0.1;
constexpr int kUnderflow = 0x2;

struct Device
{
    int opened = 0;                     // handles given out
    double rate = 1e6, freq = 0, gain = 0;
    std::string clock = "internal";
    FILE* file = nullptr;
    bool bursting = false;
    Clock::time_point start;
    long long taken = 0;                // samples accepted in this burst
    double underflowAt = -1;
    std::deque<int> events;

    // The receive side
    double rxRate = 1e6, rxFreq = 0, rxGain = 0;
    bool receiving = false;
    std::mutex lock;
    std::condition_variable arrived;
    std::deque<int16_t> cable;          // transmitted, not yet received
};

struct TxMetadata { bool hasTime, startOfBurst, endOfBurst; };
struct Event { int code = 0; };
struct RxMetadata { int code = 0; };

Device g;
std::string lastError;
bool sensorValue = false;

uhd_error failure(const std::string& what)
{
    lastError = what;
    return UHD_ERROR_RUNTIME;
}

uhd_error copyString(const std::string& s, char* out, size_t len)
{
    if (len == 0) return UHD_ERROR_NONE;
    std::strncpy(out, s.c_str(), len - 1);
    out[len - 1] = 0;
    return UHD_ERROR_NONE;
}

double played()                         // seconds of the burst the device has transmitted so far
{
    return std::chrono::duration<double>(Clock::now() - g.start).count();
}

} // namespace

API uhd_error uhd_get_last_error(char* out, size_t len) { return copyString(lastError, out, len); }
API uhd_error uhd_usrp_last_error(void*, char* out, size_t len) { return copyString(lastError, out, len); }
API uhd_error uhd_get_version_string(char* out, size_t len) { return copyString("fake-1.0", out, len); }

API uhd_error uhd_usrp_make(void** h, const char* args)
{
    std::printf("FAKE_UHD: usrp_make args=\"%s\"%s\n", args, g.opened ? " (already open)" : "");
    if (std::getenv("FAKE_UHD_NO_DEVICE")) return failure("LookupError: KeyError: No devices found for ----->\nDevice Address:\n    addr: 192.168.10.2");
    *h = &g;
    if (g.opened++) return UHD_ERROR_NONE;
    if (const char* name = std::getenv("FAKE_UHD_FILE")) {
        g.file = std::fopen(name, "wb");
        if (!g.file) return failure(std::string("cannot open ") + name);
    }
    if (const char* u = std::getenv("FAKE_UHD_UNDERFLOW_AT")) g.underflowAt = std::atof(u);
    return UHD_ERROR_NONE;
}

API uhd_error uhd_usrp_free(void** h)
{
    *h = nullptr;
    std::printf("FAKE_UHD: usrp_free\n");
    if (--g.opened > 0) return UHD_ERROR_NONE;
    if (g.file) std::fclose(g.file);
    g.file = nullptr;
    return UHD_ERROR_NONE;
}

API uhd_error uhd_usrp_set_tx_rate(void*, double rate, size_t chan)
{
    const double interp = std::fmin(512, std::fmax(4, std::round(kMasterClock / rate)));
    g.rate = kMasterClock / interp;
    std::printf("FAKE_UHD: set_tx_rate %.1f chan %zu -> %.1f\n", rate, chan, g.rate);
    return UHD_ERROR_NONE;
}
API uhd_error uhd_usrp_get_tx_rate(void*, size_t, double* out) { *out = g.rate; return UHD_ERROR_NONE; }

API uhd_error uhd_usrp_set_tx_freq(void*, uhd_tune_request_t* req, size_t, uhd_tune_result_t* res)
{
    std::printf("FAKE_UHD: tune target %.1f rf policy %c dsp policy %c args \"%s\"\n", req->target_freq, req->rf_freq_policy,
                req->dsp_freq_policy, req->args ? req->args : "(null)");
    g.freq = req->target_freq;
    res->target_rf_freq = res->actual_rf_freq = req->target_freq;
    return UHD_ERROR_NONE;
}
API uhd_error uhd_usrp_get_tx_freq(void*, size_t, double* out) { *out = g.freq; return UHD_ERROR_NONE; }

API uhd_error uhd_usrp_set_tx_gain(void*, double gain, size_t, const char* name)
{
    g.gain = std::fmin(31.5, std::fmax(0.0, gain));      // the range of an SBX
    std::printf("FAKE_UHD: tx gain %.1f name \"%s\" -> %.1f\n", gain, name, g.gain);
    return UHD_ERROR_NONE;
}
API uhd_error uhd_usrp_get_tx_gain(void*, size_t, const char*, double* out) { *out = g.gain; return UHD_ERROR_NONE; }

API uhd_error uhd_usrp_set_clock_source(void*, const char* c, size_t)
{
    std::printf("FAKE_UHD: clock %s\n", c);
    g.clock = c;
    return UHD_ERROR_NONE;
}

API uhd_error uhd_sensor_value_make(void** h) { *h = &sensorValue; return UHD_ERROR_NONE; }
API uhd_error uhd_sensor_value_free(void** h) { *h = nullptr; return UHD_ERROR_NONE; }
API uhd_error uhd_sensor_value_to_bool(void*, bool* out) { *out = sensorValue; return UHD_ERROR_NONE; }
API uhd_error uhd_usrp_get_mboard_sensor(void*, const char* name, size_t, void**)
{
    if (std::string(name) != "ref_locked") return failure(std::string("no sensor ") + name);
    sensorValue = g.clock == "external" && !std::getenv("FAKE_UHD_NO_LOCK");
    return UHD_ERROR_NONE;
}

API uhd_error uhd_usrp_get_tx_info(void*, size_t, uhd_usrp_tx_info_t* info)
{
    info->mboard_id = _strdup("N200r4");
    info->mboard_name = _strdup("fake");
    info->mboard_serial = _strdup("0");
    info->tx_id = _strdup("SBX v5 (0x0068)");
    info->tx_subdev_name = _strdup("SBX/CBX TX");
    info->tx_subdev_spec = _strdup("A:0");
    info->tx_serial = _strdup("0");
    info->tx_antenna = _strdup("TX/RX");
    return UHD_ERROR_NONE;
}
API uhd_error uhd_usrp_tx_info_free(uhd_usrp_tx_info_t* info)
{
    for (char* s : {info->mboard_id, info->mboard_name, info->mboard_serial, info->tx_id, info->tx_subdev_name,
                    info->tx_subdev_spec, info->tx_serial, info->tx_antenna})
        std::free(s);
    return UHD_ERROR_NONE;
}

API uhd_error uhd_tx_streamer_make(void** h) { *h = &g; return UHD_ERROR_NONE; }
API uhd_error uhd_tx_streamer_free(void** h) { *h = nullptr; return UHD_ERROR_NONE; }

API uhd_error uhd_usrp_get_tx_stream(void*, uhd_stream_args_t* a, void*)
{
    std::printf("FAKE_UHD: tx stream cpu=%s otw=%s args=\"%s\" channels=%d chan0=%zu\n", a->cpu_format, a->otw_format, a->args,
                a->n_channels, a->channel_list[0]);
    if (std::string(a->cpu_format) != "sc16") return failure("expected sc16");
    return UHD_ERROR_NONE;
}

API uhd_error uhd_tx_metadata_make(void** h, bool hasTime, int64_t, double, bool startOfBurst, bool endOfBurst)
{
    *h = new TxMetadata{hasTime, startOfBurst, endOfBurst};
    return UHD_ERROR_NONE;
}
API uhd_error uhd_tx_metadata_free(void** h) { delete static_cast<TxMetadata*>(*h); *h = nullptr; return UHD_ERROR_NONE; }

API uhd_error uhd_async_metadata_make(void** h) { *h = new Event; return UHD_ERROR_NONE; }
API uhd_error uhd_async_metadata_free(void** h) { delete static_cast<Event*>(*h); *h = nullptr; return UHD_ERROR_NONE; }
API uhd_error uhd_async_metadata_event_code(void* h, int* out) { *out = static_cast<Event*>(h)->code; return UHD_ERROR_NONE; }

API uhd_error uhd_tx_streamer_send(void*, const void** buffs, size_t samps, void** md, double timeout, size_t* sent)
{
    const TxMetadata& m = *static_cast<TxMetadata*>(*md);
    *sent = 0;
    if (m.startOfBurst) {
        std::printf("FAKE_UHD: start of burst\n");
        g.bursting = true;
        g.start = Clock::now();
        g.taken = 0;
    }
    if (!g.bursting) return failure("samples outside a burst");

    // Whatever the device ran out of while waiting is gone. Ending the burst is not running out.
    const double t = played();
    if (!m.endOfBurst && t > double(g.taken) / g.rate + 1e-3) {
        g.events.push_back(kUnderflow);
        g.start += std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(t - double(g.taken) / g.rate));
    }
    if (g.underflowAt >= 0 && double(g.taken) / g.rate >= g.underflowAt) {
        g.underflowAt = -1;
        g.events.push_back(kUnderflow);
    }

    const auto deadline = Clock::now() + std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(timeout));
    while (*sent < samps) {
        const long long room = (long long)((played() + kBuffer) * g.rate) - g.taken;
        if (room <= 0) {
            if (Clock::now() >= deadline) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            continue;
        }
        const size_t n = std::min<size_t>(samps - *sent, size_t(room));
        const int16_t* from = static_cast<const int16_t*>(buffs[0]) + 2 * *sent;
        if (g.file) std::fwrite(from, 4, n, g.file);
        {
            std::lock_guard<std::mutex> hold(g.lock);
            if (g.receiving) g.cable.insert(g.cable.end(), from, from + 2 * n);
        }
        g.arrived.notify_one();
        *sent += n;
        g.taken += (long long)n;
    }
    if (m.endOfBurst) {
        std::printf("FAKE_UHD: end of burst after %lld samples (%.3f s)\n", g.taken, double(g.taken) / g.rate);
        g.bursting = false;
    }
    return UHD_ERROR_NONE;
}

API uhd_error uhd_tx_streamer_recv_async_msg(void*, void** md, double, bool* valid)
{
    *valid = !g.events.empty();
    if (*valid) {
        static_cast<Event*>(*md)->code = g.events.front();
        g.events.pop_front();
    }
    return UHD_ERROR_NONE;
}

// The receive side, which gets back what was transmitted
API uhd_error uhd_usrp_set_rx_rate(void*, double rate, size_t)
{
    g.rxRate = kMasterClock / std::fmin(512, std::fmax(4, std::round(kMasterClock / rate)));
    std::printf("FAKE_UHD: set_rx_rate %.1f -> %.1f\n", rate, g.rxRate);
    return UHD_ERROR_NONE;
}
API uhd_error uhd_usrp_get_rx_rate(void*, size_t, double* out) { *out = g.rxRate; return UHD_ERROR_NONE; }

API uhd_error uhd_usrp_set_rx_freq(void*, uhd_tune_request_t* req, size_t, uhd_tune_result_t* res)
{
    std::printf("FAKE_UHD: rx tune target %.1f\n", req->target_freq);
    g.rxFreq = req->target_freq;
    res->target_rf_freq = res->actual_rf_freq = req->target_freq;
    return UHD_ERROR_NONE;
}
API uhd_error uhd_usrp_get_rx_freq(void*, size_t, double* out) { *out = g.rxFreq; return UHD_ERROR_NONE; }

API uhd_error uhd_usrp_set_rx_gain(void*, double gain, size_t, const char*)
{
    g.rxGain = std::fmin(31.5, std::fmax(0.0, gain));
    std::printf("FAKE_UHD: rx gain %.1f -> %.1f\n", gain, g.rxGain);
    return UHD_ERROR_NONE;
}
API uhd_error uhd_usrp_get_rx_gain(void*, size_t, const char*, double* out) { *out = g.rxGain; return UHD_ERROR_NONE; }
API uhd_error uhd_usrp_set_rx_antenna(void*, const char* a, size_t) { std::printf("FAKE_UHD: rx antenna %s\n", a); return UHD_ERROR_NONE; }

API uhd_error uhd_rx_streamer_make(void** h) { *h = &g; return UHD_ERROR_NONE; }
API uhd_error uhd_rx_streamer_free(void** h) { *h = nullptr; return UHD_ERROR_NONE; }
API uhd_error uhd_rx_metadata_make(void** h) { *h = new RxMetadata; return UHD_ERROR_NONE; }
API uhd_error uhd_rx_metadata_free(void** h) { delete static_cast<RxMetadata*>(*h); *h = nullptr; return UHD_ERROR_NONE; }
API uhd_error uhd_rx_metadata_error_code(void* h, int* out) { *out = static_cast<RxMetadata*>(h)->code; return UHD_ERROR_NONE; }
API uhd_error uhd_rx_streamer_max_num_samps(void*, size_t* out) { *out = 363; return UHD_ERROR_NONE; }

API uhd_error uhd_usrp_get_rx_stream(void*, uhd_stream_args_t* a, void*)
{
    std::printf("FAKE_UHD: rx stream cpu=%s otw=%s\n", a->cpu_format, a->otw_format);
    if (std::string(a->cpu_format) != "sc16") return failure("expected sc16");
    return UHD_ERROR_NONE;
}

API uhd_error uhd_rx_streamer_issue_stream_cmd(void*, const uhd_stream_cmd_t* cmd)
{
    std::printf("FAKE_UHD: rx stream command '%c'\n", cmd->stream_mode);
    std::lock_guard<std::mutex> hold(g.lock);
    g.receiving = cmd->stream_mode == 'a';
    g.cable.clear();
    return UHD_ERROR_NONE;
}

API uhd_error uhd_rx_streamer_recv(void*, void** buffs, size_t samps, void** md, double timeout, bool, size_t* got)
{
    RxMetadata& m = *static_cast<RxMetadata*>(*md);
    *got = 0;
    m.code = 0;
    std::unique_lock<std::mutex> hold(g.lock);
    if (!g.arrived.wait_for(hold, std::chrono::duration<double>(timeout), [] { return !g.cable.empty(); })) {
        m.code = 0x1;                      // timeout
        return UHD_ERROR_NONE;
    }
    const size_t n = std::min(samps, g.cable.size() / 2);
    std::copy_n(g.cable.begin(), 2 * n, static_cast<int16_t*>(buffs[0]));
    g.cable.erase(g.cable.begin(), g.cable.begin() + std::ptrdiff_t(2 * n));
    *got = n;
    return UHD_ERROR_NONE;
}
