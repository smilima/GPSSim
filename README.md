# GPSSim

Simulates the GPS L1 C/A signal seen by a receiver and writes it as I/Q samples to a file, streams it to SoftGpsReceiver
over TCP, or both.

Open `CMakeLists.txt` in Qt Creator with a Qt 6 kit. It builds `GPSSim` (the window) and `gpssim_cli` (the same
simulation without one).

## Using it with SoftGpsReceiver

**File:** keep the suggested name, such as `GPS_sim-fs5-if0-int8iq.bin`. The receiver reads the rate, IF and format from it.

**Stream:** tick *Stream to SoftGpsReceiver*, press Start, then in the receiver choose *GPSSim simulator (over TCP)* in
Settings > Live and start live mode. The simulator waits for the receiver and then runs at the sample rate. Streaming
needs an I/Q format.

From the command line:

```bash
gpssim_cli --port 5555 --seconds 120
```

```bash
gnss_cli --tcp 127.0.0.1:5555 --gnss gps --seconds 100
```

## What is simulated

* 32 made-up satellites in six orbital planes, described by broadcast ephemeris. They are not the real constellation.
* Full LNAV message with valid parity: ephemeris, clock, ionosphere and almanac.
* Satellite clocks, Earth rotation, Klobuchar ionosphere, Saastamoinen troposphere, receiver clock offset and drift, and
  a receiver moving at constant velocity.
* White noise, with C/N0 falling with elevation from the zenith value.

The atmosphere and the receiver's corrections use the same models, so a fix matching the true position shows the signal
chain is consistent. It is not evidence of accuracy against the real sky.

Subframe 4 page 18 is sent every other frame instead of every 12.5 minutes, so that short runs carry the ionosphere
parameters. There is no multipath, and no RINEX input.

## Stream format

TCP, little-endian. A 40-byte header follows the connection, then samples until the end of the run:

| Offset | Type | Meaning |
| --- | --- | --- |
| 0 | char[8] | `GPSSIMIQ` |
| 8 | u32 | version, 1 |
| 12 | u32 | 0 = int8 I/Q, 1 = int16 I/Q |
| 16 | f64 | sample rate, Hz |
| 24 | f64 | IF, Hz |
| 32 | u32 | GPS week |
| 36 | u32 | start time of week, s |
