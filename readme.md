# SyncWave

Tired of sharing a single earphone during movie night? Wanna jam out with your friend — on separate headphones?

This tool lets you play audio through multiple (two currently😅) playback devices at the same time — yes, including Bluetooth speakers and headphones.
Perfect for watching movies, listening to music, or just vibing across devices.

Now with a **GUI** and **Bluetooth delay compensation** for perfectly synced audio!

This is just the test repo so the code is trash... gonna release the actual code soon. Also this is just one of the features the main project has.

# Features

- Play audio on two devices simultaneously via WASAPI loopback
- **GUI mode** with device selection, real-time delay slider, and modern visual styles
- **Bluetooth delay compensation** — configurable offset (0–1000 ms) to sync wired and wireless devices
- **Delay direction** — choose whether to delay the output device or the default (system) device, fixing sync when the default device is the laptop speakers and the secondary is a Bluetooth speaker
- **Per-device volume control** — independent volume sliders for both the output and default devices
- **Channel routing** — per-device left/right channel selection (Both, Left only, Right only)
- **Frequency filtering** — per-device low-pass and high-pass filters with adjustable cutoff; use the Bluetooth speaker as a subwoofer by applying a low-pass filter on its output and a high-pass filter on the laptop speakers
- Adaptive buffer management to reduce drift during playback
- CLI mode for scripting and quick use

# Installation
You can download the latest release from the [releases](https://github.com/CodeWithDevesh/SyncWave/releases).

# Usage

## GUI Mode

Launch the graphical interface with the `-g` flag:

```sh
./SyncWave.exe --gui
```

The GUI lets you:
1. **Select an output device** from the dropdown
2. **Choose delay direction** — *Delay output device* (default) or *Delay default device*
3. **Adjust the delay offset** with the slider (0–1000 ms) to compensate for Bluetooth latency
4. **Control volume** independently for both the output and default devices
5. **Route channels** — send only left or right channel to each device
6. **Apply filters** — low-pass and high-pass filters per device (e.g. turn a BT speaker into a subwoofer)
7. **Start / Stop** playback with the buttons

### Delay Direction

- **Delay output device** (original behaviour): adds a delay to the secondary (non-default) device. Use this when your Bluetooth device is the system default and you mirror to wired speakers.
- **Delay default device**: adds a delay to the system default device. Use this when your laptop speakers are the default and the Bluetooth speaker is the secondary output. In this mode SyncWave opens a third playback device on the default output so it can control the timing.

> **Tip for Bluetooth sync:** Adjust the delay slider until both devices sound in sync. Typical Bluetooth latency is 100–300 ms.

### Subwoofer Mode Example

To use a Bluetooth speaker as a subwoofer:
1. Select the BT speaker as the output device
2. Under *Output Device Controls*, set the **low-pass filter** to around 150–250 Hz
3. Under *Default Device Controls*, set the **high-pass filter** to the same frequency
4. This sends only bass to the BT speaker and removes bass from the laptop speakers

## CLI Mode

Open the terminal and navigate to the folder where you extracted the files.

Run with -h flag for help

```sh
cd "path to your folder"
./syncwave.exe -h

OPTIONS:

      -h, --help                        Display this help menu
      -l, --list-devices                List available devices
      -d[device], --device=[device]     Output device number
      -o[offset], --offset=[offset]     Delay offset in ms (0-1000) for sync
      -g, --gui                         Launch the graphical interface
      --delay-default                   Apply delay to default device instead of output
```

You can use the --list-devices command to list the available devices for playback on your system.

```sh
./SyncWave.exe --list-devices

Available Devices
   0: Headphones (Sushi)  -> (default)
   1: TS35505 (HD Audio Driver for Display Audio)
   2: Speakers (Realtek(R) Audio)
```

Then with the -d command you can specify the output device you want to loopback your audio from.

It takes in the device number listed with the --list-devices command

```sh
> ./SyncWave.exe -d 1
```

### Bluetooth Delay Compensation (CLI)

Use the `-o` flag to add a delay offset (in milliseconds) to the playback output.
This compensates for the inherent latency of Bluetooth devices:

```sh
# Add 200ms delay to sync with a Bluetooth device (delays output device)
> ./SyncWave.exe -d 2 -o 200

# Add 200ms delay to the default (system) device instead
> ./SyncWave.exe -d 2 -o 200 --delay-default
```

# How Delay Compensation Works

Bluetooth audio devices typically introduce 100–300 ms of latency. When playing
audio on both a wired and a Bluetooth device simultaneously, this causes a
noticeable echo or delay.

SyncWave solves this by:

1. **Pre-filling the playback buffer** with silence equal to the configured offset,
   adding a controlled delay to the chosen device.
2. **Adaptive buffer management** — if the buffer drifts beyond the target delay,
   excess frames are skipped to maintain sync during playback.
3. **Delay direction** — when the default device is the fast one (e.g. laptop speakers)
   and the secondary device is slow (e.g. Bluetooth), select *Delay default device*
   so the speakers are delayed to match Bluetooth latency.

# Building
The third-party libraries used are-
- [spdlog](https://github.com/KjellKod/g3log) (The Logger).
- [miniaudio](https://miniaud.io/) (The Powerhouse).
- [args](https://github.com/Taywee/args) (The Argument Parser).

All these libraries are header only and are already included in the source so you don't need to setup any of them.

Build with cmake... 

```sh
mkdir build
cd build
cmake ..
cmake --build .
```

For release version use

```sh
cmake --build . --config Release
```

Again... build on windows only...
