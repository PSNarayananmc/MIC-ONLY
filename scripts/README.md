# Sandbox build & test scripts

These scripts reproduce the headless build/test environment that was used to
develop and validate VoiceCraft Studio (Debian 13 container without root,
user-space dependencies extracted from `.deb` packages into `~/opt`).

On a normal Ubuntu 20.04+ desktop you do **not** need them - install the
system packages listed in the root `README.md` and build with CMake directly.

| Script | Purpose |
|---|---|
| `setup_deps.sh` | Downloads and extracts build dependencies (CMake, Qt 5, libpulse, RNNoise deps) into `~/opt` |
| `build_rnnoise.sh` | Builds RNNoise 0.2 **with the official pretrained model weights** (distro packages ship without them) into `~/opt` |
| `env.sh` | Environment for the user-space prefix (`source scripts/env.sh`) |
| `pa_start.sh` | Starts a headless PulseAudio 17 daemon (with `pa_path_shim.so` path fixups) for automated E2E tests |
| `pa_path_shim.c` | LD_PRELOAD shim that rewrites compiled-in PulseAudio module paths to the user-space prefix |

The curated build and test logs produced with these scripts are kept in
`docs/logs/`.
