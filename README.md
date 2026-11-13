# GPSSim

Simulates the GPS L1 C/A signal seen by a receiver and writes it as I/Q samples to a file, streams it to SoftGpsReceiver
over TCP, transmits it at L1 from a USRP or a HackRF One, or any mix of these.

Open `CMakeLists.txt` in Qt Creator with a Qt 6 MinGW 64-bit kit. It builds `GPSSim` (the window) and `gpssim_cli` (the
same simulation without one).

## What to install

Only Qt is needed to build and run GPSSim. The radio drivers are loaded when transmitting starts, so each one is needed
only for its radio.

| What | Needed for | Where it goes | Where to get it |
|---|---|---|---|
| Qt 6 for MinGW 64-bit, with the MinGW compiler, CMake and Ninja | building | the Qt installer's folder | <https://www.qt.io/download> |
| UHD | transmitting from a USRP | its installer's folder, `C:\Program Files\UHD` | <https://files.ettus.com/binaries/uhd/> |
| `hackrf.dll` and `libusb-1.0.dll` | transmitting from a HackRF One | `third_party/hackrf/windows_x64/` | built from the HackRF sources: [Getting hackrf.dll](#getting-hackrfdll) |
| Python 3 with Pillow | only redrawing the icon (`resources/make_icon.py`) | | <https://www.python.org/>, then `pip install pillow` |

* **Qt.** Only Qt Base's modules are used (Core, Gui, Widgets, OpenGL, OpenGL Widgets, Concurrent and Network), so no
  add-ons are needed. The globe draws with OpenGL, which any graphics driver of the last fifteen years provides. GPSSim
  was written with Qt 6.11.2 and MinGW 13.1, in a static build of Qt, whose programs run on their own. The installer's
  Qt is a shared build: the programs run from Qt Creator, and to run one anywhere else, `windeployqt GPSSim.exe` copies
  Qt's DLLs next to it.
* **UHD** was 4.11.0 here. GPSSim finds `uhd.dll` through `UHD_PKG_PATH`, in `C:\Program Files\UHD\bin` and on `PATH`,
  or `gpssim_cli --uhd-library` gives its path. It is built with Visual C++: if it will not load, install the
  [Microsoft Visual C++ Redistributable](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist) for
  x64. An N200 on Ethernet needs no driver.
* **HackRF.** `third_party/` is not in git, so create the folder. One `hackrf.dll` serves GPSSim and SoftGpsReceiver
  both. CMake looks for it when it configures, so run CMake again after adding it (*Build > Run CMake* in Qt Creator);
  the build then copies both DLLs next to the programs. Windows 10 and later install the WinUSB driver a HackRF needs.

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

## Start position

The *Location* box holds the receiver's start. Type or paste it into *Position* in whichever way it is written, and
press Enter:

| Written as | For example |
|---|---|
| Decimal degrees | `40.014984, -105.270546` |
| Degrees and minutes | `40° 00.899' N, 105° 16.233' W` |
| Degrees, minutes and seconds | `40°00'53.94"N 105°16'13.97"W` |
| UTM | `13T 476911 4429455` |
| MGRS | `13TDE 76911 29455` |
| ECEF X, Y and Z, in metres | `-1288678.58, -4720147.72, 4080320.87` |

A height may follow, in metres or in feet (`1650 m`, `5413 ft`); without one the height stays as it was. Heights are
above the WGS-84 ellipsoid, not above sea level. *Latitude, longitude* and *Height* follow the Position, and it follows
them. The list beside it chooses the way the position is written, and goes to the way a position was typed.

* **Degrees.** The latitude comes first unless N, S, E and W say otherwise. The marks may be left out
  (`40 00 53.94 N 105 16 13.97 W`), and a longitude may run to 360°.
* **UTM.** The letter is the latitude band. N and S are also read as the hemisphere when the northing is not in that
  band, and `13 North 476911 4429455` says it outright. UTM and MGRS reach from 80° S to 84° N: nearer the poles the
  position is written in degrees.
* **MGRS.** A position is the south-west corner of its square, at any precision from 100 km (`13TDE`) to 1 m.
* **ECEF** is three numbers more than 1000 km from the centre of the Earth, or marked `X`, `Y` and `Z`.
* A position that cannot be read turns red, and the log says why. The position stays as it was.

```bash
gpssim_cli --location "13T 476911 4429455 1650 m" --file GPS_sim-fs2_5-if0-int8iq.bin --fs 2.5e6 --seconds 60
```

`--location` takes the same forms, in place of `--lat`, `--lon` and `--alt`.

## The globe

*Globe…*, beside *Height*, opens the scenario on a 3D globe: the Earth, the receiver, and every satellite simulated,
where they are at the start. While a run goes on, the globe follows it, at the time simulated.

* **Satellites** in green are above the elevation mask: these are the ones simulated, and a line of sight joins each
  to the receiver. Grey ones are below the mask. A satellite behind the Earth is hidden.
* **Orbits** are drawn as they lie in space at that moment, so each passes through its satellite. *Grid* adds
  latitude and longitude every 15°.
* **Borders.** Country borders are drawn in yellow, and state and province borders in white, which fade in from
  8000 km down. They are Natural Earth's (public domain, at 1:10 million, so within about a kilometre), fetched once
  (23 MB) the first time *Borders* is on, and kept in a compact form (4 MB).
* **Cities.** Capitals and larger towns are named, more of them nearer the ground: the largest from afar, towns of
  about 100 000 from a few thousand kilometres. Capitals are in bold gold. No name covers another, nor the receiver,
  a satellite or the key: where one would, the less important city is left out. They are Natural Earth's 7342
  populated places (public domain), fetched once (5 MB) the first time *Cities* is on and kept in a compact form
  (270 kB). Smaller towns are not among them: near Boulder, for one, Longmont and Golden are missing.
* **Moving around.** Drag with the left button to turn the globe: the ground under the pointer follows it. Drag up
  with the right button to tilt the view towards the northern horizon, turning about the ground in the middle, and
  down to look straight down again; tilted all the way, the lines of sight rise into the sky over the receiver. The
  wheel zooms, from 15 Earth radii out, where every orbit shows whole, to 5 km from the ground in the middle.
  *Go to Receiver* turns the receiver back to the middle, and so does a new position; the tilt stays.
* **Moving the receiver.** Press *Edit*, which then reads *View*, and drag the receiver's red dot, ringed in white:
  the receiver goes to the ground under the pointer, keeping its height, and *Position*, *Latitude, longitude* follow
  as it goes. The globe stays still meanwhile; dragging anywhere else still turns it. Press *View* to only look
  again. Edit is off while a run goes on, and each time the window opens.
* **Imagery.** From afar the Earth is NASA's Blue Marble, 4096 × 2048 pixels, fetched from NASA's GIBS map service
  the first time the globe opens (0.7 MB). Nearer, the globe fetches tiles of the part in view, as sharp as they
  show: Blue Marble down to 490 m a pixel, then over land the Landsat mosaic of NASA's Web-Enabled Landsat Data
  (WELD) down to 31 m, where streets and fields show. Six tiles come at a time, the coarsest first, so the view fills
  in and then sharpens; the line at the bottom of the globe counts the tiles on their way.
* **Offline.** Every image, the borders and the cities are kept in `GlobeData` next to `GPSSim.exe` as they arrive
  (`blue_marble_4096.jpg`, `borders\borders.bin`, `cities\cities.bin`, and `tiles\bluemarble` and `tiles\landsat` by
  level, row and column,
  20 to 75 kB a tile) and read from there first, so a place seen once shows again just as sharp without a connection.
  To take the globe to another computer, copy `GPSSim.exe` with its `GlobeData` folder: the static build needs
  nothing else. Without a connection the line at the bottom says so, and places not seen before show the sharpest
  imagery kept around them, or else the Blue Marble from afar, or plain blue. A tile is asked for again a minute
  after it could not be fetched. A tile request names only the tile, which tells NASA which part of the Earth is being
  looked at.
* **Where else.** Where `GPSSim.exe`'s folder cannot be written, as under `Program Files`, the globe keeps its data in
  `%LOCALAPPDATA%\GPSSim\imagery` instead. What earlier versions kept there moves to `GlobeData` the first time the
  globe opens. Each build folder (Qt Creator's Debug and Release) has its own `GlobeData`.

The globe's window keeps its size, place and check boxes.

## Start time

Set the start as a UTC date and time in *Start (UTC)*, or as *GPS week* and *Start time of week*: each follows the
other, and *Now* takes the current time. GPS time runs ahead of UTC by the leap seconds added since 1980, 18 s since
the start of 2017, and the navigation message carries the count of the start date (subframe 4, page 18).

```bash
gpssim_cli --start 2026-11-07T19:50:06 --file GPS_sim-fs2_5-if0-int8iq.bin --fs 2.5e6 --seconds 60
```

`--start` takes UTC (or a time with its offset, such as `+02:00`) or `now`, in place of `--week` and `--tow`. A
receiver shows the date modulo 1024 weeks, as with real signals: SoftGpsReceiver lists each possible era.

## Simulating a real almanac

By default GPSSim makes up its satellites. To simulate the real constellation instead, press *Fetch Latest…* next to
*Almanac*: it downloads the current YUMA almanac from the US Coast Guard Navigation Center
(<https://www.navcen.uscg.gov/gps-nanus-almanacs-opsadvisories-sof>) and loads it. The download is kept in
`%LOCALAPPDATA%\GPSSim\almanacs`, named by its week and reference time, such as `yuma_week2444_589824.alm`, and is
loaded again the next time GPSSim starts. *Load…* takes any other YUMA almanac from a file, and *Clear* goes back to
the made-up satellites.

```bash
gpssim_cli --almanac current_yuma.alm --file GPS_sim-fs2_5-if0-int8iq.bin --fs 2.5e6 --seconds 60
```

* **Satellites.** Each healthy satellite in the almanac is simulated. A satellite marked unhealthy is left out, and
  the log lists it.
* **Time.** Loading an almanac moves the start to its reference time, where it matches the real sky best. Any other start works: the orbits are carried there as plain ellipses, without the
  forces that change real orbits, so the further from the reference time, the more the sky differs from the real one.
  The log mentions it beyond a week. `gpssim_cli` starts at the reference time unless `--start`, `--week` or `--tow`
  is given.
* **Weeks.** YUMA files count weeks modulo 1024, so GPSSim takes the full week nearest today: an almanac older than
  about ten years comes out 1024 weeks late. Its satellites and the fix are still right; only the date is not.
* **What is broadcast** is made from the almanac too: each satellite's ephemeris is its almanac orbit, carried to the
  run's reference time, so a receiver solves with the same orbits that are simulated. The almanac pages carry every
  satellite's orbit at a reference time of their own, as a real satellite's do.

With an almanac of March 2023 and one of November 2026, SoftGpsReceiver tracked the satellites above the mask, decoded
the right date, and placed its fix within 1.5 m (CEP50) of the simulated position.

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

#### Getting hackrf.dll

The HackRF project ships no Windows build of its library, so `hackrf.dll` is built from its sources with the kit's
MinGW. It runs with UHD's `libusb-1.0.dll` (1.0.29 with UHD 4.11), and is compiled against libusb's header of the same
version. Without UHD, take `libusb-1.0.dll` from the Windows binaries of a libusb release,
<https://github.com/libusb/libusb/releases>, and give its folder to `-L` below.

1. In an empty folder, get the HackRF sources and libusb's header. The tag is the version of `libusb-1.0.dll`, which
   the DLL's *Properties > Details* shows:

   ```bash
   git clone https://github.com/greatscottgadgets/hackrf
   ```

   ```bash
   git clone --depth 1 --branch v1.0.29 https://github.com/libusb/libusb
   ```

2. With the kit's MinGW on `PATH` (`C:\Qt\Tools\mingw1310_64\bin`), build it in that folder:

   ```bash
   gcc -O2 -shared -Ihackrf/host/libhackrf/src -Ilibusb/libusb hackrf/host/libhackrf/src/hackrf.c -o hackrf.dll -L"C:/Program Files/UHD/bin" -l:libusb-1.0.dll -static-libgcc -Wl,-Bstatic -lwinpthread -Wl,-Bdynamic
   ```

3. Copy `hackrf.dll` and `C:\Program Files\UHD\bin\libusb-1.0.dll` into `third_party/hackrf/windows_x64/` and run CMake
   again. The build copies them next to the programs.

## What is simulated

* 32 made-up satellites in six orbital planes, described by broadcast ephemeris, or the healthy satellites of a YUMA
  almanac (see [Simulating a real almanac](#simulating-a-real-almanac)).
* Full LNAV message with valid parity: ephemeris, clock, ionosphere and almanac. A PRN with no satellite has a dummy
  SV page (SV ID 0) in place of its almanac.
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
