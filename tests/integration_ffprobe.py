import json, os, subprocess, sys, tempfile

RECORDER = sys.argv[1] if len(sys.argv) > 1 else "build/Debug/recorder.exe"
SECONDS = 5

def run():
    out = tempfile.mkdtemp(prefix="screenrec_it_")
    p = subprocess.run([RECORDER, "--record", str(SECONDS), "--out", out],
                       capture_output=True, text=True, timeout=SECONDS + 30)
    print("recorder stderr:\n", p.stderr)
    assert p.returncode == 0, f"recorder exit {p.returncode}"
    mp4 = p.stdout.strip().splitlines()[-1]
    assert os.path.isfile(mp4), f"no file: {mp4}"
    probe = subprocess.run(
        ["ffprobe", "-v", "error", "-print_format", "json",
         "-show_format", "-show_streams", mp4],
        capture_output=True, text=True, timeout=30)
    assert probe.returncode == 0, probe.stderr
    meta = json.loads(probe.stdout)
    kinds = [s["codec_type"] for s in meta["streams"]]
    assert "video" in kinds, f"no video stream: {kinds}"
    dur = float(meta["format"]["duration"])
    assert SECONDS - 1.5 <= dur <= SECONDS + 3.0, f"duration {dur} off"
    v = next(s for s in meta["streams"] if s["codec_type"] == "video")
    print(f"OK video={v['codec_name']} {v['width']}x{v['height']} "
          f"dur={dur:.2f}s streams={kinds}")
    print("INTEGRATION PASS")

run()
