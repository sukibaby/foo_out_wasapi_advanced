#pragma once
#include <foobar2000/SDK/foobar2000.h>

// Persistent settings for the WASAPI output. Floats are stored as integers (x100).
namespace wcfg {

enum { mode_shared = 0, mode_exclusive = 1 };

extern cfg_int cfg_mode;            // mode_shared / mode_exclusive
extern cfg_int cfg_app_volume;      // use app volume control instead of Windows volume
extern cfg_int cfg_gapless;         // gapless mode
extern cfg_int cfg_auto_disconnect; // release device when idle
extern cfg_int cfg_mono_to_stereo;
extern cfg_int cfg_check_format;    // call IsFormatSupported before Initialize
extern cfg_int cfg_clock_wasapi;    // 1 = IAudioClock is the clock, 0 = system (QPC)
extern cfg_int cfg_16_as_24;        // play 16-bit streams as 24-bit
extern cfg_int cfg_match_rate;      // resample to the sample rate configured for the device in Windows
extern cfg_int cfg_suppress_diag;   // do not show error popups for non-critical (diagnostic) failures
extern cfg_int cfg_period_min;      // 0 = default device period, 1 = minimum
extern cfg_int cfg_buffer_x100;     // buffer multiplier of period (1.0 .. 20.0, default 7.0)
extern cfg_int cfg_ring_x100;       // ring buffer, multiplier of device buffer (1.0 .. 40.0, default 5.0)
extern cfg_int cfg_start_x100;      // start-playing threshold, multiplier of device buffer (1.0 .. 40.0, default 1.0)

// Used for a fresh install (config.cpp) and by the "Reset page" button (preferences.cpp).
constexpr int kDefMode           = mode_shared;  // mode_shared / mode_exclusive
constexpr int kDefAppVolume      = 0;  // Enable volume control (forced on in exclusive mode)
constexpr int kDefGapless        = 1;  // Gapless mode playback
constexpr int kDefAutoDisconnect = 1;  // Automatically disconnect when idle
constexpr int kDefMonoToStereo   = 0;  // Convert mono to stereo
constexpr int kDefCheckFormat    = 0;  // Call IsFormatSupported before Initialize
constexpr int kDefClockWasapi    = 1;  // 1 = WASAPI clock, 0 = system clock
constexpr int kDef16As24         = 0;  // Play 16 bit streams as 24 bit
constexpr int kDefMatchRate      = 0;  // Automatically match device's configured sample rate
constexpr int kDefSuppressDiag   = 1;  // Suppress non-critical (diagnostic) error messages
constexpr int kDefPeriodMin      = 0;  // 0 = default device period, 1 = minimum
// Sliders (x100): buffer multiplier, ring buffer, start-playing threshold
constexpr int kBufferMin = 100,  kBufferMax = 2000, kBufferDef = 700;
constexpr int kRingMin   = 100,  kRingMax   = 4000, kRingDef   = 500;
constexpr int kStartMin  = 100,  kStartMax  = 4000, kStartDef  = 100;

constexpr DWORD kIdleDisconnectMs = 5000;

inline double clampd(double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline double buffer_mult() { return clampd(cfg_buffer_x100 / 100.0, kBufferMin / 100.0, kBufferMax / 100.0); }
inline double ring_mult()   { return clampd(cfg_ring_x100 / 100.0,   kRingMin / 100.0,   kRingMax / 100.0); }
inline double start_mult()  { return clampd(cfg_start_x100 / 100.0,  kStartMin / 100.0,  kStartMax / 100.0); }

} // namespace wcfg
