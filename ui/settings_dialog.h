#pragma once
#include "common/types.h"
#include <windows.h>

// 阻塞显示设置对话框，原地修改 cfg。返回 true=确定，false=取消。
bool show_settings_dialog(HWND owner, RecordConfig& cfg);
