#pragma once
#include "common/types.h"
#include <windows.h>

// 阻塞显示设置对话框，原地修改 cfg。返回 true=确定，false=取消。
bool show_settings_dialog(HWND owner, RecordConfig& cfg);

// 供区域选取回调用：本次对话新框选的区域（空表示未改）。
extern RECT g_pending_region;
extern bool g_has_pending_region;
