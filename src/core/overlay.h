// overlay.h - topmost overlay window, d3d11 swapchain, imgui frame pump
#pragma once

#include <windows.h>
#include <d3d11.h>
#include <dcomp.h>
#include <dxgi.h>
#include <dxgi1_2.h>

#include <string>

#include "game.h"
#include "settings.h"
#include "viewport.h"

struct ImFont;

class Overlay {
public:
    bool Initialize(HINSTANCE instance);
    void Shutdown();
    int  Run();

private:
    static LRESULT CALLBACK WndProcThunk(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    LRESULT WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

    bool CreateOverlayWindow(bool composition);
    bool CreateDevice();
    bool CreateCompositionSwapChain();
    bool CreateColorKeySwapChain();
    void ReleaseComposition();
    bool CreateRenderTarget();
    void DestroyRenderTarget();
    ID3D11RenderTargetView* CurrentRenderTarget() const;
    void OnDisplayChanged();

    bool HandleInput();                 // returns true when unload was requested
    bool RefreshTargetConfig();         // re-reads target.txt, returns true when the target changed
    bool UpdateOverlayBounds(bool force);
    bool ComputeGameViewport(GameViewport& viewport) const;
    void SetMenuVisible(bool visible);
    void SyncWindowState();
    void ApplyCaptureAffinity();        // dwm-level exclusion from screen capture
    void SetClickThrough(bool enabled);
    void EnforceFocusBlock();
    void StealFocus();
    void ReturnFocus(HWND target);      // hand input (and the mouse) back to the game
    void UpdateFocusReturn();           // per-frame: finish the hand-back once focus lands
    void TryAttachGame();
    void WaitForRefresh();
    HWND FindGameWindow() const;
    DWORD FindGameProcess() const;
    bool RenderFrame(bool& want_close, const GameViewport& viewport);

    HINSTANCE instance_ = nullptr;
    HWND      hwnd_     = nullptr;

    bool EspActive() const { return ::EspActive(settings_); }

    ID3D11Device*           device_        = nullptr;
    ID3D11DeviceContext*    context_       = nullptr;
    IDXGISwapChain*         swapchain_     = nullptr;
    ID3D11RenderTargetView* render_targets_[2] = {};
    int                     render_target_count_ = 0;

    IDCompositionDevice*    dcomp_device_  = nullptr;
    IDCompositionTarget*    dcomp_target_  = nullptr;
    IDCompositionVisual*    dcomp_visual_  = nullptr;

    Settings   settings_;
    GameReader game_;
    GameFrame  game_frame_;
    ImFont*    esp_font_      = nullptr;
    ImFont*    esp_icon_font_ = nullptr;   // weapon icon glyphs (embedded font)
    float      esp_text_size_ = 14.0f;
    float      esp_icon_size_ = 17.0f;

    bool     menu_visible_     = false;
    bool     hotkey_down_      = false;
    bool     unload_key_down_  = false;
    bool     imgui_ready_      = false;
    bool     window_shown_     = false;
    bool     click_through_    = false;
    bool     per_pixel_alpha_  = false;
    bool     affinity_applied_ = false;   // last window-capture-affinity state
    bool     affinity_value_   = false;
    unsigned long long next_affinity_ms_ = 0;
    unsigned long long next_attach_try_ms_ = 0;
    HWND     game_window_      = nullptr;   // target main window, re-resolved when stale
    HWND     restore_target_   = nullptr;   // foreground captured when the menu opened
    HWND     focus_return_        = nullptr;   // window that gets input back after the menu closes
    int      focus_return_wait_   = 0;         // frames left to keep retrying the hand-back
    int      focus_return_nudges_ = 0;         // net-zero mouse nudges left once focus is back
    int      focus_return_gap_    = 0;         // frames to wait before the next nudge
    std::wstring exe_dir_;                     // exe folder (config lives there)
    std::wstring settings_path_;               // settings.ini next to the exe
    unsigned long long next_window_find_ms_ = 0;    // throttles game-window lookups when idle
    unsigned long long next_target_reload_ms_ = 0;  // target.txt is re-read on this cadence
    HANDLE   frame_timer_      = nullptr;       // high-resolution waitable timer for frame pacing
    long long next_frame_qpc_  = 0;             // qpc time the next frame is due
    std::wstring game_exe_name_ = L"cs2.exe";   // overridable via target.txt next to the exe
    int      screen_left_      = 0;             // overlay monitor bounds in screen coords
    int      screen_top_       = 0;
    int      screen_width_     = 0;
    int      screen_height_    = 0;
};
