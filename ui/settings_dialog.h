#pragma once
#include "common/types.h"
#include <windows.h>

// 阻塞显示设置对话框，原地修改 cfg。返回 true=确定，false=取消。
bool show_settings_dialog(HWND owner, RecordConfig& cfg);

// 开始录制前的格式确认框：二选一 mp4 / webp，原地修改 cfg.format。
// 返回 true=确定（开始录制），false=取消。
bool show_format_dialog(HWND owner, RecordConfig& cfg);
