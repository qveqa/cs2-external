// capcheck.cpp - prints the average colour of a screen region as seen through the dxgi
// desktop duplication path (what "display capture" in obs, discord and most recorders
// use). lets us verify the overlay's screen-capture bypass from the outside.
//
// build (from the repo root):
//   C:/w64devkit/bin/g++.exe tools/capcheck.cpp -std=c++20 -O2 \
//       -o %TEMP%/opencode/capcheck.exe -ldxgi -ld3d11 -ldxguid
//
// usage: capcheck [x y w h]   (defaults to the menu area)
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>

#include <cstdio>
#include <cstdlib>

int main(int argc, char** argv) {
    int rx = 830;
    int ry = 470;
    int rw = 260;
    int rh = 120;
    if (argc >= 5) {
        rx = std::atoi(argv[1]);
        ry = std::atoi(argv[2]);
        rw = std::atoi(argv[3]);
        rh = std::atoi(argv[4]);
    }

    ID3D11Device*        device  = nullptr;
    ID3D11DeviceContext* context = nullptr;
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
                                   D3D11_SDK_VERSION, &device, nullptr, &context);
    if (FAILED(hr)) {
        std::printf("d3d11 device failed (0x%08lX)\n", static_cast<unsigned long>(hr));
        return 1;
    }

    IDXGIDevice*  dxgi_device = nullptr;
    IDXGIAdapter* adapter     = nullptr;
    IDXGIOutput*  output      = nullptr;
    IDXGIOutput1* output1     = nullptr;
    IDXGIOutputDuplication* dup = nullptr;

    device->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&dxgi_device));
    if (dxgi_device) dxgi_device->GetAdapter(&adapter);
    if (adapter)     adapter->EnumOutputs(0, &output);
    if (output)      output->QueryInterface(__uuidof(IDXGIOutput1), reinterpret_cast<void**>(&output1));
    if (output1)     hr = output1->DuplicateOutput(device, &dup);

    if (!dup) {
        std::printf("duplicate output failed (0x%08lX) - another capture app holding it?\n",
                    static_cast<unsigned long>(hr));
        return 1;
    }

    long long sum_r = 0;
    long long sum_g = 0;
    long long sum_b = 0;
    int       samples   = 0;
    int       processed = 0;

    // the first frame after duplication starts is often stale: take a few
    for (int attempt = 0; attempt < 10 && processed < 3; ++attempt) {
        DXGI_OUTDUPL_FRAME_INFO info = {};
        IDXGIResource* resource = nullptr;
        hr = dup->AcquireNextFrame(300, &info, &resource);
        if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
            if (processed > 0) break;
            continue;
        }
        if (FAILED(hr)) {
            std::printf("acquire failed (0x%08lX)\n", static_cast<unsigned long>(hr));
            break;
        }

        ID3D11Texture2D* texture = nullptr;
        resource->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&texture));
        resource->Release();

        if (texture) {
            D3D11_TEXTURE2D_DESC desc = {};
            texture->GetDesc(&desc);
            desc.Usage          = D3D11_USAGE_STAGING;
            desc.BindFlags      = 0;
            desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            desc.MiscFlags      = 0;

            ID3D11Texture2D* staging = nullptr;
            if (SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &staging))) {
                context->CopyResource(staging, texture);

                D3D11_MAPPED_SUBRESOURCE map = {};
                if (SUCCEEDED(context->Map(staging, 0, D3D11_MAP_READ, 0, &map))) {
                    sum_r = sum_g = sum_b = 0;
                    for (int y = 0; y < rh; ++y) {
                        const unsigned char* row = static_cast<const unsigned char*>(map.pData) +
                                                   static_cast<size_t>(ry + y) * map.RowPitch;
                        for (int x = 0; x < rw; ++x) {
                            const unsigned char* px = row + static_cast<size_t>(rx + x) * 4;
                            sum_b += px[0];
                            sum_g += px[1];
                            sum_r += px[2];
                        }
                    }
                    samples = rw * rh;
                    context->Unmap(staging, 0);
                    processed++;
                }
                staging->Release();
            }
            texture->Release();
        }
        dup->ReleaseFrame();
    }

    if (samples == 0) {
        std::printf("no frame captured\n");
        return 1;
    }

    std::printf("region (%d,%d %dx%d) avg rgb = (%lld, %lld, %lld)\n",
                rx, ry, rw, rh, sum_r / samples, sum_g / samples, sum_b / samples);
    return 0;
}
