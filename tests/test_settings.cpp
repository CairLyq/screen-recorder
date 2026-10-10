#include "tests/check.h"
#include "common/settings.h"
#include <cstdio>

int main() {
  const char* path = "test_settings_tmp.json";
  std::remove(path);

  RecordConfig in;
  in.output_dir = "C:/videos";
  in.fps = 60;
  in.quality = Quality::High;
  in.encoder = EncoderKind::Nvenc;
  in.region = Rect{10, 20, 800, 600};
  in.region_enabled = true;
  in.system_audio.enabled = true;
  in.system_audio.volume = 0.5f;
  in.microphone.enabled = true;
  in.include_cursor = false;
  save_settings(path, in);

  RecordConfig out = load_settings(path);
  CHECK_STREQ(out.output_dir.c_str(), "C:/videos");
  CHECK(out.fps == 60);
  CHECK(out.quality == Quality::High);
  CHECK(out.encoder == EncoderKind::Nvenc);
  CHECK(out.region.x == 10 && out.region.h == 600);
  CHECK(out.region_enabled);
  CHECK(out.system_audio.enabled);
  CHECK(out.system_audio.volume > 0.49f && out.system_audio.volume < 0.51f);
  CHECK(out.microphone.enabled);
  CHECK(!out.include_cursor);

  // 默认值：不存在的文件走默认
  std::remove(path);
  RecordConfig def = load_settings(path);
  CHECK(def.fps == 30);
  CHECK(def.quality == Quality::Medium);
  CHECK(def.encoder == EncoderKind::Auto);
  CHECK(!def.region_enabled);
  CHECK(def.include_cursor);
  return 0;
}
