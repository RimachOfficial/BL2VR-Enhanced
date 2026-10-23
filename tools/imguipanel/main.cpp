// bl2vr_panel - standalone desktop ImGui panel that edits bl2vr_grip.ini.
// The mod hot-reloads the ini every ~2s, so changes apply live while the game runs.
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include "ui.h"
#include <d3d11.h>
#include <tchar.h>

static ID3D11Device*           g_pd3dDevice = nullptr;
static ID3D11DeviceContext*    g_pd3dDeviceContext = nullptr;
static IDXGISwapChain*         g_pSwapChain = nullptr;
static ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;

static void CreateRenderTarget();

static bool CreateDeviceD3D(HWND hWnd) {
    DXGI_SWAP_CHAIN_DESC sd = {};
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
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;   // flip model: clean live-resize (bitblt/DISCARD stretches the stale frame -> streaks)

    UINT createDeviceFlags = 0;
    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevelArray[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    HRESULT res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
        createDeviceFlags, featureLevelArray, 2, D3D11_SDK_VERSION, &sd,
        &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
    if (res == DXGI_ERROR_UNSUPPORTED)
        res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr,
            createDeviceFlags, featureLevelArray, 2, D3D11_SDK_VERSION, &sd,
            &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
    if (res != S_OK) return false;
    CreateRenderTarget();
    return true;
}
static void CleanupDeviceD3D() {
    if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = nullptr; }
    if (g_pSwapChain) { g_pSwapChain->Release(); g_pSwapChain = nullptr; }
    if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = nullptr; }
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
}
static void CreateRenderTarget() {
    ID3D11Texture2D* pBackBuffer = nullptr;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    if (pBackBuffer) {
        g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
        pBackBuffer->Release();
    }
}
static void CleanupRenderTarget() {
    if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = nullptr; }
}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
static LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam)) return 1;
    switch (msg) {
    case WM_SIZE:
        if (g_pd3dDevice && wParam != SIZE_MINIMIZED) {
            CleanupRenderTarget();
            // flip model: ResizeBuffers flags must match the flags used at creation
            g_pSwapChain->ResizeBuffers(0, (UINT)LOWORD(lParam), (UINT)HIWORD(lParam), DXGI_FORMAT_UNKNOWN, DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH);
            CreateRenderTarget();
        }
        return 0;
    case WM_NCHITTEST: {
        POINT pt = { (short)LOWORD(lParam), (short)HIWORD(lParam) };
        RECT r; GetWindowRect(hWnd, &r);
        const int m = 8;   // resize margin
        bool l = pt.x < r.left + m,  rr = pt.x >= r.right - m;
        bool t = pt.y < r.top + m,   bb = pt.y >= r.bottom - m;
        if (t && l)  return HTTOPLEFT;
        if (t && rr) return HTTOPRIGHT;
        if (bb && l) return HTBOTTOMLEFT;
        if (bb && rr) return HTBOTTOMRIGHT;
        if (l)  return HTLEFT;
        if (rr) return HTRIGHT;
        if (t)  return HTTOP;
        if (bb) return HTBOTTOM;
        // title strip drag (leave the top-right X button clickable); 46px strip at any DPI
        const UINT dpi = GetDpiForWindow(hWnd);
        const int titleH = MulDiv(46, dpi, 96);
        const int closeW = MulDiv(52, dpi, 96);
        if (pt.y >= r.top && pt.y < r.top + titleH && pt.x < r.right - closeW) return HTCAPTION;
        break;
    }
    case WM_GETMINMAXINFO: {
        MINMAXINFO* mmi = (MINMAXINFO*)lParam;
        const UINT dpi0 = GetDpiForWindow(hWnd);
        const UINT dpi = dpi0 ? dpi0 : 96;
        mmi->ptMinTrackSize.x = MulDiv(430, dpi, 96);
        mmi->ptMinTrackSize.y = MulDiv(560, dpi, 96);
        return 0;
    }
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU) return 0;   // keep Alt from stealing focus
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int) {
    // per-monitor v2 DPI awareness before the window exists, so sizes are right from frame 1
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, WndProc, 0, 0, hInstance, nullptr, nullptr, nullptr, nullptr,
                       L"BL2VRGripPanel", nullptr };
    RegisterClassExW(&wc);
    const UINT sysDpi = GetDpiForSystem();
    HWND hwnd = CreateWindowW(wc.lpszClassName, L"BL2VR Grip",
                              WS_POPUP | WS_THICKFRAME, 100, 80,
                              MulDiv(560, sysDpi, 96), MulDiv(760, sysDpi, 96),
                              nullptr, nullptr, wc.hInstance, nullptr);
    if (!CreateDeviceD3D(hwnd)) { CleanupDeviceD3D(); UnregisterClassW(wc.lpszClassName, wc.hInstance); return 1; }
    ShowWindow(hwnd, SW_SHOWDEFAULT);
    UpdateWindow(hwnd);

    g_dpi = GetDpiForWindow(hwnd) / 96.0f;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;   // no imgui.ini on disk
    ApplyTheme(g_dpi);
    LoadFonts(ImGui::GetIO(), g_dpi);
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    Patcher p;
    p.dllSrc = ExeDir() + "openvr_api.dll";
    p.gameWin64 = FindGameFolder();
    p.found = !p.gameWin64.empty();
    p.gameExe = p.found ? p.gameWin64 + "Borderlands2VR.exe" : "";
    p.status = p.found ? "" : "Game not found - click Detect.";

    IniFile ini;
    std::string iniPath = p.found ? p.gameWin64 + "bl2vr_grip.ini" : "bl2vr_grip.ini";
    ini.Load(iniPath);

    bool done = false;
    while (!done) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            if (msg.message == WM_QUIT) done = true;
        }
        if (done) break;

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        // The ImGui window IS the panel: one borderless, full-size window.
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
        ImGui::Begin("BL2VR Grip", nullptr,
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
        const ImVec2 wpos0 = ImGui::GetWindowPos();

        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) done = true;

        // title strip: drag region (native via WM_NCHITTEST) + custom close button
        const float topH = 46.0f * g_dpi;
        ImGui::SetCursorPos(ImVec2(14.0f * g_dpi, 8.0f * g_dpi));
        ImGui::PushFont(g_fonts.title);
        ImGui::TextColored(kText, "BL2VR");
        ImGui::PopFont();
        ImGui::SameLine();
        ImGui::SetCursorPosY(14.0f * g_dpi);
        ImGui::TextColored(kAccent, "Enhanced");
        ImGui::SameLine();
        ImGui::SetCursorPosY(17.0f * g_dpi);
        ImGui::TextColored(kTextDim, "grip + aim smoothing");

        // close button: drawn as a small square that turns red on hover
        {
            const ImVec2 cpos(ImGui::GetWindowWidth() - topH + 4.0f * g_dpi, 8.0f * g_dpi);
            ImGui::SetCursorScreenPos(cpos);
            const float sz = topH - 16.0f * g_dpi;
            ImGui::PushID("##close");
            ImGui::InvisibleButton("##x", ImVec2(sz, sz));
            const bool hov = ImGui::IsItemHovered();
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const ImVec2 p0 = ImGui::GetItemRectMin(), p1 = ImGui::GetItemRectMax();
            if (hov) dl->AddRectFilled(p0, p1, ImGui::GetColorU32(kErr), 6.0f * g_dpi);
            const ImU32 xc = ImGui::GetColorU32(hov ? ImVec4(1, 1, 1, 1) : kTextDim);
            const float cx = (p0.x + p1.x) * 0.5f, cy = (p0.y + p1.y) * 0.5f, r = sz * 0.22f;
            dl->AddLine(ImVec2(cx - r, cy - r), ImVec2(cx + r, cy + r), xc, 1.6f * g_dpi);
            dl->AddLine(ImVec2(cx + r, cy - r), ImVec2(cx - r, cy + r), xc, 1.6f * g_dpi);
            if (ImGui::IsItemClicked()) done = true;
            ImGui::PopID();
        }
        // strip divider under the title (sidebar bg already covers the left part)
        ImGui::GetWindowDrawList()->AddLine(
            ImVec2(170.0f * g_dpi, wpos0.y + topH), ImVec2(wpos0.x + ImGui::GetWindowWidth(), wpos0.y + topH),
            ImGui::GetColorU32(ImVec4(1, 1, 1, 0.06f)));
        ImGui::SetCursorPosY(topH);

        DrawUI(ini, p);
        ImGui::End();
        ImGui::Render();

        const float clear[4] = { 0.086f, 0.090f, 0.102f, 1.0f };   // matches kWindowBgC
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        g_pSwapChain->Present(1, 0);   // vsync
    }

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    CleanupDeviceD3D();
    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
    return 0;
}
