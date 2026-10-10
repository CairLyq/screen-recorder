#include "ui/settings_dialog.h"
#include "ui/resource.h"
#include "common/settings.h"
#include "ui/region_selector.h"
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <string>

RECT g_pending_region = {};
bool g_has_pending_region = false;

static void fill_combo(HWND dlg, int id, const char* const* items, int n, int sel) {
  HWND cb = GetDlgItem(dlg, id);
  for (int i = 0; i < n; ++i) SendMessageA(cb, CB_ADDSTRING, 0, (LPARAM)items[i]);
  SendMessage(cb, CB_SETCURSEL, sel, 0);
}

static int combo_sel(HWND dlg, int id) {
  return (int)SendMessage(GetDlgItem(dlg, id), CB_GETCURSEL, 0, 0);
}

static const char* kFps[] = {"30", "60"};
static const char* kQuality[] = {"low", "medium", "high"};
static const char* kEncoder[] = {"auto", "nvenc", "qsv", "amf", "x264"};

static INT_PTR CALLBACK proc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
  static RecordConfig* cfg = nullptr;
  switch (msg) {
    case WM_INITDIALOG: {
      cfg = (RecordConfig*)lp;
      g_has_pending_region = false;
      char mon[16];
      HWND cbm = GetDlgItem(dlg, IDC_MONITOR);
      for (int i = 0; i < 4; ++i) { snprintf(mon, sizeof(mon), "显示器 %d", i + 1); SendMessageA(cbm, CB_ADDSTRING, 0, (LPARAM)mon); }
      SendMessage(cbm, CB_SETCURSEL, cfg->monitor_index, 0);

      fill_combo(dlg, IDC_FPS, kFps, 2, cfg->fps == 60 ? 1 : 0);
      fill_combo(dlg, IDC_QUALITY, kQuality, 3, (int)cfg->quality);
      fill_combo(dlg, IDC_ENCODER, kEncoder, 5, (int)cfg->encoder);

      SetDlgItemTextA(dlg, IDC_OUTDIR, cfg->output_dir.c_str());
      char b[16];
      snprintf(b, sizeof(b), "%d", (int)(cfg->system_audio.volume * 100));
      SetDlgItemTextA(dlg, IDC_SYSAUD_VOL, b);
      snprintf(b, sizeof(b), "%d", (int)(cfg->microphone.volume * 100));
      SetDlgItemTextA(dlg, IDC_MIC_VOL, b);
      CheckDlgButton(dlg, IDC_SYSAUD, cfg->system_audio.enabled ? BST_CHECKED : BST_UNCHECKED);
      CheckDlgButton(dlg, IDC_MIC, cfg->microphone.enabled ? BST_CHECKED : BST_UNCHECKED);
      CheckDlgButton(dlg, IDC_CURSOR, cfg->include_cursor ? BST_CHECKED : BST_UNCHECKED);
      SetDlgItemTextA(dlg, IDC_REGION_INFO,
                      cfg->region_enabled ? "已框选区域" : "全屏");
      return TRUE;
    }
    case WM_COMMAND:
      switch (LOWORD(wp)) {
        case IDC_REGION: {
          RECT r;
          if (pick_region(GetParent(dlg), r)) {
            g_pending_region = r;
            g_has_pending_region = true;
            SetDlgItemTextA(dlg, IDC_REGION_INFO, "已框选区域");
          }
          return TRUE;
        }
        case IDC_BROWSE: {
          char path[MAX_PATH] = {};
          GetDlgItemTextA(dlg, IDC_OUTDIR, path, MAX_PATH);
          BROWSEINFOA bi = {};
          bi.hwndOwner = dlg;
          bi.lpszTitle = "选择输出目录";
          LPITEMIDLIST id = SHBrowseForFolderA(&bi);
          if (id) {
            char sel[MAX_PATH] = {};
            SHGetPathFromIDListA(id, sel);
            SetDlgItemTextA(dlg, IDC_OUTDIR, sel);
          }
          return TRUE;
        }
        case IDOK: {
          cfg->monitor_index = combo_sel(dlg, IDC_MONITOR);
          cfg->fps = combo_sel(dlg, IDC_FPS) == 1 ? 60 : 30;
          cfg->quality = (Quality)combo_sel(dlg, IDC_QUALITY);
          cfg->encoder = (EncoderKind)combo_sel(dlg, IDC_ENCODER);
          char path[MAX_PATH] = {};
          GetDlgItemTextA(dlg, IDC_OUTDIR, path, MAX_PATH);
          cfg->output_dir = path;
          cfg->system_audio.enabled = IsDlgButtonChecked(dlg, IDC_SYSAUD) == BST_CHECKED;
          cfg->microphone.enabled = IsDlgButtonChecked(dlg, IDC_MIC) == BST_CHECKED;
          cfg->include_cursor = IsDlgButtonChecked(dlg, IDC_CURSOR) == BST_CHECKED;
          int sv = GetDlgItemInt(dlg, IDC_SYSAUD_VOL, nullptr, FALSE);
          if (sv < 0) sv = 0; if (sv > 100) sv = 100;
          cfg->system_audio.volume = sv / 100.0f;
          int mv = GetDlgItemInt(dlg, IDC_MIC_VOL, nullptr, FALSE);
          if (mv < 0) mv = 0; if (mv > 100) mv = 100;
          cfg->microphone.volume = mv / 100.0f;
          if (g_has_pending_region) {
            cfg->region.x = g_pending_region.left;
            cfg->region.y = g_pending_region.top;
            cfg->region.w = g_pending_region.right - g_pending_region.left;
            cfg->region.h = g_pending_region.bottom - g_pending_region.top;
            cfg->region_enabled = true;
          }
          EndDialog(dlg, 1);
          return TRUE;
        }
        case IDCANCEL:
          EndDialog(dlg, 0);
          return TRUE;
      }
      break;
  }
  return FALSE;
}

bool show_settings_dialog(HWND owner, RecordConfig& cfg) {
  INT_PTR r = DialogBoxParamA(GetModuleHandleA(nullptr), MAKEINTRESOURCEA(IDD_SETTINGS),
                              owner, proc, (LPARAM)&cfg);
  return r == 1;
}
