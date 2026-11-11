#include <foobar2000/helpers/foobar2000+atl.h>
#include <foobar2000/SDK/coreDarkMode.h>
#include "config.h"
#include "resource.h"
#include "tooltips.h"
#include <algorithm>
#include <vector>

using namespace wcfg;

DECLARE_COMPONENT_VERSION(
    "WASAPI advanced output",
    "1.1.0",
    "Configurable WASAPI output component featuring shared/exclusive modes,\n"
    "buffer tuning, & more.\n\n"
    "github.com/sukibaby/foo_out_wasapi_advanced\n\n"
    "This component is released under the MIT License.");
VALIDATE_COMPONENT_FILENAME("foo_out_wasapi_advanced.dll");

// {a3f0c2d8-91b4-4e7a-b6d5-0f2c8e41a7b9}
static const GUID guid_prefs_page = {0xa3f0c2d8, 0x91b4, 0x4e7a, {0xb6, 0xd5, 0x0f, 0x2c, 0x8e, 0x41, 0xa7, 0xb9}};

namespace {

// Slider resolution: buffer = 0.1 steps (10..200), ring/start = 0.05 steps (20..800).
constexpr int kBufStepX100 = 10, kRingStepX100 = 5;

struct Settings {
    int mode, appVol, gapless, idle, mono, check, clockWasapi, as24, periodMin, buf, ring, start, matchRate, suppressDiag;
    bool operator==(const Settings &o) const { return !memcmp(this, &o, sizeof(*this)); }
};

Settings from_cfg() {
    return {(int)cfg_mode,         (int)cfg_app_volume,   (int)cfg_gapless,    (int)cfg_auto_disconnect, (int)cfg_mono_to_stereo,
            (int)cfg_check_format, (int)cfg_clock_wasapi, (int)cfg_16_as_24,   (int)cfg_period_min,      (int)cfg_buffer_x100,
            (int)cfg_ring_x100,    (int)cfg_start_x100,   (int)cfg_match_rate, (int)cfg_suppress_diag};
}

// "Reset page" - all values come from config.h (kDef...), so there is a single place to change defaults.
Settings defaults() {
    return {kDefMode,   kDefAppVolume, kDefGapless, kDefAutoDisconnect, kDefMonoToStereo, kDefCheckFormat, kDefClockWasapi,
            kDef16As24, kDefPeriodMin, kBufferDef,  kRingDef,           kStartDef,        kDefMatchRate,   kDefSuppressDiag};
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
        if (!(read_ui() == from_cfg()))
            s |= preferences_state::changed;
        return s;
    }
    HWND get_wnd() override { return m_hWnd; }
    void apply() override {
        Settings s = read_ui();
        cfg_mode = s.mode;
        cfg_app_volume = s.appVol;
        cfg_gapless = s.gapless;
        cfg_auto_disconnect = s.idle;
        cfg_mono_to_stereo = s.mono;
        cfg_check_format = s.check;
        cfg_clock_wasapi = s.clockWasapi;
        cfg_16_as_24 = s.as24;
        cfg_period_min = s.periodMin;
        cfg_buffer_x100 = s.buf;
        cfg_ring_x100 = s.ring;
        cfg_start_x100 = s.start;
        cfg_match_rate = s.matchRate;
        cfg_suppress_diag = s.suppressDiag;
        m_cb->on_state_changed();
    }
    void reset() override {
        write_ui(defaults());
        m_cb->on_state_changed();
    }

    BEGIN_MSG_MAP_EX(CWasapiPrefs)
    MSG_WM_INITDIALOG(OnInitDialog)
    MSG_WM_HSCROLL(OnHScroll)
    MSG_WM_SIZE(OnSize)
    COMMAND_CODE_HANDLER_EX(BN_CLICKED, OnClicked)
    END_MSG_MAP()

  private:
    fb2k::CCoreDarkModeHooks m_dark;
    CToolTipCtrl m_tip;

    // Rectangle tool (on the dialog) that keeps the app-volume tooltip working while that checkbox is disabled.
    static constexpr UINT_PTR kAppVolRectToolId = 0x7000 + IDC_CHK_APPVOLUME;

    // Layout: the dialog is designed at a fixed base width. When the preferences pane is wider, the group
    // boxes, checkboxes and sliders are stretched by the extra width and the value labels move right.
    struct LayoutItem {
        int id;
        CRect rc;
        bool stretch;
    };
    std::vector<LayoutItem> m_layout;
    int m_baseWidth = 0;

    void CaptureLayout() {
        CRect client;
        GetClientRect(&client);
        m_baseWidth = client.Width();
        static const int stretchIds[] = {IDC_GRP_MODE,         IDC_GRP_OPTIONS,     IDC_GRP_PERIOD,         IDC_GRP_BUFFER,
                                         IDC_CHK_APPVOLUME,    IDC_CHK_GAPLESS,     IDC_CHK_AUTODISCONNECT, IDC_CHK_MONO2STEREO,
                                         IDC_CHK_CHECKFORMAT,  IDC_CHK_WASAPICLOCK, IDC_CHK_16AS24,         IDC_CHK_MATCHRATE,
                                         IDC_CHK_SUPPRESSDIAG, IDC_SLD_BUFFER,      IDC_SLD_RING,           IDC_SLD_START};
        static const int moveIds[] = {IDC_LBL_BUFFER, IDC_LBL_RING, IDC_LBL_START};
        auto add = [&](int id, bool stretch) {
            CWindow w(GetDlgItem(id));
            if (!w.IsWindow())
                return;
            CRect rc;
            w.GetWindowRect(&rc);
            ScreenToClient(&rc);
            m_layout.push_back({id, rc, stretch});
        };
        for (int id : stretchIds)
            add(id, true);
        for (int id : moveIds)
            add(id, false);
    }

    void OnSize(UINT, CSize size) {
        if (m_layout.empty())
            return;
        const int dx = (std::max)(0, (int)size.cx - m_baseWidth);
        for (const LayoutItem &it : m_layout) {
            CWindow w(GetDlgItem(it.id));
            if (!w.IsWindow())
                continue;
            if (it.stretch)
                w.SetWindowPos(nullptr, it.rc.left, it.rc.top, it.rc.Width() + dx, it.rc.Height(), SWP_NOZORDER | SWP_NOACTIVATE);
            else
                w.SetWindowPos(nullptr, it.rc.left + dx, it.rc.top, 0, 0, SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOSIZE);
        }
        if (m_tip.IsWindow()) {
            CWindow av(GetDlgItem(IDC_CHK_APPVOLUME));
            CRect rc;
            av.GetWindowRect(&rc);
            ScreenToClient(&rc);
            m_tip.SetToolRect(*this, kAppVolRectToolId, &rc);
        }
        Invalidate();
    }

    BOOL OnInitDialog(CWindow, LPARAM) {
        CTrackBarCtrl(GetDlgItem(IDC_SLD_BUFFER)).SetRange(kBufferMin / kBufStepX100, kBufferMax / kBufStepX100);
        CTrackBarCtrl(GetDlgItem(IDC_SLD_RING)).SetRange(kRingMin / kRingStepX100, kRingMax / kRingStepX100);
        CTrackBarCtrl(GetDlgItem(IDC_SLD_START)).SetRange(kStartMin / kRingStepX100, kStartMax / kRingStepX100);
        CTrackBarCtrl(GetDlgItem(IDC_SLD_BUFFER)).SetTicFreq(10);
        CTrackBarCtrl(GetDlgItem(IDC_SLD_RING)).SetTicFreq(100);
        CTrackBarCtrl(GetDlgItem(IDC_SLD_START)).SetTicFreq(100);
        CaptureLayout();
        write_ui(from_cfg());
        InitTooltips();
        m_dark.AddDialogWithControls(*this);
        return TRUE;
    }

    // One tooltip per control, texts come from tooltips.h.
    void InitTooltips() {
        if (!m_tip.Create(*this, nullptr, nullptr, TTS_ALWAYSTIP | TTS_NOPREFIX))
            return;
        {
            CClientDC dc(*this);
            m_tip.SetMaxTipWidth(MulDiv(360, dc.GetDeviceCaps(LOGPIXELSX), 96)); // enables word wrap
        }
        m_tip.SetDelayTime(TTDT_INITIAL, 400);
        m_tip.SetDelayTime(TTDT_AUTOPOP, 30000);

        for (const TipEntry &e : kTooltips) {
            CWindow ctl(GetDlgItem(e.id));
            if (!ctl.IsWindow())
                continue;
            LPWSTR text = const_cast<LPWSTR>(e.text); // static string, the tooltip only keeps the pointer

            // Tool on the control itself (works while the control is enabled).
            CToolInfo ti(TTF_SUBCLASS | TTF_IDISHWND, *this, (UINT_PTR)ctl.m_hWnd, nullptr, text);
            m_tip.AddTool(&ti);

            // A disabled control gets no mouse messages, so the app-volume checkbox (disabled in exclusive
            // mode) also gets a rectangle tool on the dialog, which still sees the mouse.
            if (e.id == IDC_CHK_APPVOLUME) {
                CRect rc;
                ctl.GetWindowRect(&rc);
                ScreenToClient(&rc);
                CToolInfo tr(TTF_SUBCLASS, *this, kAppVolRectToolId, &rc, text);
                m_tip.AddTool(&tr);
            }
        }
        m_tip.Activate(TRUE);
    }

    void OnClicked(UINT, int id, CWindow) {
        if (id == IDC_MODE_SHARED || id == IDC_MODE_EXCLUSIVE)
            update_app_volume_control();
        m_cb->on_state_changed();
    }

    // Exclusive mode has no Windows mixer to fall back on, so app volume control is forced on and greyed out.
    // In shared mode the checkbox is a normal, selectable option.
    void update_app_volume_control() {
        const bool exclusive = IsDlgButtonChecked(IDC_MODE_EXCLUSIVE) != 0;
        if (exclusive)
            CheckDlgButton(IDC_CHK_APPVOLUME, BST_CHECKED);
        ::EnableWindow(GetDlgItem(IDC_CHK_APPVOLUME), exclusive ? FALSE : TRUE);
    }

    void OnHScroll(UINT, UINT, CTrackBarCtrl) {
        update_labels();
        m_cb->on_state_changed();
    }

    // Keep start threshold <= ring size at apply time (ring buffer always holds the start amount).
    Settings read_ui() {
        Settings s;
        s.mode = IsDlgButtonChecked(IDC_MODE_EXCLUSIVE) ? mode_exclusive : mode_shared;
        s.appVol = (s.mode == mode_exclusive || IsDlgButtonChecked(IDC_CHK_APPVOLUME)) ? 1 : 0;
        s.gapless = IsDlgButtonChecked(IDC_CHK_GAPLESS) ? 1 : 0;
        s.idle = IsDlgButtonChecked(IDC_CHK_AUTODISCONNECT) ? 1 : 0;
        s.mono = IsDlgButtonChecked(IDC_CHK_MONO2STEREO) ? 1 : 0;
        s.check = IsDlgButtonChecked(IDC_CHK_CHECKFORMAT) ? 1 : 0;
        s.clockWasapi = IsDlgButtonChecked(IDC_CHK_WASAPICLOCK) ? 1 : 0;
        s.as24 = IsDlgButtonChecked(IDC_CHK_16AS24) ? 1 : 0;
        s.matchRate = IsDlgButtonChecked(IDC_CHK_MATCHRATE) ? 1 : 0;
        s.suppressDiag = IsDlgButtonChecked(IDC_CHK_SUPPRESSDIAG) ? 1 : 0;
        s.periodMin = IsDlgButtonChecked(IDC_PERIOD_MIN) ? 1 : 0;
        s.buf = CTrackBarCtrl(GetDlgItem(IDC_SLD_BUFFER)).GetPos() * kBufStepX100;
        s.ring = CTrackBarCtrl(GetDlgItem(IDC_SLD_RING)).GetPos() * kRingStepX100;
        s.start = CTrackBarCtrl(GetDlgItem(IDC_SLD_START)).GetPos() * kRingStepX100;
        return s;
    }

    void write_ui(const Settings &s) {
        CheckRadioButton(IDC_MODE_SHARED, IDC_MODE_EXCLUSIVE, s.mode == mode_exclusive ? IDC_MODE_EXCLUSIVE : IDC_MODE_SHARED);
        CheckDlgButton(IDC_CHK_APPVOLUME, s.appVol);
        CheckDlgButton(IDC_CHK_GAPLESS, s.gapless);
        CheckDlgButton(IDC_CHK_AUTODISCONNECT, s.idle);
        CheckDlgButton(IDC_CHK_MONO2STEREO, s.mono);
        CheckDlgButton(IDC_CHK_CHECKFORMAT, s.check);
        CheckDlgButton(IDC_CHK_WASAPICLOCK, s.clockWasapi);
        CheckDlgButton(IDC_CHK_16AS24, s.as24);
        CheckDlgButton(IDC_CHK_MATCHRATE, s.matchRate);
        CheckDlgButton(IDC_CHK_SUPPRESSDIAG, s.suppressDiag);
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
    const char *get_name() override { return "WASAPI (Advanced)"; }
    GUID get_guid() override { return guid_prefs_page; }
    GUID get_parent_guid() override { return guid_output; } // Playback > Output
};

preferences_page_factory_t<preferences_page_wasapi> g_prefs_factory;

} // namespace
