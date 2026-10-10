#include "recorder/wasapi_capture.h"
#include "common/log.h"
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <thread>
#include <atomic>
#include <cmath>
#include <algorithm>
#include <cstring>

static const IID kIID_IAudioClient = __uuidof(IAudioClient);
static const IID kIID_IAudioCaptureClient = __uuidof(IAudioCaptureClient);

bool WasapiCapture::init(bool loopback, int channels_hint) {
  HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  (void)hr;  // 已初始化则忽略
  IMMDeviceEnumerator* enumerator = nullptr;
  if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
                              CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                              (void**)&enumerator)))
    return false;

  IMMDevice* device = nullptr;
  hr = enumerator->GetDefaultAudioEndpoint(loopback ? eRender : eCapture,
                                           eConsole, &device);
  enumerator->Release();
  if (FAILED(hr)) { LOGE("no default audio endpoint"); return false; }

  IAudioClient* client = nullptr;
  if (FAILED(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                              (void**)&client))) {
    device->Release();
    return false;
  }
  device->Release();

  WAVEFORMATEX* mix = nullptr;
  client->GetMixFormat(&mix);
  sample_rate_ = mix->nSamplesPerSec;
  // 共享模式下 Initialize 要求格式与音频引擎 mix format 一致；声道数必须用设备原生的，
  // 否则 WAVEFORMATEX 自相矛盾（nChannels 与 nBlockAlign 不符）导致 Initialize 失败或误读。
  channels_ = mix->nChannels;
  if (channels_hint > 0 && channels_hint != channels_) {
    LOGE("audio endpoint has %d ch, requested %d — unsupported, skipping", channels_,
         channels_hint);
    CoTaskMemFree(mix);
    client->Release();
    channels_ = 0;
    return false;
  }
  // 统一用 float32
  mix->wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
  mix->wBitsPerSample = 32;
  mix->nBlockAlign = (WORD)(channels_ * 4);
  mix->nAvgBytesPerSec = sample_rate_ * mix->nBlockAlign;
  mix->cbSize = 0;

  DWORD flags = loopback ? AUDCLNT_STREAMFLAGS_LOOPBACK : 0;
  if (FAILED(client->Initialize(AUDCLNT_SHAREMODE_SHARED, flags,
                                10000000, 0, mix, nullptr))) {
    CoTaskMemFree(mix);
    client->Release();
    return false;
  }
  CoTaskMemFree(mix);

  if (FAILED(client->GetService(kIID_IAudioCaptureClient, &capture_))) {
    client->Release();
    return false;
  }
  client_ = client;
  LOGI("wasapi init: %s %dHz %dch", loopback ? "loopback" : "mic", sample_rate_,
       channels_);
  return true;
}

void WasapiCapture::start(const std::function<void(const float*, int)>& on_data) {
  running_ = true;
  IAudioClient* client = (IAudioClient*)client_;
  IAudioCaptureClient* cap = (IAudioCaptureClient*)capture_;
  client->Start();
  std::vector<float> scratch;
  while (running_) {
    UINT32 packet = 0;
    if (FAILED(cap->GetNextPacketSize(&packet))) break;
    if (packet == 0) { Sleep(5); continue; }
    BYTE* data = nullptr;
    UINT32 frames = 0;
    DWORD f = 0;
    if (FAILED(cap->GetBuffer(&data, &frames, &f, nullptr, nullptr))) break;
    int n = (int)frames * channels_;
    scratch.resize(n);
    if (f & AUDCLNT_BUFFERFLAGS_SILENT) {
      std::fill(scratch.begin(), scratch.end(), 0.0f);
    } else {
      std::memcpy(scratch.data(), data, n * sizeof(float));
    }
    cap->ReleaseBuffer(frames);
    if (n > 0) on_data(scratch.data(), (int)frames);
  }
  client->Stop();
}

void WasapiCapture::stop() { running_ = false; }

void WasapiCapture::close() {
  if (capture_) { ((IAudioCaptureClient*)capture_)->Release(); capture_ = nullptr; }
  if (client_) { ((IAudioClient*)client_)->Release(); client_ = nullptr; }
}
