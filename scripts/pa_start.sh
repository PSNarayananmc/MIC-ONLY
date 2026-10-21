#!/bin/bash
# Start the headless PulseAudio daemon used by the automated E2E tests.
# Used on the development box (PulseAudio extracted into ~/opt, no root).
#
# The LD_PRELOAD shim rewrites PulseAudio's compiled-in absolute paths
# (/usr/lib/pulse-17.0+dfsg1, /etc/pulse) to the extracted prefix — this is
# required because the daemon is not installed system-wide and /usr is not
# writable. On a normal desktop install, plain `pulseaudio --start` works.
set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/env.sh"
export XDG_RUNTIME_DIR=${XDG_RUNTIME_DIR:-/tmp/xdgrt}
mkdir -p "$XDG_RUNTIME_DIR" && chmod 700 "$XDG_RUNTIME_DIR"

if pactl info >/dev/null 2>&1; then
    echo "PulseAudio already running"
    exit 0
fi

# Build the LD_PRELOAD path shim if missing (user-space PA has compiled-in
# absolute paths pointing at /usr; the shim redirects them into ~/opt).
if [ ! -f "$SCRIPT_DIR/pa_path_shim.so" ]; then
    gcc -shared -fPIC -o "$SCRIPT_DIR/pa_path_shim.so" "$SCRIPT_DIR/pa_path_shim.c" -ldl
fi

mkdir -p /home/z/.config/pulse
cat > /home/z/.config/pulse/default.pa <<'EOF'
# VoiceCraft Studio headless test config: no ALSA hardware probing
load-module module-native-protocol-unix
load-module module-null-sink sink_name=VirtualSpeaker sink_properties=device.description=VirtualSpeaker
load-module module-null-sink sink_name=FakeMicSink sink_properties=device.description=FakeMicSink
EOF

rm -rf "$XDG_RUNTIME_DIR/pulse"
LD_PRELOAD=$SCRIPT_DIR/pa_path_shim.so \
    nohup pulseaudio -n --exit-idle-time=-1 \
    --file=/home/z/.config/pulse/default.pa > /tmp/pa_daemon.log 2>&1 &
sleep 2
pactl info | head -3
echo "---sinks---"; pactl list short sinks
echo "---sources---"; pactl list short sources
