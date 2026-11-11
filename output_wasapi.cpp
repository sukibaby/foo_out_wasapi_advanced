// WASAPI output for foobar2000 (shared / exclusive), configurable via preferences page.
#include <foobar2000/helpers/foobar2000+atl.h>
#include <initguid.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <avrt.h>
#include <functiondiscoverykeys_devpkey.h>
#include <vector>
#include <cmath>
#include <cstring>
#include "config.h"

#pragma comment(lib, "avrt.lib")
#pragma comment(lib, "ole32.lib")

using namespace wcfg;

// {7b2d9f64-3a0c-4c55-8e1a-52d1c9a7e0f3}
static const GUID guid_output_wasapi = {0x7b2d9f64, 0x3a0c, 0x4c55, {0x8e, 0x1a, 0x52, 0xd1, 0xc9, 0xa7, 0xe0, 0xf3}};
// {7b2d9f64-3a0c-4c55-8e1a-52d1c9a7e0f4}  -- "default device" entry
static const GUID guid_default_device = {0x7b2d9f64, 0x3a0c, 0x4c55, {0x8e, 0x1a, 0x52, 0xd1, 0xc9, 0xa7, 0xe0, 0xf4}};

static const GUID kSubPCM = {0x00000001, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};
static const GUID kSubFloat = {0x00000003, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};

static pfc::string8 to_utf8(const wchar_t *w) {
    pfc::string8 out;
    out = pfc::stringcvt::string_utf8_from_wide(w).get_ptr();
    return out;
}

static GUID endpoint_guid(const wchar_t *id) {
    pfc::string8 u = to_utf8(id);
    return hasher_md5::get()->process_single_string(u).asGUID();
}

// Resolves the endpoint selected in foobar2000 (or the default one). Returns nullptr on success,
// otherwise a short description of what failed.
static const char *find_endpoint(const GUID &guid, CComPtr<IMMDevice> &out) {
    out.Release();
    CComPtr<IMMDeviceEnumerator> en;
    if (FAILED(en.CoCreateInstance(__uuidof(MMDeviceEnumerator))))
        return "Cannot create device enumerator";
    if (guid == guid_default_device || guid == pfc::guid_null) {
        if (FAILED(en->GetDefaultAudioEndpoint(eRender, eConsole, &out)))
            return "No default render device";
        return nullptr;
    }
    CComPtr<IMMDeviceCollection> col;
    if (FAILED(en->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &col)))
        return "Cannot enumerate devices";
    UINT n = 0;
    col->GetCount(&n);
    for (UINT i = 0; i < n; ++i) {
        CComPtr<IMMDevice> d;
        if (FAILED(col->Item(i, &d)))
            continue;
        LPWSTR id = nullptr;
        if (FAILED(d->GetId(&id)))
            continue;
        const bool match = (endpoint_guid(id) == guid);
        CoTaskMemFree(id);
        if (match) {
            out = d;
            return nullptr;
        }
    }
    return "Selected WASAPI device not found";
}

// The sample rate configured for the device in Windows (Sound > Properties > Advanced > Default Format),
// which is the format of the shared-mode mix. Returns 0 if it cannot be determined.
static unsigned query_device_mix_rate(const GUID &deviceGuid) {
    CComPtr<IMMDevice> dev;
    if (find_endpoint(deviceGuid, dev))
        return 0;
    CComPtr<IAudioClient> client;
    if (FAILED(dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void **)&client)))
        return 0;
    WAVEFORMATEX *mix = nullptr;
    if (FAILED(client->GetMixFormat(&mix)) || !mix)
        return 0;
    const unsigned rate = mix->nSamplesPerSec;
    CoTaskMemFree(mix);
    return (rate >= 8000 && rate <= 768000) ? rate : 0;
}

// Errors that are always shown versus errors that may be hidden by "Suppress diagnostic error messages".
//  Fatal      - the device could not be set up at all (not found, cannot activate, Initialize failed, ...).
//  Diagnostic - a secondary failure while finishing setup (buffer size / event / render client queries).
enum class ErrorLevel { Fatal, Diagnostic };

struct SampleFmt {
    int container;
    int valid;
    bool isFloat;
};

class output_wasapi : public output_impl {
  public:
    output_wasapi(const GUID &device, double /*buffer_length*/, bool /*dither*/, t_uint32 bitdepth)
        : m_deviceGuid(device), m_bitdepth(bitdepth) {
        InitializeCriticalSection(&m_lock);
        m_stopEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
        m_bufEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        QueryPerformanceFrequency(&m_qpcFreq);
    }

    ~output_wasapi() {
        if (m_thread) {
            SetEvent(m_stopEvent);
            WaitForSingleObject(m_thread, INFINITE);
            CloseHandle(m_thread);
        }
        EnterCriticalSection(&m_lock);
        release_client();
        LeaveCriticalSection(&m_lock);
        CloseHandle(m_stopEvent);
        CloseHandle(m_bufEvent);
        DeleteCriticalSection(&m_lock);
    }

    // ---- output_entry statics ----
    static GUID g_get_guid() { return guid_output_wasapi; }
    static const char *g_get_name() { return "WASAPI"; }
    static bool g_advanced_settings_query() { return false; }
    static bool g_needs_bitdepth_config() { return true; }
    static bool g_needs_dither_config() { return false; }
    static bool g_needs_device_list_prefixes() { return false; }
    static bool g_supports_multiple_streams() { return false; }
    static bool g_is_high_latency() { return false; }

    static void g_enum_devices(output_device_enum_callback &cb) {
        cb.on_device(guid_default_device, "Default Device", 14);
        CComPtr<IMMDeviceEnumerator> en;
        if (FAILED(en.CoCreateInstance(__uuidof(MMDeviceEnumerator))))
            return;
        CComPtr<IMMDeviceCollection> col;
        if (FAILED(en->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &col)))
            return;
        UINT n = 0;
        col->GetCount(&n);
        for (UINT i = 0; i < n; ++i) {
            CComPtr<IMMDevice> dev;
            if (FAILED(col->Item(i, &dev)))
                continue;
            LPWSTR id = nullptr;
            if (FAILED(dev->GetId(&id)))
                continue;
            GUID g = endpoint_guid(id);
            CoTaskMemFree(id);
            CComPtr<IPropertyStore> ps;
            if (FAILED(dev->OpenPropertyStore(STGM_READ, &ps)))
                continue;
            PROPVARIANT v;
            PropVariantInit(&v);
            if (SUCCEEDED(ps->GetValue(PKEY_Device_FriendlyName, &v)) && v.vt == VT_LPWSTR) {
                pfc::string8 name = to_utf8(v.pwszVal);
                cb.on_device(g, name.c_str(), (unsigned)name.length());
            }
            PropVariantClear(&v);
        }
    }

    // ---- output_impl ----
    void open(audio_chunk::spec_t const &spec) {
        EnterCriticalSection(&m_lock);
        m_rate = spec.sampleRate;
        m_srcCh = spec.chanCount;
        m_devCh = (m_srcCh == 1 && cfg_mono_to_stereo) ? 2 : m_srcCh;
        m_expandMono = (m_devCh != m_srcCh);
        uint32_t mask = spec.chanMask;
        if (m_expandMono)
            mask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT;
        else if (mask == 0)
            mask = audio_chunk::g_guess_channel_config(m_devCh);
        m_mask = mask;
        m_exclusive = (cfg_mode == mode_exclusive);
        bool ok = init_client();
        LeaveCriticalSection(&m_lock);
        if (!ok)
            throw_open_failure();

        m_thread = CreateThread(nullptr, 0, &output_wasapi::thread_proc, this, 0, nullptr);
    }

    // Fatal errors are always reported to the user. Diagnostic-level errors can be hidden with the
    // "Suppress diagnostic error messages" option: exception_output_interrupted is the SDK's
    // "do not complain to user" exception, and the details go to the foobar2000 console instead.
    [[noreturn]] void throw_open_failure() {
        if (m_lastErrorLevel == ErrorLevel::Diagnostic && cfg_suppress_diag) {
            FB2K_console_formatter() << "WASAPI: " << m_lastError.c_str() << " (error popup suppressed)";
            throw exception_output_interrupted();
        }
        throw exception_io_data(m_lastError.c_str());
    }

    // Lets foobar2000 resample to the rate configured for this device in Windows. A nonzero return makes the
    // core inject its resampler in front of this output; zero leaves the stream at its own rate.
    unsigned get_forced_sample_rate() override {
        if (!cfg_match_rate)
            return 0;
        if (!m_deviceRateQueried) {
            m_deviceRate = query_device_mix_rate(m_deviceGuid);
            m_deviceRateQueried = true;
            if (m_deviceRate)
                FB2K_console_formatter() << "WASAPI: matching the device's configured sample rate, " << m_deviceRate << " Hz";
            else
                FB2K_console_formatter() << "WASAPI: could not read the device's configured sample rate, leaving it unchanged";
        }
        return m_deviceRate;
    }

    void write(const audio_chunk &chunk) {
        EnterCriticalSection(&m_lock);
        const audio_sample *in = chunk.get_data();
        size_t frames = chunk.get_sample_count();
        size_t space = m_ringFrames - m_ringCount;
        if (frames > space)
            frames = space; // foobar normally respects can_write_samples()
        for (size_t i = 0; i < frames; ++i) {
            size_t wr = (m_ringRead + m_ringCount) % m_ringFrames;
            float *dst = &m_ring[wr * m_devCh];
            if (m_expandMono) {
                dst[0] = dst[1] = (float)in[i];
            } else
                for (uint32_t c = 0; c < m_srcCh; ++c)
                    dst[c] = (float)in[i * m_srcCh + c];
            ++m_ringCount;
        }
        m_lastActivity = GetTickCount();
        try_start();
        LeaveCriticalSection(&m_lock);
    }

    t_size can_write_samples() {
        EnterCriticalSection(&m_lock);
        t_size n = m_ringFrames - m_ringCount;
        LeaveCriticalSection(&m_lock);
        return n;
    }

    t_size get_latency_samples() {
        EnterCriticalSection(&m_lock);
        t_size n = m_ringCount;
        if (m_client && m_started) {
            uint64_t played = played_frames();
            if (m_submitted > played)
                n += (t_size)(m_submitted - played);
        }
        LeaveCriticalSection(&m_lock);
        return n;
    }

    void on_update() {}

    void pause(bool state) {
        EnterCriticalSection(&m_lock);
        m_paused = state;
        if (state) {
            stop_client();
            m_pauseTick = GetTickCount();
        } else {
            if (!m_client) {
                if (!init_client()) {
                    LeaveCriticalSection(&m_lock);
                    return;
                }
            }
            m_forceStart = true;
            try_start();
        }
        LeaveCriticalSection(&m_lock);
    }

    // output_impl defers this until its own queue is empty (end of stream / nothing more coming).
    void on_force_play() {
        EnterCriticalSection(&m_lock);
        m_forceStart = true;
        try_start();
        LeaveCriticalSection(&m_lock);
    }

    // Must be overridden: the SDK's default implementation calls get_latency_info(), which calls back into
    // get_latency_samples_v2() -> is_progressing() and recurses until the stack overflows.
    // Reports whether the WASAPI client is actually running right now.
    bool is_progressing() {
        EnterCriticalSection(&m_lock);
        const bool playing = (m_client != nullptr) && m_started && !m_paused;
        LeaveCriticalSection(&m_lock);
        return playing;
    }

    void on_flush() {
        EnterCriticalSection(&m_lock);
        if (m_client) {
            stop_client();
            m_client->Reset();
        }
        m_ringRead = m_ringCount = 0;
        m_submitted = 0;
        m_sysAccum = 0;
        m_started = false;
        m_forceStart = false;
        LeaveCriticalSection(&m_lock);
    }

    void on_flush_changing_track() {
        // Gapless: keep already-queued audio so the next track follows seamlessly.
        if (cfg_gapless)
            return;
        on_flush();
    }

    void volume_set(double dB) {
        EnterCriticalSection(&m_lock);
        if (app_volume_enabled()) {
            m_gain = (float)std::pow(10.0, dB / 20.0);
            if (m_simpleVol)
                m_simpleVol->SetMasterVolume(m_gain, nullptr);
        } else {
            m_gain = 1.0f;
        }
        LeaveCriticalSection(&m_lock);
    }

  private:
    // Exclusive mode bypasses the Windows mixer, so app-side volume is always required there.
    bool app_volume_enabled() const { return m_exclusive || cfg_mode == mode_exclusive || cfg_app_volume; }

    // ---------- device / client setup (call with lock held) ----------
    bool activate_device() {
        if (const char *err = find_endpoint(m_deviceGuid, m_dev))
            return fail(ErrorLevel::Fatal, err);
        return true;
    }

    // Records why opening failed; always returns false so callers can "return fail(...)".
    bool fail(ErrorLevel level, const char *msg) {
        m_lastError = msg;
        m_lastErrorLevel = level;
        return false;
    }
    bool fail_hr(ErrorLevel level, const char *what, HRESULT hr) {
        char msg[128];
        snprintf(msg, sizeof(msg), "%s (0x%08X)", what, (unsigned)hr);
        return fail(level, msg);
    }

    std::vector<SampleFmt> candidate_formats() const {
        std::vector<SampleFmt> v;
        if (!m_exclusive) {
            v.push_back({4, 32, true});
            return v;
        }
        int bd = (int)m_bitdepth;
        if (bd != 16 && bd != 24 && bd != 32)
            bd = 24;
        if (bd == 16 && cfg_16_as_24)
            bd = 24;
        auto add = [&](SampleFmt f) {
            for (auto &e : v)
                if (e.container == f.container && e.valid == f.valid && e.isFloat == f.isFloat)
                    return;
            v.push_back(f);
        };
        if (bd == 16)
            add({2, 16, false});
        if (bd == 24) {
            add({3, 24, false});
            add({4, 24, false});
        }
        if (bd == 32) {
            add({4, 32, false});
            add({4, 32, true});
        }
        // fallbacks
        add({4, 32, false});
        add({3, 24, false});
        add({4, 24, false});
        add({2, 16, false});
        add({4, 32, true});
        return v;
    }

    WAVEFORMATEXTENSIBLE make_format(const SampleFmt &f) const {
        WAVEFORMATEXTENSIBLE w = {};
        w.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
        w.Format.nChannels = (WORD)m_devCh;
        w.Format.nSamplesPerSec = m_rate;
        w.Format.wBitsPerSample = (WORD)(f.container * 8);
        w.Format.nBlockAlign = (WORD)(m_devCh * f.container);
        w.Format.nAvgBytesPerSec = w.Format.nBlockAlign * m_rate;
        w.Format.cbSize = 22;
        w.Samples.wValidBitsPerSample = (WORD)f.valid;
        w.dwChannelMask = m_mask;
        w.SubFormat = f.isFloat ? kSubFloat : kSubPCM;
        return w;
    }

    bool init_client() {
        release_client();
        for (const SampleFmt &f : candidate_formats()) {
            if (try_init_with(f))
                return true;
        }
        if (m_lastError.is_empty())
            fail(ErrorLevel::Fatal, "Unable to initialize WASAPI with any supported format");
        return false;
    }

    bool try_init_with(const SampleFmt &f) {
        for (int attempt = 0; attempt < 2; ++attempt) {
            if (!activate_device())
                return false;
            m_client.Release();
            if (FAILED(m_dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void **)&m_client))) {
                return fail(ErrorLevel::Fatal, "Cannot activate IAudioClient");
            }
            WAVEFORMATEXTENSIBLE wf = make_format(f);
            AUDCLNT_SHAREMODE sm = m_exclusive ? AUDCLNT_SHAREMODE_EXCLUSIVE : AUDCLNT_SHAREMODE_SHARED;

            if (cfg_check_format) {
                WAVEFORMATEX *closest = nullptr;
                HRESULT hr = m_client->IsFormatSupported(sm, &wf.Format, m_exclusive ? nullptr : &closest);
                if (closest)
                    CoTaskMemFree(closest);
                // In shared mode we ask for auto-conversion, so S_FALSE is acceptable.
                if (hr != S_OK && !(!m_exclusive && hr == S_FALSE)) {
                    m_client.Release();
                    return fail(ErrorLevel::Fatal, "Format not supported (IsFormatSupported)");
                }
            }

            REFERENCE_TIME defP = 0, minP = 0;
            m_client->GetDevicePeriod(&defP, &minP);
            REFERENCE_TIME period = cfg_period_min ? minP : defP;
            REFERENCE_TIME dur = (REFERENCE_TIME)(period * buffer_mult());
            if (m_alignedDuration)
                dur = m_alignedDuration;

            DWORD flags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK;
            if (!m_exclusive)
                flags |= AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;

            HRESULT hr = m_client->Initialize(sm, flags, dur, m_exclusive ? dur : 0, &wf.Format, nullptr);
            if (hr == AUDCLNT_E_BUFFER_SIZE_NOT_ALIGNED && m_exclusive && attempt == 0) {
                UINT32 frames = 0;
                m_client->GetBufferSize(&frames);
                m_alignedDuration = (REFERENCE_TIME)(10000000.0 * frames / m_rate + 0.5);
                continue; // re-activate and retry with the aligned duration
            }
            if (FAILED(hr)) {
                m_client.Release();
                return fail_hr(ErrorLevel::Fatal, "IAudioClient::Initialize failed", hr);
            }
            m_fmt = f;
            m_alignedDuration = 0;
            break;
        }

        // The device itself initialized; failures from here on are secondary (diagnostic level).
        HRESULT hr2 = m_client->SetEventHandle(m_bufEvent);
        if (FAILED(hr2)) {
            m_client.Release();
            return fail_hr(ErrorLevel::Diagnostic, "SetEventHandle failed", hr2);
        }
        hr2 = m_client->GetBufferSize(&m_bufFrames);
        if (FAILED(hr2)) {
            m_client.Release();
            return fail_hr(ErrorLevel::Diagnostic, "GetBufferSize failed", hr2);
        }
        hr2 = m_client->GetService(__uuidof(IAudioRenderClient), (void **)&m_render);
        if (FAILED(hr2)) {
            m_client.Release();
            return fail_hr(ErrorLevel::Diagnostic, "No render client", hr2);
        }
        m_client->GetService(__uuidof(IAudioClock), (void **)&m_clock);
        if (m_clock)
            m_clock->GetFrequency(&m_clockFreq);
        if (!m_exclusive && app_volume_enabled()) {
            m_client->GetService(__uuidof(ISimpleAudioVolume), (void **)&m_simpleVol);
            if (m_simpleVol)
                m_simpleVol->SetMasterVolume(m_gain, nullptr);
        }

        // Ring buffer: sized in device-buffer multiples (only allocated once, kept across re-inits).
        size_t ringFrames = (size_t)std::ceil(m_bufFrames * ring_mult());
        size_t startFrames = (size_t)std::ceil(m_bufFrames * start_mult());
        if (m_ring.empty() || m_ringFrames != ringFrames) {
            std::vector<float> old;
            m_ringFrames = ringFrames;
            m_ring.assign(m_ringFrames * m_devCh, 0.0f);
            m_ringRead = m_ringCount = 0;
        }
        m_startFrames = startFrames > m_ringFrames ? m_ringFrames : startFrames;
        m_submitted = 0;
        m_sysAccum = 0;
        m_started = false;
        m_lastError.reset();
        m_lastErrorLevel = ErrorLevel::Fatal;
        return true;
    }

    void stop_client() {
        if (m_client && m_started) {
            m_client->Stop();
            m_started = false;
            if (m_sysRunning) {
                LARGE_INTEGER now;
                QueryPerformanceCounter(&now);
                m_sysAccum += now.QuadPart - m_sysStart.QuadPart;
                m_sysRunning = false;
            }
        }
    }

    void release_client() {
        stop_client();
        m_simpleVol.Release();
        m_clock.Release();
        m_render.Release();
        m_client.Release();
        m_dev.Release();
        m_started = false;
    }

    // ---------- playback control (lock held) ----------
    void try_start() {
        if (!m_client || m_paused || m_started)
            return;
        if (!m_forceStart && m_ringCount < m_startFrames)
            return;
        if (m_ringCount == 0)
            return;
        fill_device(); // prime the device buffer before Start()
        if (SUCCEEDED(m_client->Start())) {
            m_started = true;
            QueryPerformanceCounter(&m_sysStart);
            m_sysRunning = true;
        }
    }

    uint64_t played_frames() {
        if (cfg_clock_wasapi && m_clock && m_clockFreq) {
            UINT64 pos = 0;
            if (SUCCEEDED(m_clock->GetPosition(&pos, nullptr)))
                return (uint64_t)((double)pos * m_rate / (double)m_clockFreq);
        }
        int64_t ticks = m_sysAccum;
        if (m_sysRunning) {
            LARGE_INTEGER now;
            QueryPerformanceCounter(&now);
            ticks += now.QuadPart - m_sysStart.QuadPart;
        }
        uint64_t f = (uint64_t)((double)ticks * m_rate / (double)m_qpcFreq.QuadPart);
        return f > m_submitted ? m_submitted : f;
    }

    // Pull frames from the ring into the device buffer, converting float -> device format.
    void fill_device() {
        if (!m_client || !m_render)
            return;
        UINT32 pad = 0;
        if (!m_exclusive)
            m_client->GetCurrentPadding(&pad);
        UINT32 n = m_bufFrames - pad;
        if (!n)
            return;
        BYTE *dst = nullptr;
        if (FAILED(m_render->GetBuffer(n, &dst)))
            return;

        size_t take = m_ringCount < n ? m_ringCount : n;
        const float gain = (m_exclusive && app_volume_enabled()) ? m_gain : 1.0f;
        for (size_t i = 0; i < take; ++i) {
            const float *s = &m_ring[((m_ringRead + i) % m_ringFrames) * m_devCh];
            convert_frame(s, dst + i * m_devCh * m_fmt.container, gain);
        }
        if (take < n)
            memset(dst + take * m_devCh * m_fmt.container, 0, (n - take) * m_devCh * m_fmt.container);
        m_ringRead = (m_ringRead + take) % m_ringFrames;
        m_ringCount -= take;
        m_render->ReleaseBuffer(n, 0);
        m_submitted += n;
    }

    void convert_frame(const float *s, BYTE *d, float gain) const {
        for (uint32_t c = 0; c < m_devCh; ++c) {
            float x = s[c] * gain;
            if (m_fmt.isFloat) {
                memcpy(d + c * 4, &x, 4);
                continue;
            }
            const double scale = (double)(1ull << (m_fmt.valid - 1));
            double v = x * scale;
            if (v > scale - 1)
                v = scale - 1;
            if (v < -scale)
                v = -scale;
            int64_t iv = (int64_t)std::llrint(v);
            iv <<= (m_fmt.container * 8 - m_fmt.valid);
            for (int b = 0; b < m_fmt.container; ++b)
                d[c * m_fmt.container + b] = (BYTE)((iv >> (8 * b)) & 0xFF);
        }
    }

    // ---------- render thread ----------
    static DWORD WINAPI thread_proc(LPVOID p) { return ((output_wasapi *)p)->run(); }

    DWORD run() {
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        DWORD task = 0;
        HANDLE mm = AvSetMmThreadCharacteristicsW(L"Pro Audio", &task);
        HANDLE h[2] = {m_stopEvent, m_bufEvent};
        for (;;) {
            DWORD r = WaitForMultipleObjects(2, h, FALSE, 100);
            if (r == WAIT_OBJECT_0)
                break;
            EnterCriticalSection(&m_lock);
            if (r == WAIT_OBJECT_0 + 1 && m_started)
                fill_device();
            try_start();
            if (cfg_auto_disconnect && m_paused && m_client && GetTickCount() - m_pauseTick > kIdleDisconnectMs) {
                release_client(); // device is re-opened on resume
            }
            LeaveCriticalSection(&m_lock);
        }
        if (mm)
            AvRevertMmThreadCharacteristics(mm);
        CoUninitialize();
        return 0;
    }

    // ---------- state ----------
    CRITICAL_SECTION m_lock;
    HANDLE m_stopEvent = nullptr, m_bufEvent = nullptr, m_thread = nullptr;

    GUID m_deviceGuid;
    t_uint32 m_bitdepth;
    uint32_t m_rate = 0, m_srcCh = 0, m_devCh = 0, m_mask = 0;
    bool m_expandMono = false, m_exclusive = false;
    pfc::string8 m_lastError;
    ErrorLevel m_lastErrorLevel = ErrorLevel::Fatal;
    unsigned m_deviceRate = 0;
    bool m_deviceRateQueried = false;

    CComPtr<IMMDevice> m_dev;
    CComPtr<IAudioClient> m_client;
    CComPtr<IAudioRenderClient> m_render;
    CComPtr<IAudioClock> m_clock;
    CComPtr<ISimpleAudioVolume> m_simpleVol;
    SampleFmt m_fmt = {4, 32, true};
    UINT32 m_bufFrames = 0;
    UINT64 m_clockFreq = 0;
    REFERENCE_TIME m_alignedDuration = 0;

    std::vector<float> m_ring;
    size_t m_ringFrames = 0, m_ringRead = 0, m_ringCount = 0, m_startFrames = 0;

    bool m_started = false, m_paused = false, m_forceStart = false;
    DWORD m_pauseTick = 0, m_lastActivity = 0;
    uint64_t m_submitted = 0;
    float m_gain = 1.0f;

    LARGE_INTEGER m_qpcFreq = {}, m_sysStart = {};
    int64_t m_sysAccum = 0;
    bool m_sysRunning = false;
};

static output_factory_t<output_wasapi> g_output_factory;