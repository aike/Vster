# VSTer

A minimal host that launches VST3 plugins like standalone applications (Windows x64).
Play a software instrument instantly, without starting a DAW.

## Features

- Fixed serial chain: **1 VST3 instrument + 3 insert VST3 effects** (Inst → FX1 → FX2 → FX3 → Master)
- **ASIO** and WASAPI output
- **Host BPM** — tempo-synced plugins (delays, LFOs, arpeggiators, ...) follow the host
  play head, with a Play/Stop toggle (Play restarts from bar 1)
- On-screen keyboard plus **hardware MIDI input**
- **Session save/load** (`.vster`): slot configuration, full plugin states, BPM, master volume.
  If a plugin is missing on load, its saved state is preserved and written back on the next save;
  reloading the same plugin restores it automatically
- Master volume / MUTE (click-free ramp) and a stereo peak meter
- Crash insurance: autosave every 2 minutes and before each plugin load, with recovery on restart
- Plugin scanning with an automatic crash blacklist (dead-man's-pedal)

No sequencer, a single track only. MIDI is routed to the instrument slot only
(vocoder/arpeggiator-style *effect* plugins will stay silent).

## Building

Requirements: Visual Studio 2022 (C++ workload), CMake 3.22+, git.

```
git clone <this repo>
cd vster
git submodule update --init    # external/JUCE
```

### ASIO SDK (optional, recommended)

Steinberg's license **forbids redistributing the ASIO SDK**, so it is not part of this
repository and must never be committed to it. To build with ASIO support:

1. Download the ASIO SDK from https://www.steinberg.net/developers/
   (free, license agreement required)
2. Unzip it so that `sdk/asiosdk/common/iasiodrv.h` exists,
   or keep it anywhere else and pass `-DASIOSDK_DIR=<path>`

Only the SDK headers are used. If the SDK is not found, CMake prints a warning and
builds a WASAPI-only binary (you can also disable ASIO explicitly with
`-DVSTER_ENABLE_ASIO=OFF`).

### Build

```
cmake -B build -G "Visual Studio 17 2022" -A x64 [-DASIOSDK_DIR=<path>]
cmake --build build --config Release
```

Binary: `build/Vster_artefacts/Release/VSTer.exe`

## Known limitations

- **64-bit VST3 only.** 32-bit plugins and VST2 (`.dll`) cannot be loaded.
- Plugins run in-process: **a crashing plugin takes VSTer down with it.**
  Autosave/recovery limits the damage; a plugin that crashes a scan is blacklisted
  automatically on the next run.
- No plugin delay compensation. Latency from look-ahead plugins simply adds up
  (the total is shown in the status readout).
- ASIO drivers are usually exclusive — the device may fail to open while a DAW is using it.
- Some VST3 editors misbehave on HiDPI setups.

## License

VSTer is free software, released under the **GNU Affero General Public License v3.0**
(see [LICENSE](LICENSE)). AGPLv3 was chosen because it is the license under which the
dependencies may be used at no cost, and the combination stays compliant:

- **JUCE** (git submodule at `external/JUCE`) is used under its **AGPLv3** option.
  VSTer as a whole is therefore distributed under AGPLv3.
- **VST3 hosting** uses the VST3 interface headers bundled with JUCE, which Steinberg
  makes available under **GPLv3**. GPLv3 code may be combined with AGPLv3 code
  (see GPLv3 §13 / AGPLv3 §13); the combined work is distributed under AGPLv3.
  The separate proprietary Steinberg VST 3 SDK license is **not** used.
- The **ASIO SDK** is **not included** in this repository and is never redistributed
  in source form (its license forbids that). Users who want ASIO support download it
  from Steinberg themselves and accept Steinberg's license. Distributing *compiled*
  binaries built against it is permitted by the ASIO SDK licensing terms with the
  trademark attribution below.

If you distribute modified versions, the AGPLv3 requires you to provide the complete
corresponding source code.

### Trademark notices

VST is a trademark of Steinberg Media Technologies GmbH, registered in Europe and
other countries.
ASIO is a trademark and software of Steinberg Media Technologies GmbH.
