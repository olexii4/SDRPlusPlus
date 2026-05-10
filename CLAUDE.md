# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build Commands

### macOS / Linux

```sh
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug   # or Release
make -j$(nproc)
```

To enable the MSI-SDR source (off by default on desktop):
```sh
cmake .. -DOPT_BUILD_MIRISDR_SOURCE=ON
make -j$(nproc)
```

### First-time dev setup (after build)

```sh
sh ./create_root.sh              # Creates root_dev/ from root/
./build/sdrpp -r root_dev/       # Run once to generate root_dev/config.json
```

Then edit `root_dev/config.json` to list the built `.so`/`.dylib` modules:
```json
"modules": [
    "./build/source_modules/mirisdr_source/mirisdr_source.so",
    "./build/sink_modules/audio_sink/audio_sink.so",
    "./build/decoder_modules/radio/radio.so"
]
```

Run for development:
```sh
./build/sdrpp -r root_dev
```

### Android

Build via Android Studio with Gradle, or from the CLI:
```sh
cd android && ./gradlew assembleDebug
```

The `android/app/build.gradle` passes `-DOPT_BUILD_*` cmake arguments to enable/disable modules for Android. The `sdr-kit/arm64-v8a/` directory contains prebuilt ARM64 libraries that Android modules link against.

## Architecture

The full directory/component map is in `docs/architecture.md`. Key patterns:

**Module system:** Every source, sink, decoder, and misc module is a shared library (`.so`/`.dylib`/`.dll`) loaded at runtime. Each exports four C functions: `_INIT_`, `_CREATE_INSTANCE_`, `_DELETE_INSTANCE_`, `_END_`. The module declares metadata via `SDRPP_MOD_INFO`.

**Source module pattern:** A source module creates a `dsp::stream<dsp::complex_t> stream`, registers it with `sigpath::sourceManager.registerSource("Name", &handler)`, and implements five callbacks: `selectHandler`, `deselectHandler`, `menuHandler`, `startHandler`, `stopHandler`, `tuneHandler`. IQ data is pushed by calling `stream.writeBuf[i] = sample; stream.swap(count)` from the device callback thread.

**GUI:** Desktop uses GLFW + OpenGL + Dear ImGui (embedded in `core/src/imgui/`). Module UI is drawn in `menuHandler` using ImGui calls directly. `SmGui` (`core/src/gui/smgui.h`) is a thin wrapper that works in both GUI and server (headless) mode — prefer `SmGui` over raw `ImGui` in module menus.

**Config:** Each module has its own `ConfigManager` instance. Use `config.acquire()` / `config.release(modified)` for thread-safe access to the JSON config file. Config files live in the root directory passed via `-r`.

**Android USB flow:** `MainActivity.kt` requests USB permissions and stores the open FD. Native code calls `backend::getDeviceFD(vid, pid, VIDPID_TABLE)` to retrieve it, then passes the FD to the driver's `open_fd()` function. VID/PID tables for Android are defined per-module (e.g., `MIRISDR_VIDPIDS` in `mirisdr_source`).

**DSP:** All DSP blocks are in `core/src/dsp/` as header-only C++ templates. `dsp::stream<T>` is a lock-free ring buffer; producers call `swap(count)`, consumers call `read()` / `flush()`.

## mirisdr_source Module

Located at `source_modules/mirisdr_source/`. This module is a fork-specific addition that drives MSi2500-based devices (MSi2500, SDRplay RSP1/RSP1A/RSP2) using a bundled `libmirisdr` (no system dependency). On desktop it uses system `libusb-1.0` (via pkg-config); on Android it builds libusb from source. Enable with `-DOPT_BUILD_MIRISDR_SOURCE=ON`.

The module is enabled in the Android build in `android/app/build.gradle` and the desktop build uses the existing `build/` directory configuration.

## Adding a New Module

1. Create `source_modules/<name>/` with `CMakeLists.txt` and `src/main.cpp`
2. Use `include(${SDRPP_MODULE_CMAKE})` in the module's CMakeLists to get the shared lib boilerplate
3. Add an `option(OPT_BUILD_<NAME> ...)` and `add_subdirectory` guard in the root `CMakeLists.txt`
4. Implement `SDRPP_MOD_INFO`, `_INIT_`, `_CREATE_INSTANCE_`, `_DELETE_INSTANCE_`, `_END_`
5. For Android: add the `-DOPT_BUILD_<NAME>=ON/OFF` flag in `android/app/build.gradle` and define a VID/PID table

## No Tests

There is no test suite. Verification is done by running the application and testing hardware.
