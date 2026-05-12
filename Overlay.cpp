#include "../include/Overlay.hpp"
#include "gui.hpp"
#include "../include/Fonts/Verdana_Regular.h"
#include "../catalyst/catalyst/project/resources/fonts/weapons.hpp"
#include <iostream>
#include <thread>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <vector>
#include <wincodec.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "windowscodecs.lib")

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

LRESULT WINAPI Overlay::WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    switch (msg) {
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

bool Overlay::Setup(const std::string& targetClassName, const std::string& targetWindowName) {
    // Find target window
    targetWindow = FindWindowA(targetClassName.c_str(), targetWindowName.c_str());
    if (!targetWindow) {
        std::cerr << "Could not find target window!" << std::endl;
        return false;
    }

    // Wait for a valid window size (not minimized)
    RECT clientRect;
    while (true) {
        GetClientRect(targetWindow, &clientRect);
        Width = clientRect.right - clientRect.left;
        Height = clientRect.bottom - clientRect.top;
        if (Width > 100 && Height > 100) break;
        std::cout << "[~] Waiting for valid CS2 window size..." << std::endl;
        Sleep(500);
    }

    POINT screenPos = { 0, 0 };
    ClientToScreen(targetWindow, &screenPos);

    // Register overlay class
    WNDCLASSEX wc = { sizeof(WNDCLASSEX), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(NULL), NULL, NULL, NULL, NULL, "ZitroxOverlay", NULL };
    RegisterClassEx(&wc);

    // Create overlay window
    overlayWindow = CreateWindowExA(
        WS_EX_TOPMOST | WS_EX_TRANSPARENT | WS_EX_LAYERED | WS_EX_TOOLWINDOW,
        "ZitroxOverlay", "Zitrox ESP",
        WS_POPUP,
        screenPos.x, screenPos.y, Width, Height,
        NULL, NULL, wc.hInstance, NULL
    );

    // Transparency
    SetLayeredWindowAttributes(overlayWindow, RGB(0, 0, 0), 255, LWA_ALPHA);
    MARGINS margins = { -1 };
    DwmExtendFrameIntoClientArea(overlayWindow, &margins);

    if (!CreateDeviceD3D()) {
        CleanupDeviceD3D();
        UnregisterClass(wc.lpszClassName, wc.hInstance);
        return false;
    }

    ShowWindow(overlayWindow, SW_SHOWDEFAULT);
    UpdateWindow(overlayWindow);

    // ImGui Initialization
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    
    char path[MAX_PATH];
    GetCurrentDirectoryA(MAX_PATH, path);
    std::string currentDir = std::string(path);
    char modulePath[MAX_PATH];
    GetModuleFileNameA(NULL, modulePath, MAX_PATH);
    std::string exeDir = std::filesystem::path(modulePath).parent_path().string();
    
    std::vector<std::string> robotoPaths = {
        "C:\\ZitFem\\Fonts\\Roboto-Medium.ttf",
        currentDir + "\\catalyst\\skeet.cc-master\\misc\\fonts\\Roboto-Medium.ttf",
        currentDir + "\\..\\femboy.meow\\imgui\\misc\\fonts\\Roboto-Medium.ttf",
        "Zitrox\\catalyst\\skeet.cc-master\\misc\\fonts\\Roboto-Medium.ttf"
    };

    for (const auto& p : robotoPaths) {
        MenuFont = io.Fonts->AddFontFromFileTTF(p.c_str(), 15.0f);
        if (MenuFont) break;
    }

    MainFont = MenuFont ? MenuFont : io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(Verdana_Regular), sizeof(Verdana_Regular), 16.0f);
    ESPFont = io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(Verdana_Regular), sizeof(Verdana_Regular), 12.0f);
    
    // Weapon icons font
    IconFont = io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(resources::fonts::weapons), sizeof(resources::fonts::weapons), 16.0f); 

    // Try to load Minecraft font from file (multiple paths)
    std::vector<std::string> paths = {
        "C:\\ZitFem\\Fonts\\minecraftfont.ttf",
        currentDir + "\\minecraftfont.ttf",
        currentDir + "\\..\\..\\minecraftfont.ttf",
        "Zitrox\\minecraftfont.ttf",
        "minecraftfont.ttf"
    };

    for (const auto& p : paths) {
        MinecraftFont = io.Fonts->AddFontFromFileTTF(p.c_str(), 14.0f);
        MinecraftFontSmall = io.Fonts->AddFontFromFileTTF(p.c_str(), 10.0f);
        if (MinecraftFont) break;
    }
    
    if (!MainFont) MainFont = io.Fonts->AddFontDefault();
    if (!MenuFont) MenuFont = MainFont;
    io.FontDefault = MenuFont;
    if (!ESPFont) ESPFont = MainFont;
    if (!IconFont) IconFont = MainFont;
    if (!MinecraftFont) MinecraftFont = ESPFont;
    if (!MinecraftFontSmall) MinecraftFontSmall = MinecraftFont;

    CombatFemIcon = LoadTextureFromFile(currentDir + "\\icon\\combat_fem.png");
    CombatZitIcon = LoadTextureFromFile(currentDir + "\\icon\\combat_zit.png");
    auto loadTabIcon = [&](const std::string& fileName) -> ID3D11ShaderResourceView* {
        std::vector<std::string> iconPaths = {
            currentDir + "\\icon\\" + fileName,
            exeDir + "\\icon\\" + fileName,
            exeDir + "\\..\\icon\\" + fileName,
            exeDir + "\\..\\..\\icon\\" + fileName,
            currentDir + "\\..\\icon\\" + fileName,
            currentDir + "\\..\\..\\icon\\" + fileName,
            currentDir + "\\Zitrox\\icon\\" + fileName,
            "Z:\\Vaznoe\\cs2-internal\\Zitrox\\icon\\" + fileName,
            "C:\\ZitFem\\icon\\" + fileName,
            "Zitrox\\icon\\" + fileName
        };
        for (const auto& iconPath : iconPaths) {
            if (!std::filesystem::exists(iconPath)) continue;
            ID3D11ShaderResourceView* texture = LoadTextureFromFile(iconPath, nullptr, nullptr, true);
            if (texture) return texture;
        }
        return nullptr;
    };

    TabCombatIcon = loadTabIcon("tab_combat.png");
    TabVisualsIcon = loadTabIcon("tab_visuals.png");
    TabMiscIcon = loadTabIcon("tab_misc.png");
    TabGrenadesIcon = loadTabIcon("tab_grenades.png");
    TabConfigsIcon = loadTabIcon("tab_configs.png");
    TabLuaIcon = loadTabIcon("tab_lua.png");
    ESPPreviewTexture = LoadTextureFromFile(currentDir + "\\ct_t.png", &ESPPreviewWidth, &ESPPreviewHeight);

    ImGui::StyleColorsDark();

    ImGui_ImplWin32_Init(overlayWindow);
    ImGui_ImplDX11_Init(pd3dDevice, pd3dDeviceContext);

    gui::SetupStyles();

    return true;
}

void Overlay::Render() {
    MSG msg;
    while (PeekMessage(&msg, NULL, 0U, 0U, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
        if (msg.message == WM_QUIT) return;
    }

    // Keep window size and position updated
    RECT clientRect;
    GetClientRect(targetWindow, &clientRect);
    int newWidth = clientRect.right - clientRect.left;
    int newHeight = clientRect.bottom - clientRect.top;

    POINT screenPos = { 0, 0 };
    ClientToScreen(targetWindow, &screenPos);

    static int lastX = -1, lastY = -1;
    if (newWidth != Width || newHeight != Height || screenPos.x != lastX || screenPos.y != lastY) {
        Width = newWidth;
        Height = newHeight;
        lastX = screenPos.x;
        lastY = screenPos.y;
        MoveWindow(overlayWindow, screenPos.x, screenPos.y, Width, Height, true);
    }

    // Only render if CS2 is in foreground
    HWND foregroundWindow = GetForegroundWindow();
    if (foregroundWindow != targetWindow && foregroundWindow != overlayWindow) {
        // Clear screen and present empty buffer if not in foreground
        const float clear_color_with_alpha[4] = { 0.f, 0.f, 0.f, 0.f };
        pd3dDeviceContext->OMSetRenderTargets(1, &pMainRenderTargetView, NULL);
        pd3dDeviceContext->ClearRenderTargetView(pMainRenderTargetView, clear_color_with_alpha);
        pSwapChain->Present(0, 0);
        std::this_thread::sleep_for(std::chrono::milliseconds(100)); // Sleep longer when out of focus
        return;
    }

    // Update ImGui display size
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2((float)Width, (float)Height);

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    // Call custom ESP logic
    if (RenderCallback) {
        RenderCallback();
    }

    ImGui::EndFrame();
    ImGui::Render();

    const float clear_color_with_alpha[4] = { 0.f, 0.f, 0.f, 0.f };
    pd3dDeviceContext->OMSetRenderTargets(1, &pMainRenderTargetView, NULL);
    pd3dDeviceContext->ClearRenderTargetView(pMainRenderTargetView, clear_color_with_alpha);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    pSwapChain->Present(0, 0);
}

void Overlay::ToggleTransparency(bool transparent) {
    LONG_PTR style = GetWindowLongPtr(overlayWindow, GWL_EXSTYLE);
    if (transparent) {
        style |= WS_EX_TRANSPARENT;
    } else {
        style &= ~WS_EX_TRANSPARENT;
        SetForegroundWindow(overlayWindow);
    }
    SetWindowLongPtr(overlayWindow, GWL_EXSTYLE, style);
    SetWindowPos(overlayWindow, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_FRAMECHANGED);
}

void Overlay::Cleanup() {
    if (CombatFemIcon) { CombatFemIcon->Release(); CombatFemIcon = nullptr; }
    if (CombatZitIcon) { CombatZitIcon->Release(); CombatZitIcon = nullptr; }
    if (TabCombatIcon) { TabCombatIcon->Release(); TabCombatIcon = nullptr; }
    if (TabVisualsIcon) { TabVisualsIcon->Release(); TabVisualsIcon = nullptr; }
    if (TabMiscIcon) { TabMiscIcon->Release(); TabMiscIcon = nullptr; }
    if (TabGrenadesIcon) { TabGrenadesIcon->Release(); TabGrenadesIcon = nullptr; }
    if (TabConfigsIcon) { TabConfigsIcon->Release(); TabConfigsIcon = nullptr; }
    if (TabLuaIcon) { TabLuaIcon->Release(); TabLuaIcon = nullptr; }
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    CleanupDeviceD3D();
    DestroyWindow(overlayWindow);
    UnregisterClassA("ZitroxOverlay", GetModuleHandle(NULL));
}

bool Overlay::CreateDeviceD3D() {
    DXGI_SWAP_CHAIN_DESC sd;
    ZeroMemory(&sd, sizeof(sd));
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 0;
    sd.BufferDesc.RefreshRate.Denominator = 0;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = overlayWindow;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    UINT createDeviceFlags = 0;
    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevelArray[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0, };

    if (D3D11CreateDeviceAndSwapChain(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, createDeviceFlags, featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &pSwapChain, &pd3dDevice, &featureLevel, &pd3dDeviceContext) != S_OK)
        return false;

    CreateRenderTarget();
    return true;
}

void Overlay::CreateRenderTarget() {
    ID3D11Texture2D* pBackBuffer;
    pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    pd3dDevice->CreateRenderTargetView(pBackBuffer, NULL, &pMainRenderTargetView);
    pBackBuffer->Release();
}

void Overlay::CleanupRenderTarget() {
    if (pMainRenderTargetView) { pMainRenderTargetView->Release(); pMainRenderTargetView = NULL; }
}

ID3D11ShaderResourceView* Overlay::LoadTextureFromFile(const std::string& path, int* outWidth, int* outHeight, bool forceWhitePixels) {
    IWICImagingFactory* factory = nullptr;
    IWICBitmapDecoder* decoder = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICFormatConverter* converter = nullptr;
    ID3D11Texture2D* texture = nullptr;
    ID3D11ShaderResourceView* view = nullptr;

    if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) {
        CoInitialize(nullptr);
    }

    std::wstring widePath(path.begin(), path.end());
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    if (SUCCEEDED(hr)) hr = factory->CreateDecoderFromFilename(widePath.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &decoder);
    if (SUCCEEDED(hr)) hr = decoder->GetFrame(0, &frame);
    if (SUCCEEDED(hr)) hr = factory->CreateFormatConverter(&converter);
    if (SUCCEEDED(hr)) hr = converter->Initialize(frame, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);

    UINT width = 0, height = 0;
    if (SUCCEEDED(hr)) hr = converter->GetSize(&width, &height);
    if (outWidth) *outWidth = width;
    if (outHeight) *outHeight = height;

    std::vector<unsigned char> pixels(width * height * 4);
    if (SUCCEEDED(hr)) hr = converter->CopyPixels(nullptr, width * 4, (UINT)pixels.size(), pixels.data());
    if (SUCCEEDED(hr) && forceWhitePixels) {
        for (size_t i = 0; i + 3 < pixels.size(); i += 4) {
            if (pixels[i + 3] == 0) continue;
            pixels[i] = 255;
            pixels[i + 1] = 255;
            pixels[i + 2] = 255;
        }
    }

    if (SUCCEEDED(hr) && pd3dDevice && width > 0 && height > 0) {
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = width;
        desc.Height = height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

        D3D11_SUBRESOURCE_DATA data{};
        data.pSysMem = pixels.data();
        data.SysMemPitch = width * 4;

        hr = pd3dDevice->CreateTexture2D(&desc, &data, &texture);
        if (SUCCEEDED(hr)) hr = pd3dDevice->CreateShaderResourceView(texture, nullptr, &view);
    }

    if (texture) texture->Release();
    if (converter) converter->Release();
    if (frame) frame->Release();
    if (decoder) decoder->Release();
    if (factory) factory->Release();
    return view;
}

void Overlay::CleanupDeviceD3D() {
    CleanupRenderTarget();
    if (pSwapChain) { pSwapChain->Release(); pSwapChain = NULL; }
    if (pd3dDeviceContext) { pd3dDeviceContext->Release(); pd3dDeviceContext = NULL; }
    if (pd3dDevice) { pd3dDevice->Release(); pd3dDevice = NULL; }
}
