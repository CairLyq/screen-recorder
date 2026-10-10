#include "recorder/desktop_capture.h"
#include "common/log.h"
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <cstring>

using Microsoft::WRL::ComPtr;

bool DesktopCapture::init(int monitor_index, const Rect& region) {
  ComPtr<IDXGIFactory1> factory;
  if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&factory))) {
    LOGE("CreateDXGIFactory1 failed");
    return false;
  }
  // 枚举显示器
  UINT mon = 0;
  ComPtr<IDXGIAdapter1> adapter;
  ComPtr<IDXGIOutput> output;
  bool found = false;
  for (UINT a = 0; factory->EnumAdapters1(a, &adapter) != DXGI_ERROR_NOT_FOUND; ++a) {
    for (UINT o = 0; adapter->EnumOutputs(o, &output) != DXGI_ERROR_NOT_FOUND; ++o) {
      if ((int)mon == monitor_index) { found = true; break; }
      mon++;
      output.Reset();
    }
    if (found) break;
  }
  if (!found) { LOGE("monitor %d not found", monitor_index); return false; }

  DXGI_OUTPUT_DESC desc;
  output->GetDesc(&desc);
  mon_left_ = desc.DesktopCoordinates.left;
  mon_top_ = desc.DesktopCoordinates.top;
  int full_w = desc.DesktopCoordinates.right - desc.DesktopCoordinates.left;
  int full_h = desc.DesktopCoordinates.bottom - desc.DesktopCoordinates.top;

  ComPtr<IDXGIAdapter> ad;
  output->GetParent(__uuidof(IDXGIAdapter), (void**)&ad);
  D3D_FEATURE_LEVEL lvl;
  if (FAILED(D3D11CreateDevice(ad.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0,
                               nullptr, 0, D3D11_SDK_VERSION,
                               (ID3D11Device**)&device_, &lvl,
                               (ID3D11DeviceContext**)&context_))) {
    LOGE("D3D11CreateDevice failed");
    return false;
  }

  ComPtr<IDXGIOutput1> output1;
  output.As(&output1);
  if (FAILED(output1->DuplicateOutput((ID3D11Device*)device_,
                                      (IDXGIOutputDuplication**)&duplication_))) {
    LOGE("DuplicateOutput failed (权限/独占?)");
    return false;
  }

  crop_ = region.empty() ? Rect{0, 0, full_w, full_h} : region;
  if (crop_.x < 0) crop_.x = 0;
  if (crop_.y < 0) crop_.y = 0;
  if (crop_.x + crop_.w > full_w) crop_.w = full_w - crop_.x;
  if (crop_.y + crop_.h > full_h) crop_.h = full_h - crop_.y;
  tex_w_ = full_w;
  tex_h_ = full_h;

  D3D11_TEXTURE2D_DESC sd = {};
  sd.Width = full_w;
  sd.Height = full_h;
  sd.MipLevels = 1;
  sd.ArraySize = 1;
  sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
  sd.SampleDesc.Count = 1;
  sd.Usage = D3D11_USAGE_STAGING;
  sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  if (FAILED(((ID3D11Device*)device_)->CreateTexture2D(&sd, nullptr,
                                                       (ID3D11Texture2D**)&staging_))) {
    LOGE("CreateTexture2D(staging) failed");
    return false;
  }
  LOGI("capture init: monitor %d %dx%d crop(%d,%d,%d,%d)", monitor_index, full_w,
       full_h, crop_.x, crop_.y, crop_.w, crop_.h);
  return true;
}

bool DesktopCapture::next(CapturedFrame& out, int timeout_ms) {
  IDXGIOutputDuplication* dup = (IDXGIOutputDuplication*)duplication_;
  ID3D11DeviceContext* ctx = (ID3D11DeviceContext*)context_;

  DXGI_OUTDUPL_FRAME_INFO info;
  IDXGIResource* res = nullptr;
  HRESULT hr = dup->AcquireNextFrame(timeout_ms, &info, &res);
  if (hr == DXGI_ERROR_WAIT_TIMEOUT) return false;
  if (FAILED(hr)) {
    // AccessLost：重建由上层处理，这里返回 false
    return false;
  }

  ComPtr<ID3D11Texture2D> tex;
  res->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&tex);
  ctx->CopyResource((ID3D11Texture2D*)staging_, tex.Get());

  D3D11_MAPPED_SUBRESOURCE map;
  if (SUCCEEDED(ctx->Map((ID3D11Texture2D*)staging_, 0, D3D11_MAP_READ, 0, &map))) {
    out.width = crop_.w;
    out.height = crop_.h;
    out.bgra.resize((size_t)crop_.w * crop_.h * 4);
    const uint8_t* src = (const uint8_t*)map.pData;
    for (int r = 0; r < crop_.h; ++r) {
      const uint8_t* srow = src + (size_t)(crop_.y + r) * map.RowPitch +
                            (size_t)crop_.x * 4;
      std::memcpy(out.bgra.data() + (size_t)r * crop_.w * 4, srow,
                  (size_t)crop_.w * 4);
    }
    ctx->Unmap((ID3D11Texture2D*)staging_, 0);
  }
  dup->ReleaseFrame();
  res->Release();
  return true;
}

void DesktopCapture::close() {
  if (staging_) { ((ID3D11Texture2D*)staging_)->Release(); staging_ = nullptr; }
  if (duplication_) { ((IDXGIOutputDuplication*)duplication_)->Release(); duplication_ = nullptr; }
  if (context_) { ((ID3D11DeviceContext*)context_)->Release(); context_ = nullptr; }
  if (device_) { ((ID3D11Device*)device_)->Release(); device_ = nullptr; }
}
