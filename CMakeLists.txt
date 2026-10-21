cmake_minimum_required(VERSION 3.16)
project(VoiceCraftStudio VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_compile_options(-O2 -Wall -Wextra -Wno-unused-parameter)

# Realtime-safe floating point: no fast-math (needs exact denormal handling via FTZ in code)
add_compile_definitions(
  VOICECRAFT_VERSION="${PROJECT_VERSION}"
)

find_package(Qt5 5.12 REQUIRED COMPONENTS Core Gui Widgets)
find_package(PkgConfig REQUIRED)
pkg_check_modules(PULSE REQUIRED IMPORTED_TARGET libpulse)
pkg_check_modules(PULSE_SIMPLE REQUIRED IMPORTED_TARGET libpulse-simple)
pkg_check_modules(SAMPLERATE REQUIRED IMPORTED_TARGET samplerate)
pkg_check_modules(SNDFILE REQUIRED IMPORTED_TARGET sndfile)
pkg_check_modules(RNNOISE REQUIRED IMPORTED_TARGET rnnoise)

find_package(Threads REQUIRED)

# ---------------------------------------------------------------------------
# Core library: audio backend, DSP engine, presets, recorder, analyzer
# ---------------------------------------------------------------------------
set(VC_CORE_SOURCES
  src/core/Log.cpp
  src/core/Settings.cpp
  src/core/Diagnostics.cpp
  src/core/PresetManager.cpp
  src/audio/RingBuffer.cpp
  src/audio/PulseContext.cpp
  src/audio/AudioSource.cpp
  src/audio/VirtualMicrophone.cpp
  src/audio/AudioPipeline.cpp
  src/dsp/Biquad.cpp
  src/dsp/HighPass.cpp
  src/dsp/NoiseSuppression.cpp
  src/dsp/NoiseGate.cpp
  src/dsp/ParametricEQ.cpp
  src/dsp/DeEsser.cpp
  src/dsp/Compressor.cpp
  src/dsp/Saturation.cpp
  src/dsp/LoudnessMeter.cpp
  src/dsp/LoudnessControl.cpp
  src/dsp/Limiter.cpp
  src/dsp/DSPEngine.cpp
  src/autotune/VoiceAnalyzer.cpp
  src/recorder/Recorder.cpp
  src/recorder/AudioDocument.cpp
)

add_library(voicecraft_core STATIC ${VC_CORE_SOURCES})
target_include_directories(voicecraft_core PUBLIC src ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(voicecraft_core
  PUBLIC Qt5::Core PkgConfig::PULSE PkgConfig::SAMPLERATE PkgConfig::RNNOISE
  PRIVATE PkgConfig::SNDFILE Threads::Threads
)

# ---------------------------------------------------------------------------
# GUI application
# ---------------------------------------------------------------------------
set(VC_GUI_SOURCES
  src/main.cpp
  src/ui/MainWindow.cpp
  src/ui/Theme.cpp
  src/ui/LevelMeter.cpp
  src/ui/WaveformView.cpp
  src/ui/StudioTab.cpp
  src/ui/RecordingTab.cpp
  src/ui/MixerTab.cpp
  src/ui/PresetsTab.cpp
  src/ui/SettingsTab.cpp
  src/ui/AutoVoiceDialog.cpp
)

add_executable(voicecraft-studio ${VC_GUI_SOURCES} resources/resources.qrc)
target_link_libraries(voicecraft-studio PRIVATE voicecraft_core Qt5::Widgets Qt5::Gui Qt5::Core)
set_target_properties(voicecraft-studio PROPERTIES OUTPUT_NAME "voicecraft-studio")

# ---------------------------------------------------------------------------
# Headless test suite
# ---------------------------------------------------------------------------
enable_testing()

add_executable(test_ringbuffer tests/test_ringbuffer.cpp)
target_link_libraries(test_ringbuffer voicecraft_core)
add_test(NAME ringbuffer COMMAND test_ringbuffer)

add_executable(test_dsp_chain tests/test_dsp_chain.cpp)
target_link_libraries(test_dsp_chain voicecraft_core)
add_test(NAME dsp_chain COMMAND test_dsp_chain)

add_executable(test_loudness tests/test_loudness.cpp)
target_link_libraries(test_loudness voicecraft_core)
add_test(NAME loudness COMMAND test_loudness)

add_executable(test_presets tests/test_presets.cpp)
target_link_libraries(test_presets voicecraft_core)
add_test(NAME presets COMMAND test_presets)

add_executable(test_recorder tests/test_recorder.cpp)
target_link_libraries(test_recorder voicecraft_core)
add_test(NAME recorder COMMAND test_recorder)

add_executable(test_pulse_e2e tests/test_pulse_e2e.cpp)
target_link_libraries(test_pulse_e2e voicecraft_core PkgConfig::PULSE_SIMPLE)
add_test(NAME pulse_e2e COMMAND test_pulse_e2e)

add_executable(test_dsp_bench tests/test_dsp_bench.cpp)
target_link_libraries(test_dsp_bench voicecraft_core)
add_test(NAME dsp_bench COMMAND test_dsp_bench)

# ---------------------------------------------------------------------------
# Install rules (used by the .deb package)
# ---------------------------------------------------------------------------
include(GNUInstallDirs)
install(TARGETS voicecraft-studio RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES resources/voicecraft-studio.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES resources/icons/voicecraft-studio.svg
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
install(FILES docs/README.md docs/USER_GUIDE.md docs/OBS_GUIDE.md
        docs/TROUBLESHOOTING.md docs/LICENSES.md
        DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/voicecraft-studio)
install(FILES resources/51-voicecraft-realtime.rules
        DESTINATION /etc/udev/rules.d)
