# SDR++ Architecture

## Overview

SDR++ is a cross-platform, modular SDR application. Desktop uses GLFW + ImGui; Android uses NativeActivity + ImGui. All modules (sources, sinks, decoders) are shared libraries loaded at runtime.

## Directory Structure

```
SDRPlusPlus/
├── CMakeLists.txt              # Master build — options for every module
├── sdrpp_module.cmake          # Shared cmake for all modules (creates .so, links sdrpp_core)
│
├── core/                       # Core library (sdrpp_core.so)
│   ├── src/
│   │   ├── core.h/cpp          # App init, args, module loading
│   │   ├── config.h/cpp        # JSON config manager (per-module configs)
│   │   ├── module.h            # Module interface: SDRPP_MOD_INFO, _INIT_, _CREATE_INSTANCE_, etc.
│   │   │
│   │   ├── signal_path/        # Signal routing
│   │   │   ├── source.h/cpp    # SourceManager — registers/selects source modules
│   │   │   ├── sink.h/cpp      # SinkManager — audio output routing
│   │   │   ├── vfo_manager.h/cpp  # VFO (virtual frequency oscillator) management
│   │   │   ├── iq_frontend.h/cpp  # IQ → FFT → display pipeline
│   │   │   └── signal_path.h/cpp  # Top-level signal path wiring
│   │   │
│   │   ├── dsp/                # DSP building blocks (header-only templates)
│   │   │   ├── types.h         # complex_t, stereo_t
│   │   │   ├── stream.h        # Lock-free IQ stream (writeBuf + swap())
│   │   │   ├── buffer/         # Ring buffers
│   │   │   ├── convert/        # Format converters (real↔complex, etc.)
│   │   │   ├── channel/        # Channel processing (freq shift, BW filter)
│   │   │   ├── demod/          # Demodulators (FM, AM, SSB, CW, etc.)
│   │   │   ├── filter/         # FIR/IIR filters, taps
│   │   │   ├── multirate/      # Decimation, interpolation
│   │   │   ├── noise_reduction/# NR algorithms
│   │   │   ├── audio/          # Audio volume, squelch
│   │   │   ├── clock_recovery/ # Symbol timing
│   │   │   ├── correction/     # DC removal, IQ correction
│   │   │   └── routing/        # Splitter, merger
│   │   │
│   │   ├── gui/
│   │   │   ├── gui.h/cpp       # Main GUI loop, menu bar, waterfall
│   │   │   ├── style.h/cpp     # ImGui styling/colors
│   │   │   ├── smgui.h/cpp     # Simplified menu GUI (works in server mode too)
│   │   │   ├── menus/          # Left-panel menus
│   │   │   │   ├── source.h/cpp      # Source selection dropdown
│   │   │   │   ├── display.h/cpp     # FFT size, waterfall colors
│   │   │   │   ├── bandplan.h/cpp    # Band plan overlay
│   │   │   │   ├── sink.h/cpp        # Audio output selection
│   │   │   │   ├── module_manager.h/cpp  # Module load/unload
│   │   │   │   ├── theme.h/cpp       # UI theme selection
│   │   │   │   └── vfo_color.h/cpp   # VFO marker colors
│   │   │   └── widgets/        # Custom ImGui widgets
│   │   │       ├── waterfall.h/cpp    # Main waterfall + spectrum display
│   │   │       ├── frequency_select.h/cpp  # Frequency digit input
│   │   │       ├── bandplan.h/cpp     # Band plan rendering
│   │   │       └── ...
│   │   │
│   │   ├── imgui/              # Embedded Dear ImGui + OpenGL3 backend
│   │   └── utils/              # Logging (flog), networking, etc.
│   │
│   └── backends/               # Platform-specific rendering
│       ├── glfw/               # Desktop: GLFW window + OpenGL context
│       │   └── backend.cpp
│       └── android/            # Android: NativeActivity + EGL + OpenGL ES
│           ├── backend.cpp     # USB device FD management, VID/PID tables
│           └── android_backend.h  # getDeviceFD(), DevVIDPID struct
│
├── source_modules/             # SDR hardware drivers (28 modules)
│   ├── rtl_sdr_source/         # RTL-SDR (U8 IQ via librtlsdr)
│   ├── hackrf_source/          # HackRF One
│   ├── airspy_source/          # Airspy
│   ├── sdrplay_source/         # SDRplay (via proprietary API)
│   ├── plutosdr_source/        # PlutoSDR (via libiio)
│   ├── rtl_tcp_source/         # RTL-SDR over TCP
│   ├── file_source/            # WAV file playback
│   └── ...                     # 21 more sources
│
├── sink_modules/               # Audio output backends
│   ├── audio_sink/             # RtAudio (desktop)
│   ├── android_audio_sink/     # AAudio (Android)
│   ├── portaudio_sink/         # PortAudio
│   └── network_sink/           # Audio over network
│
├── decoder_modules/            # Signal decoders/demodulators
│   ├── radio/                  # AM/FM/SSB/CW/DSB (main decoder)
│   ├── m17_decoder/            # M17 digital voice
│   ├── meteor_demodulator/     # Meteor satellite
│   ├── pager_decoder/          # POCSAG/FLEX pager
│   ├── atv_decoder/            # Analog TV
│   └── ...                     # 6 more decoders
│
├── misc_modules/               # Utility modules
│   ├── recorder/               # IQ + audio recording
│   ├── frequency_manager/      # Bookmarks/frequency database
│   ├── scanner/                # Auto-scan
│   ├── rigctl_server/          # CAT control (hamlib compatible)
│   └── ...
│
├── android/                    # Android app wrapper
│   └── app/
│       ├── build.gradle        # Gradle build, cmake arguments, enabled modules
│       └── src/main/
│           ├── AndroidManifest.xml
│           └── java/
│               ├── MainActivity.kt   # NativeActivity, USB permission, asset extraction
│               └── DeviceManager.kt  # (stub)
│
└── root/                       # Runtime assets (copied to app data dir)
    ├── res/
    │   ├── bandplans/          # ITU band plan JSONs
    │   ├── colormaps/          # Waterfall color LUTs
    │   ├── themes/             # ImGui theme JSONs
    │   ├── fonts/              # TTF fonts
    │   └── icons/              # App icons
    └── modules/                # Empty — modules go here at install time
```

## Module System

Every module is a shared library exporting 4 C functions:

```cpp
MOD_EXPORT void _INIT_()                                        // Load config
MOD_EXPORT ModuleManager::Instance* _CREATE_INSTANCE_(string)   // Create instance
MOD_EXPORT void _DELETE_INSTANCE_(Instance*)                    // Destroy instance
MOD_EXPORT void _END_()                                        // Save config
```

Module metadata declared via `SDRPP_MOD_INFO`:
```cpp
SDRPP_MOD_INFO {
    /* Name:        */ "my_source",
    /* Description: */ "My SDR source",
    /* Author:      */ "author",
    /* Version:     */ 0, 1, 0,
    /* Max instances*/ 1
};
```

## Source Module Pattern

A source module must:
1. Create a `dsp::stream<dsp::complex_t> stream` for IQ output
2. Register with `sigpath::sourceManager.registerSource("Name", &handler)`
3. Implement the `SourceHandler` callbacks:
   - `selectHandler` — called when source is selected in UI
   - `deselectHandler` — called when source is deselected
   - `menuHandler` — draws ImGui controls in the source menu
   - `startHandler` — open device, start IQ streaming
   - `stopHandler` — stop streaming, close device
   - `tuneHandler` — set center frequency

IQ data flow:
```
USB async callback → convert to float complex_t → stream.writeBuf[i] → stream.swap(count)
                                                                           ↓
                                                              iq_frontend → FFT → waterfall
                                                                           ↓
                                                              vfo_manager → demod → audio sink
```

## Android USB Flow

1. `MainActivity.kt` enumerates all USB devices, requests permission for each
2. Permission granted → opens `UsbDeviceConnection`, stores FD/VID/PID in public fields
3. Native modules call `backend::getDeviceFD(vid, pid, VIDPID_TABLE)` to get the FD
4. Module passes FD to driver's `open_fd()` or `open_sys_dev()` function
5. Driver uses libusb with the FD (no enumeration needed on Android)

VIDPIDs are defined in `core/backends/android/backend.cpp`:
```cpp
const std::vector<DevVIDPID> RTL_SDR_VIDPIDS = { {0x0bda, 0x2832}, ... };
```

## Build System

- Top-level `CMakeLists.txt` has `option()` for every module (ON/OFF)
- `sdrpp_module.cmake` creates the shared lib, links sdrpp_core, sets install dir
- Module's own `CMakeLists.txt` adds platform-specific library links
- Android build: `android/app/build.gradle` passes cmake arguments to enable/disable modules

## Key Data Types

```cpp
dsp::complex_t { float re, im; }  // IQ sample
dsp::stream<T>                     // Thread-safe ring buffer with writeBuf + swap()
ConfigManager                      // JSON config per-module with acquire/release locking
```
