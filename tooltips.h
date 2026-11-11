#pragma once
#include "resource.h"

struct TipEntry {
    int id;
    const wchar_t *text;
};

inline constexpr TipEntry kTooltips[] = {

    // ---- WASAPI mode ----
    {IDC_MODE_SHARED, L"Shared mode: Windows mixes this audio with sound from other programs. It works with any device "
                      L"and lets other apps play at the same time, but isn't bit-perfect."},

    {IDC_MODE_EXCLUSIVE, L"Exclusive mode: foobar2000 takes direct control of the device, so audio is sent as-is without "
                         L"Windows mixing, resampling, or dithering. Other programs cannot use the device while it plays. "
                         L"Use this for bit-perfect output."},

    // ---- Options ----
    {IDC_CHK_APPVOLUME, L"Use foobar2000's own volume control instead of the Windows volume control. In shared mode this "
                        L"changes only this program's volume; in exclusive mode the volume is applied in software. "
                        L"This option isn't user-configurable when using Exclusive mode."},

    {IDC_CHK_GAPLESS, L"Keeps already-queued audio when the next track starts, so there are no gaps between tracks. "
                      L"If disabled, the output is flushed at every track change."},

    {IDC_CHK_AUTODISCONNECT, L"If this is enabled, other programs can take control of the audio device if foobar2000 isn't"
                             L"actively playing audio. Disable this if you want foobar2000 to lock the audio device even"
                             L"when nothing is being played."},

    {IDC_CHK_MONO2STEREO, L"Upmixes mono to stereo from within foobar2000."},

    {IDC_CHK_CHECKFORMAT, L"Asks the device whether it supports the audio format (IAudioClient::IsFormatSupported) before "
                          L"opening it. Some drivers answer incorrectly, so this isn't always desired."},

    {IDC_CHK_WASAPICLOCK, L"Determines whether the audio device or Windows' built-in high resolution timer is used for"
                          L"timing/clocking."},

    {IDC_CHK_16AS24, L"Force 16-bit audio to be output as 24-bit if enabled.. The sound itself is unchanged."},

    {IDC_CHK_MATCHRATE, L"If enabled, foobar2000's built-in resampler will be used to convert all audio to the sample rate "
                        L"set for this device in Windows (Sound settings > Device Properties > Advanced > Default Format) "
                        L"Avoids Windows' own resampling in shared mode and rate switching in exclusive mode"
                        L"mode. If the rate cannot be read, audio is left unchanged. Takes effect the next time playback starts."
                        L"Don't enable this if you wish to use a separate resampler (for example, a resampling DSP plugin)."},

    {IDC_CHK_SUPPRESSDIAG, L"If enabled, minor errors will be reported in a pop-up. Serious errors, "
                           L"such as being unable to initialize the device, are always shown regardless of this setting."},

    // ---- Device period ----
    {IDC_PERIOD_DEFAULT, L"Uses the device's default period: how often the device asks for more audio. A safe choice "
                         L"for most hardware. Also sometimes referred to as standard latency."},

    {IDC_PERIOD_MIN, L"Low latency mode. Uses more CPU. Can cause crackling or dropouts on some systems."},

    // ---- Buffers ----
    {IDC_SLD_BUFFER, L"Size of the device buffer, as a multiple of the device period. Larger values tolerate hiccups "
                     L"better but add delay; smaller values lower the delay. Default: 7.0"},

    {IDC_SLD_RING, L"Size of foobar2000's own queue in front of the device buffer, as a multiple of the device buffer. "
                   L"Larger values smooth over slow moments in the player. Default: 5.00"},

    {IDC_SLD_START, L"How much audio must be queued, as a multiple of the device buffer, before playback starts. "
                    L"Higher values start more safely but slightly later. Default: 1.00"},

};