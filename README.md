# VoiceCraft Studio

Real-time voice processing for Linux — turns an ordinary microphone into a
podcast-grade source and hands the result to OBS Studio as a selectable
virtual microphone called **“VoiceCraft Studio Microphone”**.

Version 1.0.0 · C++17 · Qt 5 · PulseAudio · RNNoise · fully offline

---

## What it does

```
real microphone ──► PulseAudio capture ──► lock-free ring ──► DSP thread
                                                                 │
   ┌─────────────────────────────────────────────────────────────┘
   │  HPF → RNNoise → Gate → EQ → De-Esser → Compressor → Saturation
   │  → Loudness (BS.1770-4) → True-Peak Limiter → Output trim
   ▼
processed stream ──► module-null-sink "VoiceCraft_Output"
                          │ (monitor)
                          ▼
                module-virtual-source "VoiceCraft_Studio_Microphone"
                          │
                          ▼
                     OBS Studio microphone input
```

All modules process **real samples in real time** — there are no mocks,
simulators or UI-only placeholders anywhere in the audio path.

## Measured performance (this repository, automated tests)

| Metric | Value | Where |
|---|---|---|
| Deterministic DSP latency | **25.06 ms** (RNNoise 20 ms + limiter 5 ms), 5.06 ms with NS off | `test_dsp_chain` |
| CPU load, full mono chain @48 kHz | **3.7 % of one core** (27× realtime) | `test_dsp_bench` |
| CPU load, stereo | 8.0 % of one core (12× realtime) | `test_dsp_bench` |
| RNNoise suppression, noise-only −35 dBFS | **−16.4 dB** at 85 % amount | `test_dsp_chain` |
| Limiter accuracy | output peak −1.50 dBFS with ceiling −1.5 dBTP | `test_dsp_chain` |
| Loudness steering | −16.0 LUFS at target −16 LUFS | `test_loudness` |
| E2E tone through the whole chain into the virtual mic | captured, 220 Hz ±5 % | `test_pulse_e2e` |
| Start/stop ×5 | zero leftover devices / modules | `test_pulse_e2e` |

Hardware note: the benchmark ran on a shared cloud vCPU. The worst supported
target (Intel Celeron J1800, 2.4 GHz single core) has roughly 4–6× less
single-thread throughput; 3.7 % × 6 ≈ 22 % of one core — comfortable margin.
`lowCpuMode` additionally restricts RNNoise to channel 0 for stereo setups.

## Building from source

Requirements (Ubuntu 20.04):

```
sudo apt install build-essential cmake qtbase5-dev libpulse-dev \
                 libsamplerate0-dev libsndfile1-dev librnnoise0-dev
```

> **RNNoise warning** — the packaged `librnnoise0` in several distros ships
> **without usable pretrained weights**; `rnnoise_create(nullptr)` then does
> almost nothing (measured 0.1 dB suppression). Build RNNoise from source
> *with the official model*, or verify yours first
> (see TROUBLESHOOTING.md, “RNNoise does nothing”).

```
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

Or with one command (deps + build + tests + both packages):

```
bash build.sh
```

Artifacts: `build/voicecraft-studio` (GUI) and seven test binaries
(`build/test_*`).

## Packages: .deb and AppImage

Both are produced by the build:

| Package | Command | Install / run |
|---|---|---|
| **.deb** | `cpack -G DEB` (or `bash build.sh`) | `sudo apt install ./build/voicecraft-studio-1.0.0-amd64.deb` |
| **AppImage** | `scripts/build_appimage.sh` (or `bash build.sh`) | `./build/voicecraft-studio.AppImage` |

The .deb installs `/usr/bin/voicecraft-studio`, the desktop entry, icon,
docs and a udev rule for realtime audio; it depends on the system Qt5 /
PulseAudio / RNNoise runtime packages (no bundled libraries, standard
distro hygiene).

**glibc compatibility rule** — an AppImage (or binary) can only run on
systems whose glibc is *newer or equal* to the machine that built it.
Build on your Ubuntu 20.04 desktop (`bash build.sh`) to get artifacts that
run on 20.04 and anything newer; prebuilt artifacts generated on newer
distros (Debian 12+, Ubuntu 22.04+) will not start on 20.04.

## Running the test suite

```
cd build
ctest --output-on-failure
```

Two tests need a PulseAudio daemon; `test_pulse_e2e` additionally expects a
null-sink named `FakeMicSink` (created by the scripts in `scripts/` on the
development box) acting as a stand-in microphone. Raw logs from the actual
CI-style run on this repository are stored in `docs/logs/`.

## Using it with OBS

1. Start VoiceCraft Studio, pick your microphone on the **Studio** tab,
   press **START**.
2. In OBS: Settings → Audio → Mic/Auxiliary Input → select
   **“VoiceCraft Studio Microphone”**.
3. What OBS records is exactly the processed stream (post-DSP, pre-volume).

Full walkthrough: `docs/OBS_GUIDE.md`. Problems: `docs/TROUBLESHOOTING.md`.

## Repository layout

```
src/core/      settings, presets, logging, diagnostics
src/audio/     PulseAudio context, capture, virtual microphone, pipeline
src/dsp/       the ten-stage voice chain (all header-documented)
src/autotune/  AUTO STUDIO VOICE sample analyzer
src/recorder/  Recorder (WAV/FLAC) + AudioDocument editor
src/ui/        Qt tabs: Studio / Recording / Mixer / Presets / Settings
tests/         seven automated test programs (see docs/logs/test_log.txt)
scripts/       build + environment helpers used during development
docs/          this guide, OBS guide, troubleshooting, licenses, logs
```

## Development status

Phase 1 (audio foundations + full DSP chain + OBS routing) is complete and
verified by the automated suite. Remaining roadmap: recording workspace
polish, AUTO STUDIO VOICE UI integration, packaging (.deb).
