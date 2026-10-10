#include "common/log.h"
#include "common/settings.h"
#include "common/ipc_message.h"
#include "recorder/pipeline.h"
#include "recorder/ipc_server.h"
#include "recorder/muxer.h"
#include <nlohmann/json.hpp>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <windows.h>

using nlohmann::json;

static int run_headless(int seconds, const std::string& out_dir) {
  RecordConfig cfg;
  cfg.output_dir = out_dir;
  cfg.fps = 30;
  cfg.system_audio.enabled = true;
  Pipeline p;
  std::string path;
  if (!p.start(cfg, path)) { LOGE("start failed"); return 2; }
  for (int i = 0; i < seconds * 10 && p.stats().elapsed_ms < seconds * 1000; ++i)
    Sleep(100);
  p.stop();
  LOGI("done: %s", path.c_str());
  std::printf("%s\n", path.c_str());
  return 0;
}

static int64_t now_ms() { return (int64_t)GetTickCount64(); }

static int run_ipc(const std::string& pipe_name) {
  IpcServer server;
  if (!server.create(pipe_name)) { LOGE("pipe create failed"); return 3; }
  LOGI("waiting for UI on %s", pipe_name.c_str());
  if (!server.wait_client()) { LOGE("connect failed"); return 3; }
  server.send_message({MsgType::Ready, R"({"version":1})"});

  Pipeline p;
  bool recording = false;
  int64_t last_stats = 0;
  for (;;) {
    Message m;
    int r = server.poll_message(m);
    if (r < 0) { LOGI("pipe closed"); break; }
    if (r == 1) {
      if (m.type == MsgType::Start) {
        RecordConfig cfg = config_from_json(m.payload);
        std::string path;
        if (p.start(cfg, path)) {
          recording = true;
          last_stats = now_ms();
          json j; j["path"] = path;
          server.send_message({MsgType::State, j.dump()});
        } else {
          server.send_message({MsgType::Error, R"({"code":"start_failed"})"});
        }
      } else if (m.type == MsgType::Stop) {
        p.stop();
        recording = false;
        server.send_message({MsgType::Done, R"({"ok":true})"});
      } else if (m.type == MsgType::Ping) {
        server.send_message({MsgType::State, R"("alive")"});
      }
    }
    if (recording) {
      int64_t now = now_ms();
      if (now - last_stats >= 1000) {
        last_stats = now;
        Stats s = p.stats();
        json sj;
        sj["fps"] = s.fps;
        sj["elapsed_ms"] = s.elapsed_ms;
        sj["dropped_frames"] = s.dropped_frames;
        server.send_message({MsgType::Stats, sj.dump()});
      }
    }
    Sleep(5);
  }
  recording = false;
  p.stop();
  server.close();
  return 0;
}

int main(int argc, char** argv) {
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  std::string mode = "ipc";
  std::string pipe_name = "\\\\.\\pipe\\screenrec-default";
  std::string out_dir = ".";
  int seconds = 10;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--headless") mode = "headless";
    else if (a == "--pipe" && i + 1 < argc) pipe_name = argv[++i];
    else if (a == "--out" && i + 1 < argc) out_dir = argv[++i];
    else if (a == "--record" && i + 1 < argc) { mode = "headless"; seconds = atoi(argv[++i]); }
  }
  CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  if (mode == "headless") return run_headless(seconds, out_dir);
  return run_ipc(pipe_name);
}
