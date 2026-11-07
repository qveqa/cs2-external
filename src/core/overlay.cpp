// overlay.cpp - external overlay; touches the game with PROCESS_VM_READ only
#include "overlay.h"

#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>
#include <dwmapi.h>
#include <tlhelp32.h>

#include <string>
#include <vector>

#include <cstdio>

#include "func/esp/esp.h"
#include "func/aim/aim.h"
#include "func/aim/fov.h"
#include "func/aim/trigger.h"
#include "func/misc/bhop.h"
#include "func/misc/input.h"
#include "func/misc/quickstop.h"
#if !defined(BLUE_WHALE_NO_WEAPON_FONT) && __has_include("assets/weapon_icons.h")
#include "assets/weapon_icons.h"
#define BLUE_WHALE_HAS_WEAPON_FONT 1
#endif
#include "config.h"
#include "menu.h"

// the win32 backend intentionally leaves this out of its header;
// the app is expected to forward declare it before its wndproc
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace {

constexpr wchar_t kWindowClass[] = L"bluewhale_overlay_window";

// mingw headers do not always declare this one
const GUID kIID_IDCompositionDevice = {0xC37EA93A, 0xE7AA, 0x450D, {0xB1, 0x6F, 0x97, 0x46, 0xCB, 0x04, 0x07, 0xF3}};

// windows 10 2004+: the window stays on the monitor but vanishes from every screen
// capture path (dwm excludes it from what os-level and api-based captures see)
#ifndef WDA_EXCLUDEFROMCAPTURE
#define WDA_EXCLUDEFROMCAPTURE 0x00000011
#endif

std::string Utf8FromWide(const wchar_t* wide) {
    if (!wide || !*wide) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
    if (size <= 0) return {};
    std::string out(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide, -1, out.data(), size, nullptr, nullptr);
    out.pop_back();  // drop the terminating zero
    return out;
}

std::string SystemFontPath(const wchar_t* file_name) {
    wchar_t dir[MAX_PATH];
    if (!GetWindowsDirectoryW(dir, MAX_PATH)) return {};
    const std::wstring path = std::wstring(dir) + L"\\Fonts\\" + file_name;
    return Utf8FromWide(path.c_str());
}

std::wstring Utf8ToWide(const std::string& text) {
    if (text.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
    if (size <= 1) return {};
    std::wstring out(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, out.data(), size);
    out.pop_back();   // drop the terminating zero
    return out;
}

// a net-zero relative nudge: the game re-captures the mouse on fresh input
void NudgeMouseCapture() {
    INPUT inputs[2] = {};
    inputs[0].type       = INPUT_MOUSE;
    inputs[0].mi.dwFlags = MOUSEEVENTF_MOVE;
    inputs[0].mi.dx      = 2;
    inputs[1]            = inputs[0];
    inputs[1].mi.dx      = -2;
    SendInput(2, inputs, sizeof(INPUT));
}

}  // namespace

bool Overlay::RefreshTargetConfig() {
    wchar_t exe_path[MAX_PATH];
    if (!GetModuleFileNameW(nullptr, exe_path, MAX_PATH)) return false;

    std::wstring directory = exe_path;
    const size_t slash = directory.find_last_of(L"\\/");
    if (slash == std::wstring::npos) return false;
    directory.resize(slash + 1);
    exe_dir_ = directory;

    // default target is cs2.exe; target.txt overrides it. the file is re-read while
    // running, so creating or removing it switches the target without a restart.
    std::wstring wide_name = L"cs2.exe";
    const std::wstring config_path = directory + L"target.txt";
    HANDLE file = CreateFileW(config_path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        char buffer[256] = {};
        DWORD bytes_read = 0;
        const bool read_ok = ReadFile(file, buffer, sizeof(buffer) - 1, &bytes_read, nullptr) != FALSE;
        CloseHandle(file);

        if (read_ok && bytes_read > 0) {
            std::string name(buffer, buffer + bytes_read);
            const size_t line_end = name.find_first_of("\r\n");
            if (line_end != std::string::npos) name.resize(line_end);
            while (!name.empty() && (name.back() == ' ' || name.back() == '\t')) name.pop_back();

            const std::wstring parsed = Utf8ToWide(name);
            if (!parsed.empty()) wide_name = parsed;
        }
    }

    if (wide_name == game_exe_name_) return false;

    game_exe_name_ = wide_name;
    game_.Detach();
    game_frame_ = GameFrame{};
    game_window_ = nullptr;
    next_window_find_ms_ = 0;
    next_attach_try_ms_ = 0;
    return true;
}

bool Overlay::Initialize(HINSTANCE instance) {
    instance_ = instance;

    RefreshTargetConfig();
    if (!exe_dir_.empty()) {
        settings_path_ = exe_dir_ + L"settings.ini";
        LoadSettings(settings_, settings_path_);
    }

    if (!CreateDevice()) return false;

    // composition route first: true per-pixel alpha, hairline drawings composite cleanly
    if (CreateOverlayWindow(true) && CreateCompositionSwapChain()) {
        per_pixel_alpha_ = true;
    } else {
        // fallback: layered window with a color key
        ReleaseComposition();
        if (swapchain_) { swapchain_->Release(); swapchain_ = nullptr; }
        if (hwnd_) { DestroyWindow(hwnd_); hwnd_ = nullptr; }

        if (!CreateOverlayWindow(false)) return false;
        if (!CreateColorKeySwapChain()) return false;
        per_pixel_alpha_ = false;
    }

    if (!CreateRenderTarget()) return false;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;   // no imgui.ini next to the exe
    io.LogFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ApplyMenuStyle();

    // menu font: semibold reads cleanly on the dark panel
    std::string menu_font_path = SystemFontPath(L"seguisb.ttf");
    if (menu_font_path.empty())
        menu_font_path = SystemFontPath(L"segoeui.ttf");

    ImFont* menu_font = menu_font_path.empty()
        ? nullptr
        : io.Fonts->AddFontFromFileTTF(menu_font_path.c_str(), 16.0f, nullptr, io.Fonts->GetGlyphRangesCyrillic());
    if (!menu_font)
        io.Fonts->AddFontDefault();

    // esp font: small regular weight, antialiased thin strokes over the game
    const std::string esp_font_path = SystemFontPath(L"segoeui.ttf");
    if (!esp_font_path.empty())
        esp_font_ = io.Fonts->AddFontFromFileTTF(esp_font_path.c_str(), esp_text_size_, nullptr,
                                                 io.Fonts->GetGlyphRangesCyrillic());

    // weapon icon font: gun silhouettes addressed by ascii letters/digits.
    // a writable copy keeps the (dynamic) atlas away from the read-only embedded blob
#ifdef BLUE_WHALE_HAS_WEAPON_FONT
    {
        static std::vector<unsigned char> icon_font_data(kWeaponIconFont,
                                                         kWeaponIconFont + kWeaponIconFontSize);
        ImFontConfig icon_cfg;
        icon_cfg.FontDataOwnedByAtlas = false;
        static const ImWchar icon_ranges[] = { 0x20, 0x7F, 0 };
        esp_icon_font_ = io.Fonts->AddFontFromMemoryTTF(icon_font_data.data(),
                                                        static_cast<int>(icon_font_data.size()),
                                                        esp_icon_size_, &icon_cfg, icon_ranges);
    }
#endif

    if (!ImGui_ImplWin32_Init(hwnd_)) return false;
    if (!ImGui_ImplDX11_Init(device_, context_)) return false;
    imgui_ready_ = true;
    return true;
}

bool Overlay::CreateOverlayWindow(bool composition) {
    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = &Overlay::WndProcThunk;
    wc.hInstance     = instance_;
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kWindowClass;
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;

    MONITORINFO monitor{};
    monitor.cbSize = sizeof(monitor);
    if (GetMonitorInfoW(MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY), &monitor)) {
        screen_left_   = monitor.rcMonitor.left;
        screen_top_    = monitor.rcMonitor.top;
        screen_width_  = monitor.rcMonitor.right - monitor.rcMonitor.left;
        screen_height_ = monitor.rcMonitor.bottom - monitor.rcMonitor.top;
    } else {
        screen_width_  = GetSystemMetrics(SM_CXSCREEN);
        screen_height_ = GetSystemMetrics(SM_CYSCREEN);
    }

    // WS_EX_LAYERED is what makes WS_EX_TRANSPARENT actually pass the mouse through:
    // without it the full-screen window swallows every click on the desktop
    const DWORD ex_style = WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED |
                           (composition ? WS_EX_NOREDIRECTIONBITMAP : 0);
    hwnd_ = CreateWindowExW(
        ex_style, kWindowClass, L"blue whale", WS_POPUP,
        screen_left_, screen_top_, screen_width_, screen_height_,
        nullptr, nullptr, instance_, this);
    if (!hwnd_) return false;

    // color key path: pure black is transparent and click-through
    if (!composition)
        SetLayeredWindowAttributes(hwnd_, RGB(0, 0, 0), 255, LWA_COLORKEY);

    SetClickThrough(true);
    return true;
}

bool Overlay::CreateDevice() {
    const D3D_FEATURE_LEVEL levels[] = {
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
    };
    D3D_FEATURE_LEVEL obtained{};
    const HRESULT hr = D3D11CreateDevice(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
        levels, static_cast<UINT>(sizeof(levels) / sizeof(levels[0])), D3D11_SDK_VERSION,
        &device_, &obtained, &context_);
    return SUCCEEDED(hr);
}

namespace {

// shared factory lookup for both swap chain flavours
IDXGIFactory2* GetFactory(ID3D11Device* device, IDXGIDevice** dxgi_device_out, IDXGIAdapter** adapter_out) {
    *dxgi_device_out = nullptr;
    *adapter_out     = nullptr;

    IDXGIDevice* dxgi_device = nullptr;
    if (FAILED(device->QueryInterface(IID_IDXGIDevice, reinterpret_cast<void**>(&dxgi_device)))) return nullptr;

    IDXGIAdapter* adapter = nullptr;
    dxgi_device->GetAdapter(&adapter);

    IDXGIFactory2* factory = nullptr;
    if (adapter) adapter->GetParent(IID_IDXGIFactory2, reinterpret_cast<void**>(&factory));

    if (!factory) {
        if (adapter) adapter->Release();
        dxgi_device->Release();
        return nullptr;
    }

    *dxgi_device_out = dxgi_device;
    *adapter_out     = adapter;
    return factory;
}

}  // namespace

bool Overlay::CreateCompositionSwapChain() {
    IDXGIDevice*  dxgi_device = nullptr;
    IDXGIAdapter* adapter     = nullptr;
    IDXGIFactory2* factory    = GetFactory(device_, &dxgi_device, &adapter);
    if (!factory) return false;

    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width            = static_cast<UINT>(screen_width_);
    desc.Height           = static_cast<UINT>(screen_height_);
    desc.Format           = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage      = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount      = 2;
    desc.SwapEffect       = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    desc.AlphaMode        = DXGI_ALPHA_MODE_PREMULTIPLIED;
    desc.Scaling          = DXGI_SCALING_STRETCH;

    IDXGISwapChain1* swapchain1 = nullptr;
    bool ok = SUCCEEDED(factory->CreateSwapChainForComposition(device_, &desc, nullptr, &swapchain1));
    if (ok) swapchain_ = swapchain1;
    if (ok)
        ok = SUCCEEDED(DCompositionCreateDevice(dxgi_device, kIID_IDCompositionDevice,
                                                reinterpret_cast<void**>(&dcomp_device_)));
    if (ok)
        ok = SUCCEEDED(dcomp_device_->CreateTargetForHwnd(hwnd_, TRUE, &dcomp_target_));
    if (ok)
        ok = SUCCEEDED(dcomp_device_->CreateVisual(&dcomp_visual_));
    if (ok)
        ok = SUCCEEDED(dcomp_visual_->SetContent(swapchain_));
    if (ok)
        ok = SUCCEEDED(dcomp_target_->SetRoot(dcomp_visual_));
    if (ok)
        ok = SUCCEEDED(dcomp_device_->Commit());

    factory->Release();
    if (adapter) adapter->Release();
    if (dxgi_device) dxgi_device->Release();
    return ok;
}

bool Overlay::CreateColorKeySwapChain() {
    IDXGIDevice*  dxgi_device = nullptr;
    IDXGIAdapter* adapter     = nullptr;
    IDXGIFactory2* factory    = GetFactory(device_, &dxgi_device, &adapter);
    if (!factory) return false;

    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width            = static_cast<UINT>(screen_width_);
    desc.Height           = static_cast<UINT>(screen_height_);
    desc.Format           = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage      = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount      = 2;
    desc.SwapEffect       = DXGI_SWAP_EFFECT_DISCARD;   // blt model pairs with a layered color key
    desc.Scaling          = DXGI_SCALING_STRETCH;
    desc.AlphaMode        = DXGI_ALPHA_MODE_UNSPECIFIED;

    IDXGISwapChain1* swapchain1 = nullptr;
    const bool ok = SUCCEEDED(factory->CreateSwapChainForHwnd(device_, hwnd_, &desc, nullptr, nullptr, &swapchain1));
    if (ok) {
        swapchain_ = swapchain1;
    }

    factory->Release();
    if (adapter) adapter->Release();
    if (dxgi_device) dxgi_device->Release();
    return ok;
}

void Overlay::ReleaseComposition() {
    if (dcomp_visual_) { dcomp_visual_->Release(); dcomp_visual_ = nullptr; }
    if (dcomp_target_) { dcomp_target_->Release(); dcomp_target_ = nullptr; }
    if (dcomp_device_) { dcomp_device_->Release(); dcomp_device_ = nullptr; }
}

bool Overlay::CreateRenderTarget() {
    DestroyRenderTarget();

    // a composition swap chain exposes only its first buffer through the api and hands
    // back the same texture on every present, so one view is all there is to cache
    ID3D11Texture2D* back_buffer = nullptr;
    if (FAILED(swapchain_->GetBuffer(0, IID_ID3D11Texture2D, reinterpret_cast<void**>(&back_buffer))))
        return false;

    const HRESULT view_hr = device_->CreateRenderTargetView(back_buffer, nullptr, &render_targets_[0]);
    back_buffer->Release();
    if (FAILED(view_hr)) return false;

    render_target_count_ = 1;
    return true;
}

void Overlay::DestroyRenderTarget() {
    for (ID3D11RenderTargetView*& view : render_targets_) {
        if (view) {
            view->Release();
            view = nullptr;
        }
    }
    render_target_count_ = 0;
}

ID3D11RenderTargetView* Overlay::CurrentRenderTarget() const {
    return render_target_count_ > 0 ? render_targets_[0] : nullptr;
}

bool Overlay::UpdateOverlayBounds(bool force) {
    if (!hwnd_) return false;

    // follow the game window when it moves to another monitor; without a game the
    // overlay stays on the monitor it is on now
    const HWND anchor = (game_window_ && IsWindow(game_window_)) ? game_window_ : hwnd_;
    MONITORINFO monitor{};
    monitor.cbSize = sizeof(monitor);
    if (!GetMonitorInfoW(MonitorFromWindow(anchor, MONITOR_DEFAULTTOPRIMARY), &monitor)) return false;

    const int left   = monitor.rcMonitor.left;
    const int top    = monitor.rcMonitor.top;
    const int width  = monitor.rcMonitor.right - monitor.rcMonitor.left;
    const int height = monitor.rcMonitor.bottom - monitor.rcMonitor.top;
    if (width <= 0 || height <= 0) return false;

    if (!force && left == screen_left_ && top == screen_top_ &&
        width == screen_width_ && height == screen_height_)
        return false;

    const bool size_changed = width != screen_width_ || height != screen_height_;

    screen_left_   = left;
    screen_top_    = top;
    screen_width_  = width;
    screen_height_ = height;

    if (swapchain_ && size_changed) {
        DestroyRenderTarget();
        const HRESULT hr = swapchain_->ResizeBuffers(0, static_cast<UINT>(width), static_cast<UINT>(height),
                                                     DXGI_FORMAT_UNKNOWN, 0);
        if (SUCCEEDED(hr))
            CreateRenderTarget();
    }

    SetWindowPos(hwnd_, HWND_TOPMOST, left, top, width, height,
                 SWP_NOACTIVATE | (window_shown_ ? SWP_SHOWWINDOW : 0));
    if (dcomp_device_) dcomp_device_->Commit();
    return true;
}

void Overlay::OnDisplayChanged() {
    UpdateOverlayBounds(true);
}

bool Overlay::ComputeGameViewport(GameViewport& viewport) const {
    viewport = GameViewport{};

    if (!game_window_ || !IsWindow(game_window_)) {
        // no game yet: the menu still uses the whole overlay
        viewport.width  = static_cast<float>(screen_width_);
        viewport.height = static_cast<float>(screen_height_);
        return viewport.valid();
    }

    if (IsIconic(game_window_)) return false;

    RECT client{};
    if (!GetClientRect(game_window_, &client)) return false;

    POINT origin{ 0, 0 };
    if (!ClientToScreen(game_window_, &origin)) return false;

    const float width  = static_cast<float>(client.right - client.left);
    const float height = static_cast<float>(client.bottom - client.top);
    if (width <= 0.0f || height <= 0.0f) return false;

    viewport.x      = static_cast<float>(origin.x - screen_left_);
    viewport.y      = static_cast<float>(origin.y - screen_top_);
    viewport.width  = width;
    viewport.height = height;
    return true;
}

bool Overlay::HandleInput() {
    const bool pg_down  = (GetAsyncKeyState(VK_NEXT) & 0x8000) != 0;   // PgDn: menu toggle
    const bool end_down = (GetAsyncKeyState(VK_END) & 0x8000) != 0;    // End: unload

    if (pg_down && !hotkey_down_)
        SetMenuVisible(!menu_visible_);
    hotkey_down_ = pg_down;

    const bool unload = end_down && !unload_key_down_;
    unload_key_down_ = end_down;
    return unload;
}

void Overlay::SetMenuVisible(bool visible) {
    if (menu_visible_ == visible) return;
    menu_visible_ = visible;

    if (visible) {
        // key-ups must go out while the game still owns the foreground; once the
        // menu steals it, a release would land on the overlay and strand a key
        BhopRelease();
        QuickstopReleaseKeys();

        restore_target_ = GetForegroundWindow();
        game_window_    = FindGameWindow();
        SyncWindowState();
        StealFocus();   // game loses foreground: raw input stops, camera freezes
    } else {
        SyncWindowState();   // stays visible while the esp is on; clicks pass through again
        ReleaseCapture();    // the ui may still hold the win32 mouse capture from a drag
        CancelBindCapture(); // a capture armed in the menu must not outlive it
        SaveSettings(settings_, settings_path_);

        HWND target = (restore_target_ && restore_target_ != hwnd_ && IsWindow(restore_target_))
                          ? restore_target_ : nullptr;
        restore_target_ = nullptr;

        if (target) {
            focus_return_        = target;   // finished by UpdateFocusReturn across the next frames
            focus_return_wait_   = 30;       // ~0.5s of retries is plenty
            focus_return_nudges_ = 4;
            focus_return_gap_    = 0;        // first nudge right away, then spaced out
            ReturnFocus(target);
        }
    }
}

#ifdef BLUE_WHALE_VIS_TIMING
// temporary diagnostic: for every enemy it logs how long after the head first
// projects on screen the engine's spotted flag flips - the visible-check latency
// floor - into visible_timing.txt next to the exe
void LogVisibleTiming(const GameFrame& frame, const GameViewport& viewport) {
    struct Track {
        uintptr_t          controller    = 0;
        bool               on_screen     = false;
        bool               spotted       = false;
        unsigned long long on_since_ms   = 0;
    };
    static Track tracks[24];
    static FILE* log_file   = nullptr;
    static bool  log_opened = false;

    if (!log_opened) {
        log_opened = true;
        wchar_t path[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, path, MAX_PATH);
        if (wchar_t* slash = wcsrchr(path, L'\\'))
            slash[1] = L'\0';
        wcscat_s(path, L"visible_timing.txt");
        log_file = _wfopen(path, L"w");
    }
    if (!log_file || !frame.has_local || !viewport.valid()) return;

    const unsigned long long now = GetTickCount64();

    for (const EspPlayer& player : frame.players) {
        if (frame.has_local_team && player.team == frame.local_team) continue;

        bool on_screen = false;
        {
            const Vec3& w = player.bones[kHeadBoneIndex];
            const auto& m = frame.view.m;
            const float w_comp = m[3][0] * w.x + m[3][1] * w.y + m[3][2] * w.z + m[3][3];
            if (w_comp > 0.01f) {
                const float x = m[0][0] * w.x + m[0][1] * w.y + m[0][2] * w.z + m[0][3];
                const float y = m[1][0] * w.x + m[1][1] * w.y + m[1][2] * w.z + m[1][3];
                const float sx = viewport.center_x() + viewport.width * 0.5f * (x / w_comp);
                const float sy = viewport.center_y() - viewport.height * 0.5f * (y / w_comp);
                on_screen = sx >= viewport.x && sx < viewport.x + viewport.width &&
                            sy >= viewport.y && sy < viewport.y + viewport.height;
            }
        }

        Track* track = nullptr;
        for (Track& t : tracks) {
            if (t.controller == player.controller) {
                track = &t;
                break;
            }
        }
        if (!track) {
            for (Track& t : tracks) {
                if (t.controller == 0) {
                    t.controller = player.controller;
                    track = &t;
                    break;
                }
            }
        }
        if (!track) continue;

        if (on_screen && !track->on_screen)
            track->on_since_ms = now;
        if (!on_screen)
            track->on_since_ms = 0;

        if (player.spotted && !track->spotted) {
            const long long dt = track->on_since_ms
                                     ? static_cast<long long>(now - track->on_since_ms)
                                     : -1;
            std::fprintf(log_file, "spotted: +%4lld ms after on-screen\n", dt);
            std::fflush(log_file);
        } else if (!player.spotted && track->spotted) {
            std::fprintf(log_file, "unspotted\n");
            std::fflush(log_file);
        }

        track->on_screen = on_screen;
        track->spotted   = player.spotted;
    }
}
#endif

void Overlay::SyncWindowState() {
    const bool esp_on = EspActive();

    // the game window is needed for the show logic and for the aim: resolve it (throttled)
    if (!game_window_ || !IsWindow(game_window_)) {
        const unsigned long long now = GetTickCount64();
        if (now >= next_window_find_ms_) {
            next_window_find_ms_ = now + 1000;
            game_window_ = FindGameWindow();
        }
    }

    // the overlay follows the game to whichever monitor it runs on
    UpdateOverlayBounds(false);

    bool should_show = menu_visible_;
    const bool fov_visual = settings_.aim.enabled && settings_.aim.draw_fov;
    if (!should_show && (esp_on || fov_visual)) {
        // like other external overlays: draw only while the game (or our own window) is in
        // front. hidden on the desktop — nothing can sit on top of other apps and eat clicks
        const HWND foreground = GetForegroundWindow();
        should_show = foreground && (foreground == hwnd_ || foreground == game_window_);
    }
    if (should_show && !window_shown_) {
        SetWindowPos(hwnd_, HWND_TOPMOST, screen_left_, screen_top_, screen_width_, screen_height_,
                     SWP_SHOWWINDOW | SWP_NOACTIVATE);
        window_shown_ = true;
    } else if (!should_show && window_shown_) {
        ShowWindow(hwnd_, SW_HIDE);
        window_shown_ = false;
    }

    // interactive only while the menu is open; otherwise every click belongs to the game
    SetClickThrough(!menu_visible_);
}

void Overlay::ApplyCaptureAffinity() {
    if (!hwnd_) return;

    const bool want = settings_.misc.screen_capture_bypass;
    if (affinity_applied_ && affinity_value_ == want) return;

    const unsigned long long now = GetTickCount64();
    if (now < next_affinity_ms_) return;

    // exclude the window from capture entirely; older systems can only hide the content
    // behind a black box (WDA_MONITOR), which still keeps the esp out of the capture
    BOOL ok = SetWindowDisplayAffinity(hwnd_, want ? WDA_EXCLUDEFROMCAPTURE : WDA_NONE);
    if (!ok && want)
        ok = SetWindowDisplayAffinity(hwnd_, WDA_MONITOR);

    if (ok) {
        affinity_applied_ = true;
        affinity_value_   = want;
    } else {
        next_affinity_ms_ = now + 2000;   // unsupported here: retry rarely, never spam
    }
}

void Overlay::SetClickThrough(bool enabled) {
    if (click_through_ == enabled || !hwnd_) return;
    click_through_ = enabled;

    LONG_PTR style = GetWindowLongPtrW(hwnd_, GWL_EXSTYLE);
    if (enabled)
        style |= WS_EX_TRANSPARENT;
    else
        style &= ~WS_EX_TRANSPARENT;
    SetWindowLongPtrW(hwnd_, GWL_EXSTYLE, style);
}

void Overlay::EnforceFocusBlock() {
    if (!menu_visible_) return;
    if (!game_window_ || !IsWindow(game_window_))
        game_window_ = FindGameWindow();
    if (!game_window_) return;

    // the game can sneak back to the foreground (win key, alt-tab dance):
    // while the menu is open it never keeps focus
    if (GetForegroundWindow() == game_window_)
        StealFocus();
}

void Overlay::StealFocus() {
    HWND foreground = GetForegroundWindow();
    const DWORD foreground_thread = foreground ? GetWindowThreadProcessId(foreground, nullptr) : 0;
    const DWORD overlay_thread    = GetWindowThreadProcessId(hwnd_, nullptr);

    // attach trick: lets a background process raise its window reliably
    if (foreground_thread && foreground_thread != overlay_thread)
        AttachThreadInput(foreground_thread, overlay_thread, TRUE);

    BringWindowToTop(hwnd_);
    SetForegroundWindow(hwnd_);
    SetFocus(hwnd_);

    if (foreground_thread && foreground_thread != overlay_thread)
        AttachThreadInput(foreground_thread, overlay_thread, FALSE);
}

// give the foreground back; windows sometimes refuses a one-shot call,
// so the caller retries across frames (see UpdateFocusReturn)
void Overlay::ReturnFocus(HWND target) {
    if (!target || !IsWindow(target) || GetForegroundWindow() == target) return;

    const HWND  foreground  = GetForegroundWindow();
    const DWORD fg_thread   = foreground ? GetWindowThreadProcessId(foreground, nullptr) : 0;
    const DWORD tgt_thread  = GetWindowThreadProcessId(target, nullptr);
    const DWORD our_thread  = GetCurrentThreadId();

    // join input queues with both sides: the classic recipe for a reliable hand-back
    if (fg_thread && fg_thread != our_thread && fg_thread != tgt_thread)
        AttachThreadInput(fg_thread, our_thread, TRUE);
    if (tgt_thread && tgt_thread != our_thread && tgt_thread != fg_thread)
        AttachThreadInput(tgt_thread, our_thread, TRUE);

    SetForegroundWindow(target);

    if (tgt_thread && tgt_thread != our_thread && tgt_thread != fg_thread)
        AttachThreadInput(tgt_thread, our_thread, FALSE);
    if (fg_thread && fg_thread != our_thread && fg_thread != tgt_thread)
        AttachThreadInput(fg_thread, our_thread, FALSE);
}

// runs every frame: hands input back, then nudges the mouse once the game owns it
// again (the game re-captures only on fresh input, and only while it is in front)
void Overlay::UpdateFocusReturn() {
    if (!focus_return_) return;

    if (menu_visible_ || !IsWindow(focus_return_) || --focus_return_wait_ <= 0) {
        focus_return_ = nullptr;   // menu reopened or the window died: stop trying
        return;
    }

    if (GetForegroundWindow() != focus_return_) {
        ReturnFocus(focus_return_);   // keep retrying until the switch actually lands
        return;
    }

    if (focus_return_nudges_ > 0) {
        if (focus_return_gap_ > 0) {
            --focus_return_gap_;
        } else {
            // space the nudges: the game needs a beat after activation before it listens again
            NudgeMouseCapture();
            --focus_return_nudges_;
            focus_return_gap_ = 2;
        }
    } else {
        focus_return_ = nullptr;
    }
}

void Overlay::TryAttachGame() {
    const unsigned long long now = GetTickCount64();
    if (now >= next_target_reload_ms_) {
        next_target_reload_ms_ = now + 2000;
        RefreshTargetConfig();
    }

    if (game_.attached() && !game_.alive())
        game_.Detach();
    if (game_.attached()) return;

    if (now < next_attach_try_ms_) return;
    next_attach_try_ms_ = now + 2000;   // the game may start later than the overlay

    game_.Attach(game_exe_name_.c_str());
}

// DwmFlush does not always block: with a hidden or occluded window some drivers return
// at once, and an un-paced loop then runs thousands of times a second and starves the
// game's own frame pacing. Sleep until just before the next composition boundary and
// leave the last fraction of a millisecond to DwmFlush.
void Overlay::WaitForRefresh() {
    if (!frame_timer_) {
        frame_timer_ = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                                              TIMER_ALL_ACCESS);
        if (!frame_timer_)
            frame_timer_ = CreateWaitableTimerW(nullptr, FALSE, nullptr);   // 15.6ms ticks: coarse but honest
    }

    LARGE_INTEGER freq{};
    QueryPerformanceFrequency(&freq);

    long long period = 0;
    DWM_TIMING_INFO timing{};
    timing.cbSize = sizeof(timing);
    if (SUCCEEDED(DwmGetCompositionTimingInfo(nullptr, &timing)))
        period = static_cast<long long>(timing.qpcRefreshPeriod);
    if (period <= 0 || period < freq.QuadPart / 1000 || period > freq.QuadPart / 10)
        period = freq.QuadPart / 60;   // no usable timing info: assume 60hz

    auto qpc = [] {
        LARGE_INTEGER t;
        QueryPerformanceCounter(&t);
        return t.QuadPart;
    };

    const long long now = qpc();
    // a long stall (menu, load screen) must not be chased with a burst of catch-up frames
    if (next_frame_qpc_ <= 0 || next_frame_qpc_ > now + period * 2 || next_frame_qpc_ < now - period * 2)
        next_frame_qpc_ = now;
    next_frame_qpc_ += period;

    const long long spin_lead = freq.QuadPart / 1600;   // ~0.6ms of spin ahead of the boundary
    for (;;) {
        const long long remaining = next_frame_qpc_ - qpc();
        if (remaining <= spin_lead) return;

        const long long due_100ns = (remaining - spin_lead) * 10000000LL / freq.QuadPart;
        LARGE_INTEGER due{};
        due.QuadPart = -due_100ns;
        if (frame_timer_ && SetWaitableTimer(frame_timer_, &due, 0, nullptr, nullptr, FALSE)) {
            WaitForSingleObject(frame_timer_, 50);
        } else {
            Sleep(static_cast<DWORD>((due_100ns + 9999) / 10000));
        }
    }
}

DWORD Overlay::FindGameProcess() const {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return 0;

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);

    DWORD pid = 0;
    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (_wcsicmp(entry.szExeFile, game_exe_name_.c_str()) == 0) {
                pid = entry.th32ProcessID;
                break;
            }
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return pid;
}

HWND Overlay::FindGameWindow() const {
    const DWORD pid = FindGameProcess();
    if (pid == 0) return nullptr;

    // the process owns several top-level windows (ime, helpers) — the game itself is the biggest one
    struct Search {
        DWORD     pid;
        HWND      window;
        long long area;
    };
    Search search{pid, nullptr, 0};

    EnumWindows([](HWND hwnd, LPARAM param) -> BOOL {
        auto* s = reinterpret_cast<Search*>(param);
        DWORD window_pid = 0;
        GetWindowThreadProcessId(hwnd, &window_pid);
        if (window_pid != s->pid) return TRUE;
        if (!IsWindowVisible(hwnd)) return TRUE;
        if ((GetWindowLongPtrW(hwnd, GWL_STYLE) & WS_CHILD) != 0) return TRUE;

        RECT rect{};
        if (!GetClientRect(hwnd, &rect)) return TRUE;
        const long long area = static_cast<long long>(rect.right - rect.left) * (rect.bottom - rect.top);
        if (area > s->area) {
            s->area   = area;
            s->window = hwnd;
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&search));

    return search.window;
}

bool Overlay::RenderFrame(bool& want_close, const GameViewport& viewport) {
    want_close = false;

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    if (EspActive())
        DrawEsp(game_frame_, settings_, viewport, esp_font_, esp_text_size_, esp_icon_font_, esp_icon_size_);

    // the aim fov circle around the crosshair (follows the current weapon's override)
    if (settings_.aim.enabled && settings_.aim.draw_fov && viewport.valid()) {
        const AimSettings aim = EffectiveAim(settings_, game_frame_.local_weapon_index);
        ImDrawList* draw = ImGui::GetBackgroundDrawList();
        draw->PushClipRect(ImVec2(viewport.x, viewport.y),
                           ImVec2(viewport.x + viewport.width, viewport.y + viewport.height), true);
        draw->AddCircle(ImVec2(viewport.center_x(), viewport.center_y()),
                        AimFovRadiusPixels(aim.fov, game_frame_.view, viewport),
                        IM_COL32(235, 235, 235, 120), 96, 1.0f);
        draw->PopClipRect();
    }

#ifdef BLUE_WHALE_ESP_DEBUG
    {
        const ReaderStats& stats = game_.stats();
        char target_utf8[128] = {};
        WideCharToMultiByte(CP_UTF8, 0, game_exe_name_.c_str(), -1, target_utf8, sizeof(target_utf8), nullptr, nullptr);

        const DWORD found_pid = FindGameProcess();

        char text[640];
        std::snprintf(text, sizeof(text),
                      "blue whale esp debug\n"
                      "target: %s   found pid: %lu\n"
                      "attached: %s   pid: %lu\n"
                      "entity stride: 0x%X\n"
                      "bone ptr off: 0x%X   bone pos off: 0x%X\n"
                      "players: %d   with bones: %d\n"
                      "visible: %s",
                      target_utf8, static_cast<unsigned long>(found_pid),
                      stats.attached ? "yes" : "no", stats.pid,
                      stats.entity_stride, stats.bone_array_offset, stats.bone_pos_offset,
                      stats.players_found, stats.bones_ok,
                      stats.visible_mask ? "mask" : "spotted");
        ImGui::GetBackgroundDrawList()->AddText(ImVec2(20.0f, 220.0f), IM_COL32(255, 210, 110, 255), text);
    }
#endif
    if (menu_visible_) {
        DrawMenu(settings_, want_close, game_frame_.local_weapon_index,
                 game_frame_.has_mouse_settings ? &game_frame_.mouse_settings : nullptr);
        DrawSpectatorsWindow(game_frame_, settings_);
    }

    ImGui::Render();

    ID3D11RenderTargetView* render_target = CurrentRenderTarget();
    if (!render_target) return false;

    // per-pixel alpha path clears to zero alpha, color key path clears to black
    const float clear[4] = { 0.0f, 0.0f, 0.0f, per_pixel_alpha_ ? 0.0f : 1.0f };
    context_->OMSetRenderTargets(1, &render_target, nullptr);
    context_->ClearRenderTargetView(render_target, clear);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

    const HRESULT hr = swapchain_->Present(0, 0);   // no vsync stall: latency is set by the compositor
    if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET)
        return false;   // device lost: bail out

    return true;
}

#ifdef BLUE_WHALE_PERF_DIAG
struct DiagClock {
    LARGE_INTEGER freq{};
    long long     last = 0;
    double        acc[8] = {};
    int           frames = 0;
    unsigned long long next_ms = 0;
    double        thread_ms = 0.0;

    DiagClock() {
        QueryPerformanceFrequency(&freq);
        next_ms = GetTickCount64() + 1000;
    }
    long long now() const { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart; }
    void add(int slot, long long from, long long to) {
        acc[slot] += static_cast<double>(to - from) * 1000.0 / static_cast<double>(freq.QuadPart);
    }
    double thread_cpu_ms() const {
        FILETIME create{}, exit{}, kernel{}, user{};
        if (!GetThreadTimes(GetCurrentThread(), &create, &exit, &kernel, &user)) return 0.0;
        auto to_ms = [](const FILETIME& ft) {
            return (static_cast<double>(ft.dwHighDateTime) * 4294967296.0 + ft.dwLowDateTime) / 10000.0;
        };
        return to_ms(kernel) + to_ms(user);
    }
    void dump(const char* tag) {
        const double t = thread_cpu_ms();
        const double cpu = t - thread_ms;
        thread_ms = t;
        FILE* f = nullptr;
        if (fopen_s(&f, "perf.log", "a") == 0 && f) {
            std::fprintf(f, "%s fps=%d thread_cpu=%.0fms dwm=%.2f read=%.2f logic=%.2f draw=%.2f\n",
                         tag, frames, cpu, acc[0] / (frames ? frames : 1), acc[1] / (frames ? frames : 1),
                         acc[2] / (frames ? frames : 1), acc[3] / (frames ? frames : 1));
            std::fclose(f);
        }
        for (double& a : acc) a = 0.0;
        frames = 0;
    }
};

#endif

int Overlay::Run() {
    bool want_close = false;

    #ifdef BLUE_WHALE_PERF_DIAG
    DiagClock diag;
    #endif

    while (true) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) return 0;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        if (HandleInput()) return 0;   // End: unload

        // unconditionally: a synthetic click must never stay held, whatever the settings
        // are by now (menu opened mid-shot, trigger switched off, unloading)
        TriggerReleasePendingClick();

        SyncWindowState();
        ApplyCaptureAffinity();
        UpdateFocusReturn();   // finish handing input back even while the esp is off

        const bool esp_on = EspActive();
        const bool aim_on = settings_.aim.enabled;
        const bool trigger_on = settings_.trigger.enabled;
        const bool quickstop_on = settings_.misc.quickstop.enabled;
        const bool bhop_on = settings_.misc.bhop.enabled;
        // the open menu also needs the frame: it shows the local player's weapon
        const bool want_frame = esp_on || aim_on || trigger_on || quickstop_on || bhop_on || menu_visible_;
        if (menu_visible_ || !aim_on) AimReset();
        if (menu_visible_ || !trigger_on) TriggerReset();
        if (!want_frame) {
            BhopRelease();
            QuickstopReleaseKeys();   // idle: nothing we injected may stay held
            Sleep(16);   // keep cpu near zero while nothing is on screen
            continue;
        }

        // wait for the composition boundary, then read as fresh as possible and draw right away
        #ifdef BLUE_WHALE_PERF_DIAG
        const long long diag_t0 = diag.now();
        #endif
        WaitForRefresh();
        DwmFlush();
        #ifdef BLUE_WHALE_PERF_DIAG
        const long long diag_t1 = diag.now();
        #endif

        TryAttachGame();
        if (game_.attached())
            game_.ReadFrame(game_frame_, settings_.other.dropped_weapons != kDroppedOff, menu_visible_,
                            aim_on && settings_.aim.auto_sensitivity);
        else
            game_frame_ = GameFrame{};
        #ifdef BLUE_WHALE_PERF_DIAG
        const long long diag_t2 = diag.now();
        #endif

        GameViewport viewport{};
        ComputeGameViewport(viewport);

#ifdef BLUE_WHALE_VIS_TIMING
        if (game_.attached())
            LogVisibleTiming(game_frame_, viewport);
#endif

        if (menu_visible_)
            EnforceFocusBlock();

        // the aim and the trigger read the same fresh frame; both act only while the game
        // owns input and the menu is closed; the current weapon's override wins when set
        if (!menu_visible_ && aim_on)
            ApplyAim(game_frame_, EffectiveAim(settings_, game_frame_.local_weapon_index), game_window_, viewport);
        if (!menu_visible_ && trigger_on)
            ApplyTrigger(game_frame_, EffectiveTrigger(settings_, game_frame_.local_weapon_index), game_window_, viewport);

        // both features guard themselves (ground, physical keys); with the menu open
        // their synthetic keys go back instead
        if (menu_visible_) {
            BhopRelease();
            QuickstopReleaseKeys();
        } else {
            ApplyBhop(game_frame_, settings_.misc.bhop, game_window_);
            ApplyQuickstop(game_frame_, settings_.misc.quickstop, game_window_);
        }

        // final memory read before drawing: the camera is the fastest-changing value
        // in the frame, so ESP and the FOV circle project with it fresh. ReadFrame
        // already refreshed it once at the end of its entity pass for aim/trigger.
        if (game_.attached())
            game_.RefreshView(game_frame_);

        if (!RenderFrame(want_close, viewport)) return 0;
        if (want_close) SetMenuVisible(false);

        #ifdef BLUE_WHALE_PERF_DIAG
        const long long diag_t3 = diag.now();
        diag.add(0, diag_t0, diag_t1);
        diag.add(1, diag_t1, diag_t2);
        diag.add(2, diag_t2, diag_t3);
        ++diag.frames;
        if (GetTickCount64() >= diag.next_ms) {
            diag.next_ms = GetTickCount64() + 1000;
            diag.dump("frame");
        }
        #endif
    }
}

void Overlay::Shutdown() {
    AimReset();
    TriggerReset();   // force release regardless of click deadline
    BhopShutdown();                 // ...or with a synthetic space held
    QuickstopShutdown();            // ...or with a movement key held
    InputShutdown();                // ...and remove the shared keyboard hook
    SaveSettings(settings_, settings_path_);

    if (imgui_ready_) {
        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        imgui_ready_ = false;
    }
    ImGui::DestroyContext();

    game_.Detach();

    ReleaseComposition();
    DestroyRenderTarget();
    if (swapchain_) { swapchain_->Release(); swapchain_ = nullptr; }
    if (context_)   { context_->Release();   context_   = nullptr; }
    if (device_)    { device_->Release();    device_    = nullptr; }
    if (frame_timer_) { CloseHandle(frame_timer_); frame_timer_ = nullptr; }
    if (hwnd_)      { DestroyWindow(hwnd_);  hwnd_      = nullptr; }
    UnregisterClassW(kWindowClass, instance_);
}

LRESULT CALLBACK Overlay::WndProcThunk(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    Overlay* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lp);
        self = static_cast<Overlay*>(create->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->hwnd_ = hwnd;
    } else {
        self = reinterpret_cast<Overlay*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (self) return self->WndProc(hwnd, msg, wp, lp);
    return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT Overlay::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (imgui_ready_ && ImGui_ImplWin32_WndProcHandler(hwnd, msg, wp, lp))
        return 1;

    switch (msg) {
    case WM_NCHITTEST:
        // full click-through while the menu is closed: with a directcomposition window
        // WS_EX_TRANSPARENT alone does not let the clicks reach the window below,
        // but an ht-transparent hit test does — the desktop and the game stay clickable
        if (click_through_ && !menu_visible_)
            return HTTRANSPARENT;
        break;
    case WM_DISPLAYCHANGE:
        OnDisplayChanged();
        return 0;
    case WM_SYSCOMMAND:
        if ((wp & 0xfff0) == SC_KEYMENU)   // swallow the alt menu
            return 0;
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
