#include "pomodoro/app_paths.h"
#include "pomodoro/logger.h"
#include "pomodoro/database_manager.h"
#include "pomodoro/timer_engine.h"
#include "pomodoro/state_recovery.h"
#include "pomodoro/clock.h"
#include "pomodoro/gui.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <tchar.h>

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"

// ─── DirectX 11 Global State ────────────────────────────────────────────────
static ID3D11Device*           g_pd3dDevice = nullptr;
static ID3D11DeviceContext*    g_pd3dDeviceContext = nullptr;
static IDXGISwapChain*         g_pSwapChain = nullptr;
static ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;
static UINT                    g_ResizeWidth = 0, g_ResizeHeight = 0;

// Forward declarations
bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
void CreateRenderTarget();
void CleanupRenderTarget();
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow) {
    using namespace tempus;

    // ── 1. Resolve data directory ───────────────────────────────────────────
    auto paths_result = AppPaths::initialize();
    if (paths_result.is_err()) {
        return EXIT_FAILURE;
    }
    const AppPaths& paths = paths_result.value();

    // ── 2. Logger ───────────────────────────────────────────────────────────
    RotatingFileLogger logger(paths.log_path());
    logger.info("Main", "=== Tempus GUI v0.1.0 starting ===");
    logger.info("Main", "Data directory: " + paths.data_dir().string());

    // ── 3. Database ─────────────────────────────────────────────────────────
    SqliteDatabaseManager db(paths.db_path().string(), &logger);
    auto db_init = db.initialize();
    if (db_init.is_err()) {
        logger.error("Main", "Database initialization failed: " + db_init.error());
        return EXIT_FAILURE;
    }

    // ── 4. Timer Engine ─────────────────────────────────────────────────────
    SystemClock clock;
    TimerEngine engine(&clock, &db, &logger);

    // ── 5. State Recovery ───────────────────────────────────────────────────
    auto snap_result = StateRecovery::load(paths.state_file_path(), &logger);
    if (snap_result.is_ok() && snap_result.value().has_value()) {
        auto restore = engine.restore(snap_result.value().value());
        if (restore.is_ok()) {
            StateRecovery::clear(paths.state_file_path());
            logger.info("Main", "Timer state restored from previous session");
        } else {
            logger.warn("Main", "State restore failed: " + restore.error());
        }
    }

    // ── 6. Create Win32 Window ──────────────────────────────────────────────
    WNDCLASSEXW wc = {
        sizeof(wc),
        CS_CLASSDC,
        WndProc,
        0L,
        0L,
        hInstance,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        L"TempusWindowClass",
        nullptr
    };
    ::RegisterClassExW(&wc);
    
    HWND hwnd = ::CreateWindowExW(
        0,
        L"TempusWindowClass",
        L"Tempus Time Tracker",
        WS_OVERLAPPEDWINDOW,
        100, 100, 960, 600,
        nullptr,
        nullptr,
        hInstance,
        nullptr
    );

    // Initialize Direct3D
    if (!CreateDeviceD3D(hwnd)) {
        logger.error("Main", "Direct3D 11 device creation failed");
        CleanupDeviceD3D();
        ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return EXIT_FAILURE;
    }

    // Show the window
    ::ShowWindow(hwnd, nCmdShow);
    ::UpdateWindow(hwnd);

    // ── 7. Setup Dear ImGui ─────────────────────────────────────────────────
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    
    GuiManager gui(&engine, &db, &logger, paths.data_dir() / "config.txt");
    gui.initialize_style();

    // Setup Platform/Renderer backends
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    // ── 8. Message Loop ─────────────────────────────────────────────────────
    logger.info("Main", "Entering Win32 render loop");

    constexpr int STATE_SAVE_TICKS = 600;   // Save state every 60 seconds
    int tick_counter = 0;

    bool running = true;
    MSG msg;
    std::time_t last_tick_time = 0;

    while (running) {
        // Poll and handle Win32 messages
        while (::PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
            if (msg.message == WM_QUIT) {
                running = false;
            }
        }
        if (!running) break;

        // Handle resize request
        if (g_ResizeWidth != 0 && g_ResizeHeight != 0) {
            CleanupRenderTarget();
            g_pSwapChain->ResizeBuffers(0, g_ResizeWidth, g_ResizeHeight, DXGI_FORMAT_UNKNOWN, 0);
            g_ResizeWidth = g_ResizeHeight = 0;
            CreateRenderTarget();
        }

        // Tick Timer Engine at ~10Hz (once every 100ms)
        std::time_t now_sec = clock.wall_time();
        if (now_sec != last_tick_time) {
            last_tick_time = now_sec;
            auto tick_result = engine.tick();
            if (tick_result.is_err()) {
                logger.error("Main", "Tick error: " + tick_result.error());
            }

            // Periodic state snapshot for crash recovery (every 60s)
            if (++tick_counter >= STATE_SAVE_TICKS) {
                tick_counter = 0;
                if (engine.state() != TimerState::Idle) {
                    StateRecovery::save(engine.snapshot(), paths.state_file_path(), &logger);
                }
            }
        }

        // Start the Dear ImGui frame
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        // Render main layout
        gui.render(running, hwnd);

        // Rendering
        ImGui::Render();
        const float clear_color_with_alpha[4] = { 0.10f, 0.10f, 0.10f, 1.00f };
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear_color_with_alpha);
        
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        // Present swap chain (VSync enabled)
        g_pSwapChain->Present(1, 0); 
    }

    // ── 9. Shutdown & Cleanup ───────────────────────────────────────────────
    logger.info("Main", "=== Tempus shutting down ===");
    
    // Save active state before exit
    if (engine.state() != TimerState::Idle) {
        StateRecovery::save(engine.snapshot(), paths.state_file_path(), &logger);
    }

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    CleanupDeviceD3D();
    ::DestroyWindow(hwnd);
    ::UnregisterClassW(wc.lpszClassName, wc.hInstance);

    return EXIT_SUCCESS;
}

// ─── DirectX 11 Helper Functions ──────────────────────────────────────────

bool CreateDeviceD3D(HWND hWnd) {
    // Setup swap chain
    DXGI_SWAP_CHAIN_DESC sd;
    ZeroMemory(&sd, sizeof(sd));
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    UINT createDeviceFlags = 0;
    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevelArray[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    
    HRESULT res = D3D11CreateDeviceAndSwapChain(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        createDeviceFlags,
        featureLevelArray,
        2,
        D3D11_SDK_VERSION,
        &sd,
        &g_pSwapChain,
        &g_pd3dDevice,
        &featureLevel,
        &g_pd3dDeviceContext
    );
    
    if (res != S_OK) return false;

    CreateRenderTarget();
    return true;
}

void CleanupDeviceD3D() {
    CleanupRenderTarget();
    if (g_pSwapChain) { g_pSwapChain->Release(); g_pSwapChain = nullptr; }
    if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = nullptr; }
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
}

void CreateRenderTarget() {
    ID3D11Texture2D* pBackBuffer = nullptr;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
    pBackBuffer->Release();
}

void CleanupRenderTarget() {
    if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = nullptr; }
}

// Forward declare message handler from imgui_impl_win32.cpp
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// Win32 Message Handler
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    switch (msg) {
        case WM_SIZE:
            if (g_pd3dDevice != nullptr && wParam != SIZE_MINIMIZED) {
                g_ResizeWidth = (UINT)LOWORD(lParam);
                g_ResizeHeight = (UINT)HIWORD(lParam);
            }
            return 0;
        case WM_SYSCOMMAND:
            if ((wParam & 0xFFF0) == SC_KEYMENU) // Disable ALT application menu
                return 0;
            break;
        case WM_DESTROY:
            ::PostQuitMessage(0);
            return 0;
    }
    return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}
