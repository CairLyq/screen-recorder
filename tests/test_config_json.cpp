#include "tests/check.h"
#include "common/settings.h"

int main() {
  RecordConfig in;
  in.output_dir = "C:/v";
  in.monitor_index = 1;
  in.fps = 60;
  in.quality = Quality::High;
  in.encoder = EncoderKind::Qsv;
  in.region = Rect{5, 6, 700, 500};
  in.region_enabled = true;
  in.system_audio.enabled = true;
  in.system_audio.volume = 0.25f;
  in.microphone.enabled = true;
  in.include_cursor = false;

  std::string s = config_to_json(in);
  RecordConfig out = config_from_json(s);
  CHECK_STREQ(out.output_dir.c_str(), "C:/v");
  CHECK(out.monitor_index == 1);
  CHECK(out.fps == 60);
  CHECK(out.quality == Quality::High);
  CHECK(out.encoder == EncoderKind::Qsv);
  CHECK(out.region.x == 5 && out.region.h == 500);
  CHECK(out.region_enabled);
  CHECK(out.system_audio.enabled);
  CHECK(out.system_audio.volume > 0.24f && out.system_audio.volume < 0.26f);
  CHECK(out.microphone.enabled);
  CHECK(!out.include_cursor);

  // 坏 JSON / fps 越界 → 回落默认且不抛
  RecordConfig def = config_from_json("{not json}");
  CHECK(def.fps == 30);
  RecordConfig clamped = config_from_json(R"({"fps":0})");
  CHECK(clamped.fps == 1);
  return 0;
}
