#pragma once
#include <string>

// 启动 recorder.exe --pipe <name>；成功返回进程句柄（void* = HANDLE）。
void* spawn_recorder(const std::string& exe_path, const std::string& pipe_name);

// 结束进程（等待一下，必要时 Terminate）。传 nullptr 安全。
void kill_recorder(void* process);
