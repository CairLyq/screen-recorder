#pragma once
#include "common/types.h"
#include <string>

RecordConfig load_settings(const std::string& path);
void save_settings(const std::string& path, const RecordConfig& cfg);

// 返回 %APPDATA%\ScreenRecorder\settings.json；若 %APPDATA% 不存在则退回当前目录。
std::string default_settings_path();
