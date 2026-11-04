# GPSSim

Simulates the GPS L1 C/A signal seen by a receiver and writes it as I/Q samples to a file, streams it to SoftGpsReceiver
over TCP, transmits it at L1 from a USRP, or any mix of these.

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

## Transmitting with a USRP or a HackRF

Choose the radio in *Transmit at L1*, give its address (such as `addr=192.168.10.2` for a USRP, or nothing for the
first HackRF) and press Start. The radio is tuned to L1 minus the IF, so the signal lands on 1575.42 MHz, and it sets
the pace: the simulation runs at the sample rate. Transmitting needs an I/Q format.

```bash
gpssim_cli --usrp --usrp-args addr=192.168.10.2 --tx-gain 0 --seconds 0
```

```bash
gpssim_cli --hackrf --tx-gain 30 --ppm -10.6 --fs 2.5e6 --seconds 0
```

`--seconds 0` runs until Ctrl+C, which ends the transmission cleanly.

**Keep it off the air.** Connect the radio to the receiver by cable through attenuation, or work in a shielded
enclosure. Radiating a GPS signal is illegal in most countries, and every receiver within range would take it for the
real sky. The TX gain starts at 0 dB, and GPSSim sets it back to 0 dB when it stops, as both radios keep their settings.

**Levels.** Each satellite is about 10 log10(fs) - C/N0 below the total output: 17 dB at 5 MS/s and 50 dB-Hz. A
receiver expects about -130 dBm per satellite at its antenna input. The output carries the simulated noise too, so the
receiver measures the C/N0 that was set only while that noise is well above its own, which at 50 dB-Hz takes the
satellites to about -110 dBm. At -130 dBm the receiver's own noise takes over and C/N0 comes out near 40 dB-Hz. Work
out the attenuation from the radio's output power, and start with more rather than less.

### USRP

**Sample rate.** An N200 transmits at 100 MHz divided by a whole number. 5 MS/s (100 / 20) is a good choice. Other rates
are refused with the nearest one the device has.

**Frequency.** Without a reference, the N200's oscillator is good to about 2.5 ppm, a few kHz at L1. Receivers search
wider than that, but the Doppler they measure then differs from the simulated one. *Lock to a 10 MHz reference on REF
IN* (`--external-ref`) removes the offset.

**Carrier leak.** The USRP leaks a little of its LO at the centre frequency. Running `uhd_cal_tx_dc_offset --args
addr=192.168.10.2 --freq_start 1.5e9 --freq_stop 1.65e9` once reduces it; UHD then applies the correction itself. That
calibration transmits too, so put a 50 ohm load on the TX/RX port first.

**Ports.** An N200's front panel calls the daughterboard's ports RF 1 and RF 2: RF 1 is TX/RX, the only one that
transmits, and RF 2 is RX2.

**Testing with one USRP.** With a cable from TX/RX to RX2 (RF 1 to RF 2), `tests/usrp_loopback` (built with
`-DGPSSIM_BUILD_TESTS=ON`) transmits a simulation and receives it back on the same device, then serves what it
received on port 5555 like GPSSim's own stream: choose *GPSSim simulator (over TCP)* in SoftGpsReceiver. It exists
because a USRP can be open in only one program at a time, so GPSSim and SoftGpsReceiver cannot share one. With an
N200 and an SBX at 0 dB TX and RX gain, through a plain cable, the satellites come back at 43 to 48 dB-Hz and the fix
matches the simulated position to about a metre. It runs at 2.5 MS/s, because at 5 MS/s the simulation and a receiver
on the same computer can fall behind the device.

UHD comes from https://files.ettus.com/binaries/uhd/ and is loaded when transmitting starts, so GPSSim builds and runs
without it. The log reports underflows (the simulation fell behind the device) and packets lost on the way. The USRP code
can be tested without a radio through `tests/fake_uhd`, a stand-in `uhd.dll` built with `-DGPSSIM_BUILD_TESTS=ON` and
given with `--uhd-library`.

### HackRF One

A HackRF transmits 8-bit samples at 2 to 20 MS/s, so int8 I/Q suits it. Its TX gain (VGA) goes from 0 to 47 dB, and
*HackRF RF amplifier* adds 14 dB. With the amplifier off its output at L1 is small: through a plain cable into an N200,
it took a VGA of 30 dB for the satellites to come back at a good C/N0. GPSSim keeps a second of samples between itself
and the HackRF, so a short stall of the simulation does not reach the air. The antenna port's DC power is kept off.

**Crystal error.** A HackRF One without a TCXO is good to about 20 ppm, 30 kHz at L1, more than many receivers search.
*HackRF crystal error* (`--ppm`) corrects the carrier and the sample rate: positive when the HackRF runs fast. To
measure it, receive the HackRF on a better-clocked radio with a wide search (SoftGpsReceiver: `--search 40000`) and
compare the Dopplers with GPSSim's table. The HackRF this was written with ran 10.6 ppm slow against an N200. A 10 MHz
signal on CLKIN replaces the crystal altogether; *Lock to a 10 MHz reference* then insists on it.

With GPSSim on the HackRF at VGA 30, through a cable into SoftGpsReceiver on an N200 (RF 1, 20 dB), all 9 satellites
were tracked and the fix kept within 1.3 m (CEP50) of the simulated position, with no underflows.

**Getting hackrf.dll.** The HackRF project ships no Windows build. Build `hackrf.dll` from
<https://github.com/greatscottgadgets/hackrf> with the kit's MinGW; it needs `libusb.h` from libusb, of the version of
the `libusb-1.0.dll` it will run with (UHD installs one, 1.0.29 with UHD 4.11):

```bash
gcc -O2 -shared -Ihackrf/host/libhackrf/src -Ilibusb-include hackrf/host/libhackrf/src/hackrf.c -o hackrf.dll -L"C:/Program Files/UHD/bin" -l:libusb-1.0.dll -static-libgcc -Wl,-Bstatic -lwinpthread -Wl,-Bdynamic
```

Put `hackrf.dll` and `libusb-1.0.dll` in `third_party/hackrf/windows_x64/`, and the build copies them next to the
programs.

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
