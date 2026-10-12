#include "ui/settings_dialog.h"
#include "ui/resource.h"
#include "common/settings.h"
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <string>

// HOTKEY 控件的修饰位（HOTKEYF_*，在高字节）与 RegisterHotKey 的 MOD_* 互转。
static UINT hkf_to_mods(BYTE f) {
  UINT m = 0;
  if (f & HOTKEYF_SHIFT) m |= MOD_SHIFT;
  if (f & HOTKEYF_CONTROL) m |= MOD_CONTROL;
  if (f & HOTKEYF_ALT) m |= MOD_ALT;
  return m;
}
static BYTE mods_to_hkf(UINT m) {
  BYTE f = 0;
  if (m & MOD_SHIFT) f |= HOTKEYF_SHIFT;
  if (m & MOD_CONTROL) f |= HOTKEYF_CONTROL;
  if (m & MOD_ALT) f |= HOTKEYF_ALT;
  return f;
}

static std::wstring widen(const std::string& s) {
  if (s.empty()) return L"";
  int n = MultiByteToWideChar(CP_ACP, 0, s.c_str(), (int)s.size(), nullptr, 0);
  std::wstring w(n, L'\0');
  MultiByteToWideChar(CP_ACP, 0, s.c_str(), (int)s.size(), &w[0], n);
  return w;
}
static std::string narrow(const std::wstring& w) {
  if (w.empty()) return "";
  int n = WideCharToMultiByte(CP_ACP, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
  std::string s(n, '\0');
  WideCharToMultiByte(CP_ACP, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr);
  return s;
}

static void fill_combo(HWND dlg, int id, const wchar_t* const* items, int n, int sel) {
  HWND cb = GetDlgItem(dlg, id);
  for (int i = 0; i < n; ++i) SendMessageW(cb, CB_ADDSTRING, 0, (LPARAM)items[i]);
  SendMessage(cb, CB_SETCURSEL, sel, 0);
}

static int combo_sel(HWND dlg, int id) {
  return (int)SendMessage(GetDlgItem(dlg, id), CB_GETCURSEL, 0, 0);
}

static const wchar_t* kFps[] = {L"30", L"60"};
static const wchar_t* kQuality[] = {L"low", L"medium", L"high"};
static const wchar_t* kEncoder[] = {L"auto", L"nvenc", L"qsv", L"amf", L"x264"};
static const wchar_t* kFormat[] = {L"MP4", L"WebP"};

static INT_PTR CALLBACK proc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
  static RecordConfig* cfg = nullptr;
  switch (msg) {
    case WM_INITDIALOG: {
      cfg = (RecordConfig*)lp;
      wchar_t mon[16];
      HWND cbm = GetDlgItem(dlg, IDC_MONITOR);
      for (int i = 0; i < 4; ++i) { swprintf_s(mon, L"显示器 %d", i + 1); SendMessageW(cbm, CB_ADDSTRING, 0, (LPARAM)mon); }
      SendMessage(cbm, CB_SETCURSEL, cfg->monitor_index, 0);

      fill_combo(dlg, IDC_FPS, kFps, 2, cfg->fps == 60 ? 1 : 0);
      fill_combo(dlg, IDC_FORMAT, kFormat, 2, cfg->format == OutputFormat::Webp ? 1 : 0);
      fill_combo(dlg, IDC_QUALITY, kQuality, 3, (int)cfg->quality);
      fill_combo(dlg, IDC_ENCODER, kEncoder, 5, (int)cfg->encoder);

      SetDlgItemTextW(dlg, IDC_OUTDIR, widen(cfg->output_dir).c_str());
      wchar_t b[16];
      swprintf_s(b, L"%d", (int)(cfg->system_audio.volume * 100));
      SetDlgItemTextW(dlg, IDC_SYSAUD_VOL, b);
      swprintf_s(b, L"%d", (int)(cfg->microphone.volume * 100));
      SetDlgItemTextW(dlg, IDC_MIC_VOL, b);
      CheckDlgButton(dlg, IDC_SYSAUD, cfg->system_audio.enabled ? BST_CHECKED : BST_UNCHECKED);
      CheckDlgButton(dlg, IDC_MIC, cfg->microphone.enabled ? BST_CHECKED : BST_UNCHECKED);
      CheckDlgButton(dlg, IDC_CURSOR, cfg->include_cursor ? BST_CHECKED : BST_UNCHECKED);
      SendMessageW(GetDlgItem(dlg, IDC_HOTKEY), HKM_SETHOTKEY,
                   MAKEWORD((BYTE)cfg->hotkey_vk, mods_to_hkf((UINT)cfg->hotkey_mods)), 0);
      return TRUE;
    }
    case WM_COMMAND:
      switch (LOWORD(wp)) {
        case IDC_BROWSE: {
          wchar_t path[MAX_PATH] = {};
          GetDlgItemTextW(dlg, IDC_OUTDIR, path, MAX_PATH);
          BROWSEINFOW bi = {};
          bi.hwndOwner = dlg;
          bi.lpszTitle = L"选择输出目录";
          LPITEMIDLIST id = SHBrowseForFolderW(&bi);
          if (id) {
            wchar_t sel[MAX_PATH] = {};
            SHGetPathFromIDListW(id, sel);
            SetDlgItemTextW(dlg, IDC_OUTDIR, sel);
          }
          return TRUE;
        }
        case IDOK: {
          cfg->monitor_index = combo_sel(dlg, IDC_MONITOR);
          cfg->fps = combo_sel(dlg, IDC_FPS) == 1 ? 60 : 30;
          cfg->format = combo_sel(dlg, IDC_FORMAT) == 1 ? OutputFormat::Webp : OutputFormat::Mp4;
          cfg->quality = (Quality)combo_sel(dlg, IDC_QUALITY);
          cfg->encoder = (EncoderKind)combo_sel(dlg, IDC_ENCODER);
          wchar_t path[MAX_PATH] = {};
          GetDlgItemTextW(dlg, IDC_OUTDIR, path, MAX_PATH);
          cfg->output_dir = narrow(path);
          cfg->system_audio.enabled = IsDlgButtonChecked(dlg, IDC_SYSAUD) == BST_CHECKED;
          cfg->microphone.enabled = IsDlgButtonChecked(dlg, IDC_MIC) == BST_CHECKED;
          cfg->include_cursor = IsDlgButtonChecked(dlg, IDC_CURSOR) == BST_CHECKED;
          int sv = GetDlgItemInt(dlg, IDC_SYSAUD_VOL, nullptr, FALSE);
          if (sv < 0) sv = 0; if (sv > 100) sv = 100;
          cfg->system_audio.volume = sv / 100.0f;
          int mv = GetDlgItemInt(dlg, IDC_MIC_VOL, nullptr, FALSE);
          if (mv < 0) mv = 0; if (mv > 100) mv = 100;
          cfg->microphone.volume = mv / 100.0f;
          UINT hk = (UINT)SendMessageW(GetDlgItem(dlg, IDC_HOTKEY), HKM_GETHOTKEY, 0, 0);
          UINT vk = LOBYTE(hk), mods = hkf_to_mods(HIBYTE(hk));
          if (vk == 0) {
            MessageBoxW(dlg, L"请设置热键", L"提示", MB_OK | MB_ICONINFORMATION);
            return TRUE;
          }
          // 无修饰键时只允许功能键，避免单键（如 A）被全局独占。
          if (mods == 0 && !(vk >= VK_F1 && vk <= VK_F24)) {
            MessageBoxW(dlg, L"该热键需带 Ctrl/Alt/Shift 修饰键", L"提示",
                        MB_OK | MB_ICONINFORMATION);
            return TRUE;
          }
          cfg->hotkey_vk = (int)vk;
          cfg->hotkey_mods = (int)mods;
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
  INT_PTR r = DialogBoxParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_SETTINGS),
                              owner, proc, (LPARAM)&cfg);
  return r == 1;
}

static INT_PTR CALLBACK format_proc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
  static RecordConfig* cfg = nullptr;
  switch (msg) {
    case WM_INITDIALOG: {
      cfg = (RecordConfig*)lp;
      CheckRadioButton(dlg, IDC_FMT_MP4, IDC_FMT_WEBP,
                       cfg->format == OutputFormat::Webp ? IDC_FMT_WEBP : IDC_FMT_MP4);
      wchar_t t[64];
      swprintf_s(t, L"区域 %d × %d", cfg->region.w, cfg->region.h);
      SetDlgItemTextW(dlg, IDC_FMT_INFO, t);
      // 弹在所选区域的正中，并夹到该显示器工作区内避免越界。
      RECT wr;
      GetWindowRect(dlg, &wr);
      int dw = wr.right - wr.left, dh = wr.bottom - wr.top;
      RECT reg = {cfg->region.x, cfg->region.y,
                  cfg->region.x + cfg->region.w, cfg->region.y + cfg->region.h};
      HMONITOR hm = MonitorFromRect(&reg, MONITOR_DEFAULTTONEAREST);
      MONITORINFO mi = {sizeof(mi)};
      if (GetMonitorInfoW(hm, &mi)) {
        const RECT& wa = mi.rcWork;
        int x = (reg.left + reg.right) / 2 - dw / 2;
        int y = (reg.top + reg.bottom) / 2 - dh / 2;
        if (x < wa.left) x = wa.left;
        if (y < wa.top) y = wa.top;
        if (x + dw > wa.right) x = wa.right - dw;
        if (y + dh > wa.bottom) y = wa.bottom - dh;
        SetWindowPos(dlg, nullptr, x, y, 0, 0,
                     SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
      }
      return TRUE;
    }
    case WM_COMMAND:
      switch (LOWORD(wp)) {
        case IDOK:
          cfg->format = IsDlgButtonChecked(dlg, IDC_FMT_WEBP) == BST_CHECKED
                            ? OutputFormat::Webp
                            : OutputFormat::Mp4;
          EndDialog(dlg, 1);
          return TRUE;
        case IDCANCEL:
          EndDialog(dlg, 0);
          return TRUE;
      }
      break;
  }
  return FALSE;
}

bool show_format_dialog(HWND owner, RecordConfig& cfg) {
  INT_PTR r = DialogBoxParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_FORMAT),
                              owner, format_proc, (LPARAM)&cfg);
  return r == 1;
}
