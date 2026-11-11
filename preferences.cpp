#include <foobar2000/helpers/foobar2000+atl.h>
#include <foobar2000/SDK/coreDarkMode.h>
#include "config.h"
#include "resource.h"

using namespace wcfg;

DECLARE_COMPONENT_VERSION("WASAPI advanced output", "0.2.0",
    "Configurable WASAPI output component featuring shared/exclusive modes, buffer tuning, & more. github.com/sukibaby/foo_out_wasapi_advanced");
VALIDATE_COMPONENT_FILENAME("foo_out_wasapi_custom.dll");

// {a3f0c2d8-91b4-4e7a-b6d5-0f2c8e41a7b9}
static const GUID guid_prefs_page = { 0xa3f0c2d8, 0x91b4, 0x4e7a, { 0xb6, 0xd5, 0x0f, 0x2c, 0x8e, 0x41, 0xa7, 0xb9 } };

namespace {

// Slider resolution: buffer = 0.1 steps (10..200), ring/start = 0.05 steps (20..800).
constexpr int kBufStepX100 = 10, kRingStepX100 = 5;

struct Settings {
    int mode, appVol, gapless, idle, mono, check, clockWasapi, as24, periodMin, buf, ring, start;
    bool operator==(const Settings& o) const { return !memcmp(this, &o, sizeof(*this)); }
};

Settings from_cfg() {
    return { (int)cfg_mode, (int)cfg_app_volume, (int)cfg_gapless, (int)cfg_auto_disconnect, (int)cfg_mono_to_stereo,
             (int)cfg_check_format, (int)cfg_clock_wasapi, (int)cfg_16_as_24, (int)cfg_period_min,
             (int)cfg_buffer_x100, (int)cfg_ring_x100, (int)cfg_start_x100 };
}

Settings defaults() {
    return {
        mode_shared, // WASAPI mode
        0,           // Enable volume control (forced on by exclusive mode)
        1,           // Gapless mode playback
        0,           // Disconnect when idle
        0,           // Convert mono to stereo
        1,           // Call IsFormatSupported before Initialize
        1,           // WASAPI as clock (0 = system clock)
        0,           // Play 16 bit streams as 24 bit
        0,           // Device period (1 = minimum)
        kBufferDef, kRingDef, kStartDef };
}

class CWasapiPrefs : public CDialogImpl<CWasapiPrefs>, public preferences_page_instance {
    preferences_page_callback::ptr m_cb;
public:
    CWasapiPrefs(preferences_page_callback::ptr cb) : m_cb(cb) {}
    ~CWasapiPrefs() noexcept {}
    enum { IDD = IDD_WASAPI_PREFS };

    // preferences_page_instance
    t_uint32 get_state() override {
        t_uint32 s = preferences_state::resettable | preferences_state::dark_mode_supported;
        if (!(read_ui() == from_cfg())) s |= preferences_state::changed;
        return s;
    }
    HWND get_wnd() override { return m_hWnd; }
    void apply() override {
        Settings s = read_ui();
        cfg_mode = s.mode; cfg_app_volume = s.appVol; cfg_gapless = s.gapless; cfg_auto_disconnect = s.idle;
        cfg_mono_to_stereo = s.mono; cfg_check_format = s.check; cfg_clock_wasapi = s.clockWasapi;
        cfg_16_as_24 = s.as24; cfg_period_min = s.periodMin;
        cfg_buffer_x100 = s.buf; cfg_ring_x100 = s.ring; cfg_start_x100 = s.start;
        m_cb->on_state_changed();
    }
    void reset() override { write_ui(defaults()); m_cb->on_state_changed(); }

    BEGIN_MSG_MAP_EX(CWasapiPrefs)
        MSG_WM_INITDIALOG(OnInitDialog)
        MSG_WM_HSCROLL(OnHScroll)
        COMMAND_CODE_HANDLER_EX(BN_CLICKED, OnClicked)
    END_MSG_MAP()

private:
    fb2k::CCoreDarkModeHooks m_dark;

    BOOL OnInitDialog(CWindow, LPARAM) {
        CTrackBarCtrl(GetDlgItem(IDC_SLD_BUFFER)).SetRange(kBufferMin / kBufStepX100, kBufferMax / kBufStepX100);
        CTrackBarCtrl(GetDlgItem(IDC_SLD_RING)).SetRange(kRingMin / kRingStepX100, kRingMax / kRingStepX100);
        CTrackBarCtrl(GetDlgItem(IDC_SLD_START)).SetRange(kStartMin / kRingStepX100, kStartMax / kRingStepX100);
        CTrackBarCtrl(GetDlgItem(IDC_SLD_BUFFER)).SetTicFreq(10);
        CTrackBarCtrl(GetDlgItem(IDC_SLD_RING)).SetTicFreq(100);
        CTrackBarCtrl(GetDlgItem(IDC_SLD_START)).SetTicFreq(100);
        write_ui(from_cfg());
        m_dark.AddDialogWithControls(*this);
        return TRUE;
    }

    void OnClicked(UINT, int id, CWindow) {
        if (id == IDC_BTN_DEFAULTS) write_ui(defaults());
        if (id == IDC_MODE_SHARED || id == IDC_MODE_EXCLUSIVE) update_app_volume_control();
        m_cb->on_state_changed();
    }

    // Exclusive mode has no Windows mixer to fall back on, so app volume control is forced on and greyed out.
    // In shared mode the checkbox is a normal, selectable option.
    void update_app_volume_control() {
        const bool exclusive = IsDlgButtonChecked(IDC_MODE_EXCLUSIVE) != 0;
        if (exclusive) CheckDlgButton(IDC_CHK_APPVOLUME, BST_CHECKED);
        ::EnableWindow(GetDlgItem(IDC_CHK_APPVOLUME), exclusive ? FALSE : TRUE);
    }

    void OnHScroll(UINT, UINT, CTrackBarCtrl) {
        update_labels();
        m_cb->on_state_changed();
    }

    // Keep start threshold <= ring size at apply time (ring buffer always holds the start amount).
    Settings read_ui() {
        Settings s;
        s.mode        = IsDlgButtonChecked(IDC_MODE_EXCLUSIVE) ? mode_exclusive : mode_shared;
        s.appVol      = (s.mode == mode_exclusive || IsDlgButtonChecked(IDC_CHK_APPVOLUME)) ? 1 : 0;
        s.gapless     = IsDlgButtonChecked(IDC_CHK_GAPLESS) ? 1 : 0;
        s.idle        = IsDlgButtonChecked(IDC_CHK_AUTODISCONNECT) ? 1 : 0;
        s.mono        = IsDlgButtonChecked(IDC_CHK_MONO2STEREO) ? 1 : 0;
        s.check       = IsDlgButtonChecked(IDC_CHK_CHECKFORMAT) ? 1 : 0;
        s.clockWasapi = IsDlgButtonChecked(IDC_CHK_WASAPICLOCK) ? 1 : 0;
        s.as24        = IsDlgButtonChecked(IDC_CHK_16AS24) ? 1 : 0;
        s.periodMin   = IsDlgButtonChecked(IDC_PERIOD_MIN) ? 1 : 0;
        s.buf   = CTrackBarCtrl(GetDlgItem(IDC_SLD_BUFFER)).GetPos() * kBufStepX100;
        s.ring  = CTrackBarCtrl(GetDlgItem(IDC_SLD_RING)).GetPos() * kRingStepX100;
        s.start = CTrackBarCtrl(GetDlgItem(IDC_SLD_START)).GetPos() * kRingStepX100;
        return s;
    }

    void write_ui(const Settings& s) {
        CheckRadioButton(IDC_MODE_SHARED, IDC_MODE_EXCLUSIVE, s.mode == mode_exclusive ? IDC_MODE_EXCLUSIVE : IDC_MODE_SHARED);
        CheckDlgButton(IDC_CHK_APPVOLUME, s.appVol);
        CheckDlgButton(IDC_CHK_GAPLESS, s.gapless);
        CheckDlgButton(IDC_CHK_AUTODISCONNECT, s.idle);
        CheckDlgButton(IDC_CHK_MONO2STEREO, s.mono);
        CheckDlgButton(IDC_CHK_CHECKFORMAT, s.check);
        CheckDlgButton(IDC_CHK_WASAPICLOCK, s.clockWasapi);
        CheckDlgButton(IDC_CHK_16AS24, s.as24);
        CheckRadioButton(IDC_PERIOD_DEFAULT, IDC_PERIOD_MIN, s.periodMin ? IDC_PERIOD_MIN : IDC_PERIOD_DEFAULT);
        CTrackBarCtrl(GetDlgItem(IDC_SLD_BUFFER)).SetPos(s.buf / kBufStepX100);
        CTrackBarCtrl(GetDlgItem(IDC_SLD_RING)).SetPos(s.ring / kRingStepX100);
        CTrackBarCtrl(GetDlgItem(IDC_SLD_START)).SetPos(s.start / kRingStepX100);
        update_app_volume_control();
        update_labels();
    }

    void update_labels() {
        Settings s = read_ui();
        pfc::string_formatter f;
        uSetDlgItemText(*this, IDC_LBL_BUFFER, (f << pfc::format_float(s.buf / 100.0, 0, 1)).c_str());
        f.reset();
        uSetDlgItemText(*this, IDC_LBL_RING, (f << pfc::format_float(s.ring / 100.0, 0, 2)).c_str());
        f.reset();
        uSetDlgItemText(*this, IDC_LBL_START, (f << pfc::format_float(s.start / 100.0, 0, 2)).c_str());
    }
};

class preferences_page_wasapi : public preferences_page_v3 {
public:
    preferences_page_instance::ptr instantiate(fb2k::hwnd_t parent, preferences_page_callback::ptr callback) override {
        auto p = fb2k::service_new<CWasapiPrefs>(callback);
        p->Create(parent);
        return p;
    }
    const char* get_name() override { return "WASAPI (custom)"; }
    GUID get_guid() override { return guid_prefs_page; }
    GUID get_parent_guid() override { return guid_output; } // Playback > Output
};

preferences_page_factory_t<preferences_page_wasapi> g_prefs_factory;

} // namespace
