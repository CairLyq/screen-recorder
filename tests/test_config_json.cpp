#include "tests/check.h"
#include "common/settings.h"

int main() {
  RecordConfig in;
  in.output_dir = "C:/v";
  in.monitor_index = 1;
  in.fps = 60;
  in.quality = Quality::High;
  in.encoder = EncoderKind::Qsv;
  in.format = OutputFormat::Webp;
  in.region = Rect{5, 6, 700, 500};
  in.region_enabled = true;
  in.system_audio.enabled = true;
  in.system_audio.volume = 0.25f;
  in.system_audio.device = "spk";
  in.microphone.enabled = true;
  in.microphone.volume = 0.7f;
  in.microphone.device = "mic0";
  in.include_cursor = false;

  std::string s = config_to_json(in);
  RecordConfig out = config_from_json(s);
  CHECK_STREQ(out.output_dir.c_str(), "C:/v");
  CHECK(out.monitor_index == 1);
  CHECK(out.fps == 60);
  CHECK(out.quality == Quality::High);
  CHECK(out.encoder == EncoderKind::Qsv);
  CHECK(out.format == OutputFormat::Webp);
  CHECK(out.region.x == 5 && out.region.y == 6);
  CHECK(out.region.w == 700 && out.region.h == 500);
  CHECK(out.region_enabled);
  CHECK(out.system_audio.enabled);
  CHECK(out.system_audio.volume > 0.24f && out.system_audio.volume < 0.26f);
  CHECK_STREQ(out.system_audio.device.c_str(), "spk");
  CHECK(out.microphone.enabled);
  CHECK(out.microphone.volume > 0.69f && out.microphone.volume < 0.71f);
  CHECK_STREQ(out.microphone.device.c_str(), "mic0");
  CHECK(!out.include_cursor);

  // 坏 JSON / fps 越界 → 回落默认且不抛
  RecordConfig def = config_from_json("{not json}");
  CHECK(def.fps == 30);
  RecordConfig clamped = config_from_json(R"({"fps":0})");
  CHECK(clamped.fps == 1);
  CHECK(config_from_json("{}").format == OutputFormat::Mp4);
  return 0;
}
