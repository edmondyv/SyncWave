# SyncWave

Tired of sharing a single earphone during movie night? Wanna jam out with your friend — on separate headphones?

This tool lets you play audio through multiple (two currently😅) playback devices at the same time — yes, including Bluetooth speakers and headphones.
Perfect for watching movies, listening to music, or just vibing across devices.

Now with a **GUI** and **Bluetooth delay compensation** for perfectly synced audio!

This is just the test repo so the code is trash... gonna release the actual code soon. Also this is just one of the features the main project has.

# Features

- Play audio on two devices simultaneously via WASAPI loopback
- **GUI mode** with device selection and real-time delay slider
- **Bluetooth delay compensation** — configurable offset (0–1000 ms) to sync wired and wireless devices
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
2. **Adjust the delay offset** with the slider (0–1000 ms) to compensate for Bluetooth latency
3. **Start / Stop** playback with the buttons

> **Tip for Bluetooth sync:** If your Bluetooth device is the secondary output, set your system default to the Bluetooth device and use SyncWave to output to wired speakers with an offset matching the Bluetooth latency (typically 100–300 ms). Adjust the slider until both devices are in sync.

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
# Add 200ms delay to sync with a Bluetooth device
> ./SyncWave.exe -d 2 -o 200
```

# How Delay Compensation Works

Bluetooth audio devices typically introduce 100–300 ms of latency. When playing
audio on both a wired and a Bluetooth device simultaneously, this causes a
noticeable echo or delay.

SyncWave solves this by:

1. **Pre-filling the playback buffer** with silence equal to the configured offset,
   adding a controlled delay to the secondary output device.
2. **Adaptive buffer management** — if the buffer drifts beyond the target delay,
   excess frames are skipped to maintain sync during playback.

Set your Bluetooth device as the system default (source), then use SyncWave to mirror
audio to the wired device with a delay that matches the Bluetooth latency.

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
