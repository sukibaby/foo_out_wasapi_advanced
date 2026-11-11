#pragma once
#include "resource.h"

struct TipEntry {
    int id;
    const wchar_t* text;
};

inline constexpr TipEntry kTooltips[] = {

    // ---- WASAPI mode ----
    { IDC_MODE_SHARED,
      L"Shared mode: Windows mixes this audio with sound from other programs. It works with any device "
      L"and lets other apps play at the same time, but Windows may convert the audio to the format "
      L"set in the Windows sound settings." },

    { IDC_MODE_EXCLUSIVE,
      L"Exclusive mode: foobar2000 takes direct control of the device, so audio is sent as-is without "
      L"Windows mixing or resampling. Other programs cannot use the device while it plays. "
      L"The device must support the audio format being played." },

    // ---- Options ----
    { IDC_CHK_APPVOLUME,
      L"Use foobar2000's own volume control instead of the Windows volume control. In shared mode this "
      L"changes only this program's volume; in exclusive mode the volume is applied in software. "
      L"Always enabled in exclusive mode, because Windows volume does not apply there." },

    { IDC_CHK_GAPLESS,
      L"Keeps already-queued audio when the next track starts, so there is no gap between tracks. "
      L"If disabled, the output is flushed at every track change." },

    { IDC_CHK_AUTODISCONNECT,
      L"Releases the audio device after playback has been paused for a few seconds, so other programs can "
      L"use it. Playback reconnects automatically when you resume." },

    { IDC_CHK_MONO2STEREO,
      L"Plays mono (single channel) audio on both the left and right channels. Useful for devices that "
      L"do not accept mono streams, or when mono audio would otherwise come out of one speaker only." },

    { IDC_CHK_CHECKFORMAT,
      L"Asks the device whether it supports the audio format (IAudioClient::IsFormatSupported) before "
      L"opening it. Some drivers answer incorrectly; turn this off to try opening the device directly." },

    { IDC_CHK_WASAPICLOCK,
      L"Measures playback position and latency using the audio device's own clock, which follows what you "
      L"actually hear. If unchecked, the system's high-resolution timer is used instead." },

    { IDC_CHK_16AS24,
      L"Sends 16-bit audio to the device as 24-bit (exclusive mode). The sound itself is unchanged; this "
      L"only helps devices or drivers that work better with, or only accept, 24-bit." },

    { IDC_CHK_MATCHRATE,
      L"Converts all audio to the sample rate set for this device in Windows (Sound settings > device "
      L"Properties > Advanced > Default Format), using foobar2000's resampler. Every track is then played at "
      L"one steady rate, which avoids Windows' own resampling in shared mode and rate switching in exclusive "
      L"mode. If the rate cannot be read, audio is left unchanged. Takes effect the next time playback starts." },

    { IDC_CHK_SUPPRESSDIAG,
      L"Hides error popups for minor, non-critical problems (for example a buffer query failing as a stream "
      L"ends abruptly). Playback just stops quietly and the details are written to the foobar2000 console. "
      L"Serious errors, such as being unable to initialize the device, are always shown." },

    // ---- Device period ----
    { IDC_PERIOD_DEFAULT,
      L"Uses the device's default period: how often the device asks for more audio. A safe choice "
      L"for most hardware." },

    { IDC_PERIOD_MIN,
      L"Uses the smallest period the device supports for the lowest possible delay. Uses more CPU "
      L"and can cause crackling or dropouts on some systems." },

    // ---- Buffers ----
    { IDC_SLD_BUFFER,
      L"Size of the device buffer, as a multiple of the device period. Larger values tolerate hiccups "
      L"better but add delay; smaller values lower the delay. Default: 7.0" },

    { IDC_SLD_RING,
      L"Size of foobar2000's own queue in front of the device buffer, as a multiple of the device buffer. "
      L"Larger values smooth over slow moments in the player. Default: 5.00" },

    { IDC_SLD_START,
      L"How much audio must be queued, as a multiple of the device buffer, before playback starts. "
      L"Higher values start more safely but slightly later. Default: 1.00" },

};