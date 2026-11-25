[![License: GPL v3](https://img.shields.io/badge/License-GPL%20v3-blue.svg)](https://www.gnu.org/licenses/gpl-3.0.en.html)

# MuseScore JACK Bridge

A VST3 plugin for Linux that forwards the MIDI played by MuseScore to JACK,
together with the play, stop and position of the score (JACK transport).

MuseScore 4 no longer has JACK support. Load this plugin as an instrument in
MuseScore and it appears as a JACK client with a MIDI output port. Connect
that port to any synth, sampler or DAW, and let the DAW follow MuseScore's
playback.

Works with a real JACK server (jackd) and with PipeWire's JACK layer.

> **Status: early.** Tested on one setup (MuseScore 4 with PipeWire and
> Ardour). Expect rough edges. See [Limitations](#limitations).

## Requirements

- A MuseScore 4 build that can load **VST3 plugins on Linux**. The official
  Linux builds cannot; a build with Linux VST3 support is needed.
- JACK or PipeWire (with its JACK support) running.
- To build: JUCE, CMake 3.22 or newer, a C++17 compiler, and the JACK
  development headers (`libjack-jackd2-dev` on Debian/Ubuntu). Only the
  headers are needed; `libjack` itself is loaded at run time.

## Build

```bash
git clone https://github.com/diedeno/MuseScoreJACKBridge.git
cd MuseScoreJACKBridge
cmake -S . -B build -DJUCE_DIR=/path/to/JUCE -DCMAKE_BUILD_TYPE=Release
cmake --build build --target MuseScoreJACKBridge_VST3 --parallel
```

Copy the resulting `MuseScoreJACKBridge.vst3` folder (it is below
`build/MuseScoreJACKBridge_artefacts/`) to `~/.vst3/`.

The repository also contains a GitHub Actions workflow that builds the plugin
on Ubuntu 24.04. Start it from the **Actions** tab and download the zip from
the run's artifacts.

## Use

1. Load **MuseScoreJACKBridge** as the sound of an instrument in MuseScore.
2. A JACK client named `MuseScore-<channel name>` appears with an output port
   called `midi_out`. Connect it with a patchbay such as qpwgraph.
3. Open the plugin window to change the **JACK channel name** (default
   `Channel`). Use one name per instance, for example one per instrument. The
   name is saved with the score. The window shows the name JACK really
   assigned.

### Following MuseScore in a DAW (JACK transport)

Tick **Control JACK transport** in the plugin window. MuseScore's play, stop
and position are then sent to JACK transport. Only **one** instance can do
this; ticking it in another instance takes it over. It is off by default.

In Ardour, choose JACK Transport as the transport master (**Window >
Transport Masters**) so that it follows.

## Notes

- Only MIDI messages up to 3 bytes are forwarded; SysEx is not.
- If JACK is not running, the plugin keeps retrying. If the JACK server
  restarts, it reconnects. Patchbay connections are lost on a restart; save
  and restore them with your patchbay tool.
- `MUSESCORE_JACK_LIB=/path/to/libjack.so.0` forces a specific libjack. By
  default PipeWire's libjack is tried first, then the system one; the first
  that can reach a running server is used.

## Limitations

- MIDI events are written at the start of each JACK period, so timing inside
  a period is not preserved.
- No tempo or bar information is sent to JACK (no timebase).
- Not tested with many instances or long sessions.

## License

GPL-3.0. See `LICENSE`.
