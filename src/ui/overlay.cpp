#include "ui/overlay.h"

#include "ui/skinchanger_icon.h"
#include "ui/skinchanger_tab_icon.h"
#include "game/hook.h"
#include "features/changer/Changer.h"
#include "features/changer/catalog/catalog.h"
#include "features/changer/loadout/loadout.h"

#include <d3d11.h>
#include <dxgi.h>
#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>
#include <wincodec.h>
#include <Windows.h>

#pragma comment(lib, "windowscodecs.lib")

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace oc::ui {
namespace {

std::atomic<bool> g_ready{false};
std::atomic<bool> g_open{true};
HWND g_hwnd{};
WNDPROC g_original_wndproc{};
ID3D11Device *g_device{};
ID3D11DeviceContext *g_context{};
ID3D11RenderTargetView *g_rtv{};
IDXGISwapChain *g_swapchain{};
ID3D11ShaderResourceView *g_icon_srv{};
int g_icon_w{}, g_icon_h{};
ID3D11ShaderResourceView *g_tab_icon_srv{};
int g_tab_icon_w{}, g_tab_icon_h{};
std::mutex g_frame_mutex;

LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    if (g_ready.load() && ImGui_ImplWin32_WndProcHandler(hwnd, msg, wparam, lparam))
        return true;
    if (msg == WM_KEYDOWN && wparam == VK_INSERT)
        g_open.store(!g_open.load());
    return CallWindowProcW(g_original_wndproc, hwnd, msg, wparam, lparam);
}

void createRenderTarget(IDXGISwapChain *swapchain) {
    ID3D11Texture2D *backbuffer = nullptr;
    if (SUCCEEDED(swapchain->GetBuffer(0, IID_PPV_ARGS(&backbuffer))) && backbuffer) {
        g_device->CreateRenderTargetView(backbuffer, nullptr, &g_rtv);
        backbuffer->Release();
    }
}

void cleanupRenderTarget() {
    if (g_rtv) { g_rtv->Release(); g_rtv = nullptr; }
}

void loadTextureFromMemory(const unsigned char *data, size_t size, ID3D11ShaderResourceView **srv, int *out_w, int *out_h) {
    if (*srv || !g_device) return;
    const HRESULT cohr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    IWICImagingFactory *factory = nullptr;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))) || !factory) {
        if (SUCCEEDED(cohr)) CoUninitialize();
        return;
    }
    IWICStream *stream = nullptr;
    factory->CreateStream(&stream);
    if (!stream) { factory->Release(); if (SUCCEEDED(cohr)) CoUninitialize(); return; }
    stream->InitializeFromMemory(const_cast<BYTE *>(data), static_cast<DWORD>(size));
    IWICBitmapDecoder *decoder = nullptr;
    factory->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnLoad, &decoder);
    IWICBitmapFrameDecode *frame = nullptr;
    if (decoder) decoder->GetFrame(0, &frame);
    IWICFormatConverter *conv = nullptr;
    if (frame) {
        factory->CreateFormatConverter(&conv);
        if (conv) conv->Initialize(frame, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom);
    }
    UINT w = 0, h = 0;
    if (conv) { conv->GetSize(&w, &h); }
    if (w && h) {
        std::vector<unsigned char> pixels(w * h * 4);
        if (SUCCEEDED(conv->CopyPixels(nullptr, w * 4, static_cast<UINT>(pixels.size()), pixels.data()))) {
            D3D11_TEXTURE2D_DESC td{};
            td.Width = w; td.Height = h; td.MipLevels = 1; td.ArraySize = 1;
            td.Format = DXGI_FORMAT_R8G8B8A8_UNORM; td.SampleDesc.Count = 1;
            td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            D3D11_SUBRESOURCE_DATA sd{pixels.data(), w * 4, 0};
            ID3D11Texture2D *tex = nullptr;
            if (SUCCEEDED(g_device->CreateTexture2D(&td, &sd, &tex)) && tex) {
                g_device->CreateShaderResourceView(tex, nullptr, srv);
                tex->Release();
                if (out_w) *out_w = static_cast<int>(w);
                if (out_h) *out_h = static_cast<int>(h);
            }
        }
    }
    if (conv) conv->Release();
    if (frame) frame->Release();
    if (decoder) decoder->Release();
    if (stream) stream->Release();
    factory->Release();
    if (SUCCEEDED(cohr)) CoUninitialize();
}

void loadIconTexture() {
    loadTextureFromMemory(skinchanger_icon_png, skinchanger_icon_png_size, &g_icon_srv, &g_icon_w, &g_icon_h);
    loadTextureFromMemory(skinchanger_tab_icon_png, skinchanger_tab_icon_png_size, &g_tab_icon_srv, &g_tab_icon_w, &g_tab_icon_h);
}

void initImGui(IDXGISwapChain *swapchain) {
    if (g_ready.load())
        return;
    if (FAILED(swapchain->GetDevice(IID_PPV_ARGS(&g_device))))
        return;
    g_device->GetImmediateContext(&g_context);

    DXGI_SWAP_CHAIN_DESC desc{};
    swapchain->GetDesc(&desc);
    g_hwnd = desc.OutputWindow;
    g_swapchain = swapchain;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();
    ImGuiStyle &s = ImGui::GetStyle();
    s.WindowRounding = 10.0f; s.ChildRounding = 8.0f; s.FrameRounding = 6.0f;
    s.PopupRounding = 8.0f; s.ScrollbarRounding = 6.0f; s.GrabRounding = 6.0f;
    s.WindowBorderSize = 1.0f; s.FrameBorderSize = 0.0f; s.ItemSpacing = ImVec2(10, 8);
    auto accent = ImVec4(139.f/255.f, 92.f/255.f, 246.f/255.f, 1.0f);
    s.Colors[ImGuiCol_WindowBg] = ImVec4(18.f/255.f,18.f/255.f,20.f/255.f,0.97f);
    s.Colors[ImGuiCol_ChildBg] = ImVec4(24.f/255.f,24.f/255.f,27.f/255.f,0.55f);
    s.Colors[ImGuiCol_Border] = ImVec4(1,1,1,0.06f);
    s.Colors[ImGuiCol_Text] = ImVec4(235.f/255.f,235.f/255.f,238.f/255.f,1);
    s.Colors[ImGuiCol_TextDisabled] = ImVec4(150.f/255.f,150.f/255.f,158.f/255.f,1);
    s.Colors[ImGuiCol_FrameBg] = ImVec4(30.f/255.f,30.f/255.f,34.f/255.f,0.90f);
    s.Colors[ImGuiCol_FrameBgHovered] = ImVec4(45.f/255.f,45.f/255.f,52.f/255.f,0.95f);
    s.Colors[ImGuiCol_FrameBgActive] = ImVec4(55.f/255.f,55.f/255.f,62.f/255.f,1.0f);
    s.Colors[ImGuiCol_Button] = ImVec4(38.f/255.f,38.f/255.f,46.f/255.f,1);
    s.Colors[ImGuiCol_ButtonHovered] = ImVec4(55.f/255.f,55.f/255.f,66.f/255.f,1);
    s.Colors[ImGuiCol_ButtonActive] = accent;
    s.Colors[ImGuiCol_Header] = ImVec4(45.f/255.f,45.f/255.f,54.f/255.f,1);
    s.Colors[ImGuiCol_HeaderHovered] = ImVec4(60.f/255.f,60.f/255.f,72.f/255.f,1);
    s.Colors[ImGuiCol_HeaderActive] = accent;
    s.Colors[ImGuiCol_CheckMark] = accent;
    s.Colors[ImGuiCol_SliderGrab] = accent;
    s.Colors[ImGuiCol_SliderGrabActive] = ImVec4(160.f/255.f,120.f/255.f,255.f/255.f,1);
    s.Colors[ImGuiCol_Separator] = ImVec4(1,1,1,0.06f);
    s.Colors[ImGuiCol_ScrollbarBg] = ImVec4(0,0,0,0);
    s.Colors[ImGuiCol_ScrollbarGrab] = ImVec4(1,1,1,0.10f);
    s.Colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(1,1,1,0.18f);
    s.Colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(1,1,1,0.25f);

    ImGui_ImplWin32_Init(g_hwnd);
    ImGui_ImplDX11_Init(g_device, g_context);
    createRenderTarget(swapchain);

    g_original_wndproc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(g_hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(wndProc)));
    loadIconTexture();
    g_ready.store(true);
}

using ItemKind = oc::changer::ItemKind;
struct UiState {
    int kind = 0;
    int selected_item = -1;
    int selected_paint = -1;
    int selected_sticker = -1;
    int selected_patch = -1;
    int selected_keychain = -1;
    char name_buf[64]{};
    float wear = 0.f;
    int seed = 0;
    bool stattrak = false;
    int stattrak_count = 0;
};
UiState g_ui;

std::vector<oc::changer::CatalogItem> itemsByKind(ItemKind kind) {
    std::vector<oc::changer::CatalogItem> v;
    const auto &cat = oc::changer::Changer::get().catalog();
    for (const auto &it : cat.Items) if (it.Kind == kind) v.push_back(it);
    std::sort(v.begin(), v.end(), [](auto &a, auto &b){ return a.Name < b.Name; });
    return v;
}

void renderSkinchanger() {
    static const char *kinds[] = {"Weapons", "Knives", "Gloves", "Agents", "Music", "Medals"};
    ImGui::Combo("Category", &g_ui.kind, kinds, 6);
    const auto kind = static_cast<ItemKind>(g_ui.kind);
    const auto &items = itemsByKind(kind);
    const auto &cat = oc::changer::Changer::get().catalog();

    ImGui::BeginChild("items", ImVec2(260, 360), true);
    for (int i = 0; i < (int)items.size(); ++i) {
        const auto &it = items[i];
        if (ImGui::Selectable(it.Name.empty() ? std::to_string(it.Definition).c_str() : it.Name.c_str(), g_ui.selected_item == i)) {
            g_ui.selected_item = i;
            g_ui.selected_paint = -1;
        }
    }
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("details", ImVec2(0, 360), true);
    if (g_ui.selected_item >= 0 && g_ui.selected_item < (int)items.size()) {
        const auto &it = items[g_ui.selected_item];
        ImGui::Text("%s", it.Name.c_str());
        ImGui::TextDisabled("Def: %u  Slot: %u  Paintable: %s  Stickers: %u  Charms: %u  Patches: %u", it.Definition, it.Slot, it.Paintable ? "yes" : "no", it.StickerSlots, it.CharmSlots, it.PatchSlots);
        if (it.Paintable) {
            if (ImGui::BeginCombo("Paint", g_ui.selected_paint >= 0 && g_ui.selected_paint < (int)cat.Paints.size() ? cat.Paints[g_ui.selected_paint].Name.c_str() : "None")) {
                if (ImGui::Selectable("None", g_ui.selected_paint < 0)) g_ui.selected_paint = -1;
                for (int i = 0; i < (int)cat.Paints.size(); ++i) {
                    const auto &p = cat.Paints[i];
                    if (ImGui::Selectable(p.Name.c_str(), g_ui.selected_paint == i)) g_ui.selected_paint = i;
                }
                ImGui::EndCombo();
            }
            ImGui::SliderFloat("Wear", &g_ui.wear, 0.f, 1.f);
            ImGui::InputInt("Seed", &g_ui.seed);
        }
        if (it.StatTrak) {
            ImGui::Checkbox("StatTrak", &g_ui.stattrak);
            if (g_ui.stattrak) ImGui::InputInt("StatTrak Count", &g_ui.stattrak_count);
        }
        ImGui::InputText("Name Tag", g_ui.name_buf, sizeof(g_ui.name_buf));

        if (it.StickerSlots > 0 && ImGui::BeginCombo("Sticker Kit", g_ui.selected_sticker >= 0 && g_ui.selected_sticker < (int)cat.Stickers.size() ? cat.Stickers[g_ui.selected_sticker].Name.c_str() : "None")) {
            for (int i = 0; i < (int)cat.Stickers.size(); ++i)
                if (ImGui::Selectable(cat.Stickers[i].Name.c_str(), g_ui.selected_sticker == i)) g_ui.selected_sticker = i;
            ImGui::EndCombo();
        }
        if (it.PatchSlots > 0 && ImGui::BeginCombo("Patch Kit", g_ui.selected_patch >= 0 && g_ui.selected_patch < (int)cat.Patches.size() ? cat.Patches[g_ui.selected_patch].Name.c_str() : "None")) {
            for (int i = 0; i < (int)cat.Patches.size(); ++i)
                if (ImGui::Selectable(cat.Patches[i].Name.c_str(), g_ui.selected_patch == i)) g_ui.selected_patch = i;
            ImGui::EndCombo();
        }
        if (it.CharmSlots > 0 && ImGui::BeginCombo("Charm/Keychain", g_ui.selected_keychain >= 0 && g_ui.selected_keychain < (int)cat.Keychains.size() ? cat.Keychains[g_ui.selected_keychain].Name.c_str() : "None")) {
            for (int i = 0; i < (int)cat.Keychains.size(); ++i)
                if (ImGui::Selectable(cat.Keychains[i].Name.c_str(), g_ui.selected_keychain == i)) g_ui.selected_keychain = i;
            ImGui::EndCombo();
        }

        if (ImGui::Button("Equip")) {
            oc::changer::ItemRecord rec{};
            rec.Team = cat.Ids.AnyTeam;
            rec.Slot = it.Slot;
            rec.Definition = it.Definition;
            rec.Paint = (g_ui.selected_paint >= 0 && g_ui.selected_paint < (int)cat.Paints.size()) ? cat.Paints[g_ui.selected_paint].Id : 0;
            rec.Wear = g_ui.wear;
            rec.Seed = static_cast<std::uint32_t>((std::max)(0, g_ui.seed));
            rec.StatTrak = g_ui.stattrak;
            rec.StatTrakCount = static_cast<std::uint32_t>((std::max)(0, g_ui.stattrak_count));
            rec.Name = g_ui.name_buf;
            if (g_ui.selected_sticker >= 0 && g_ui.selected_sticker < (int)cat.Stickers.size() && it.StickerSlots > 0)
                rec.Stickers.push_back({0, cat.Stickers[g_ui.selected_sticker].Id, 0.f, 1.f, 0.f, 0.f, 0.f, -1});
            if (g_ui.selected_patch >= 0 && g_ui.selected_patch < (int)cat.Patches.size() && it.PatchSlots > 0)
                rec.Patches.push_back({0, cat.Patches[g_ui.selected_patch].Id});
            if (g_ui.selected_keychain >= 0 && g_ui.selected_keychain < (int)cat.Keychains.size() && it.CharmSlots > 0)
                rec.Charm = oc::changer::Charm{cat.Keychains[g_ui.selected_keychain].Id, 0, 0, 0.f, 0.f, 0.f};
            (void)oc::changer::Changer::get().equip(std::move(rec));
        }
        ImGui::SameLine();
        if (ImGui::Button("Unequip")) {
            oc::changer::ItemRecord rec{};
            rec.Team = cat.Ids.AnyTeam; rec.Slot = it.Slot; rec.Definition = it.Definition;
            oc::changer::Changer::get().unequip(rec);
        }
    } else {
        ImGui::TextDisabled("Select an item.");
    }
    ImGui::EndChild();
}

bool navButton(const char *id, const char *label, ID3D11ShaderResourceView *icon, bool active) {
    ImGui::PushID(id);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    const ImVec2 size(width, 38.0f);
    bool pressed = ImGui::InvisibleButton("##nav", size);
    bool hovered = ImGui::IsItemHovered();
    ImDrawList *dl = ImGui::GetWindowDrawList();
    ImU32 bg = active ? IM_COL32(139,92,246,40) : hovered ? IM_COL32(255,255,255,10) : IM_COL32(0,0,0,0);
    dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), bg, 6.0f);
    if (active) dl->AddRectFilled(pos, ImVec2(pos.x + 3.0f, pos.y + size.y), IM_COL32(139,92,246,255), 2.0f);
    float x = pos.x + 12.0f;
    if (icon) {
        dl->AddImage(reinterpret_cast<ImTextureID>(icon), ImVec2(x, pos.y + 9.0f), ImVec2(x + 20.0f, pos.y + 29.0f));
        x += 28.0f;
    } else {
        x += 4.0f;
    }
    ImU32 text = active ? IM_COL32(235,235,238,255) : IM_COL32(150,150,158,255);
    ImVec2 ts = ImGui::CalcTextSize(label);
    dl->AddText(ImVec2(x, pos.y + (size.y - ts.y) * 0.5f), text, label);
    ImGui::PopID();
    return pressed;
}

void renderMenu() {
    if (!g_open.load())
        return;
    static int g_tab = 0;
    ImGui::SetNextWindowSize(ImVec2(920, 600), ImGuiCond_FirstUseEver);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(18.f/255.f,18.f/255.f,20.f/255.f,0.97f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1,1,1,0.06f));
    if (ImGui::Begin("Zoowie", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar)) {
        const ImVec2 wp = ImGui::GetWindowPos();
        const ImVec2 ws = ImGui::GetWindowSize();
        ImDrawList *dl = ImGui::GetWindowDrawList();
        ImGui::SetCursorPos(ImVec2(0, 0));
        ImGui::InvisibleButton("##drag", ImVec2(ws.x, 60.0f));
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            ImVec2 d = ImGui::GetIO().MouseDelta;
            ImGui::SetWindowPos(ImVec2(wp.x + d.x, wp.y + d.y));
        }

        const float sidebar_w = 190.0f;
        dl->AddRectFilled(wp, ImVec2(wp.x + sidebar_w, wp.y + ws.y), IM_COL32(24,24,27,240), 10.0f, ImDrawFlags_RoundCornersLeft);
        dl->AddLine(ImVec2(wp.x + sidebar_w, wp.y + 1), ImVec2(wp.x + sidebar_w, wp.y + ws.y - 1), IM_COL32(255,255,255,15));

        ImGui::SetCursorPos(ImVec2(16, 18));
        if (g_icon_srv) { ImGui::Image(reinterpret_cast<ImTextureID>(g_icon_srv), ImVec2(28, 28)); ImGui::SameLine(0, 10); }
        ImGui::SetCursorPosY(22);
        ImGui::TextUnformatted("Zoowie");

        ImGui::SetCursorPos(ImVec2(10, 70));
        ImGui::BeginChild("##sidebar", ImVec2(sidebar_w - 20, ws.y - 90), false);
        if (navButton("skin", "Skinchanger", g_tab_icon_srv, g_tab == 0)) g_tab = 0;
        ImGui::Dummy(ImVec2(0, 4));
        if (navButton("set", "Settings", nullptr, g_tab == 1)) g_tab = 1;
        ImGui::EndChild();

        // Content
        ImGui::SetCursorPos(ImVec2(sidebar_w + 20, 20));
        ImGui::BeginChild("##content", ImVec2(ws.x - sidebar_w - 40, ws.y - 40), false);
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.0f);
        if (g_tab == 0) renderSkinchanger();
        if (g_tab == 1) {
            ImGui::TextUnformatted("Settings");
            ImGui::Separator();
            ImGui::TextUnformatted("Insert - toggle menu");
            ImGui::TextDisabled("Base: LarpChanger/OpenChanger (MIT). Offsets/schemas: a2x/cs2-dumper (pinned).");
        }
        ImGui::PopStyleVar();
        ImGui::EndChild();
    }
    ImGui::End();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);
}

using PresentFn = HRESULT(__stdcall *)(IDXGISwapChain *, UINT, UINT);
PresentFn g_original_present{};
using ResizeBuffersFn = HRESULT(__stdcall *)(IDXGISwapChain *, UINT, UINT, UINT, DXGI_FORMAT, UINT);
ResizeBuffersFn g_original_resize{};

HRESULT __stdcall presentHook(IDXGISwapChain *swapchain, UINT sync, UINT flags) {
    std::scoped_lock lock(g_frame_mutex);
    if (!g_ready.load())
        initImGui(swapchain);
    if (g_ready.load()) {
        if (!g_rtv) createRenderTarget(swapchain);
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        renderMenu();
        ImGui::Render();
        g_context->OMSetRenderTargets(1, &g_rtv, nullptr);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    }
    return g_original_present(swapchain, sync, flags);
}

HRESULT __stdcall resizeHook(IDXGISwapChain *swapchain, UINT count, UINT w, UINT h, DXGI_FORMAT fmt, UINT flags) {
    std::scoped_lock lock(g_frame_mutex);
    cleanupRenderTarget();
    const auto hr = g_original_resize(swapchain, count, w, h, fmt, flags);
    if (g_ready.load()) createRenderTarget(swapchain);
    return hr;
}

bool hookPresent() {
    WNDCLASSEXW wc{sizeof(WNDCLASSEXW), CS_CLASSDC, DefWindowProcW, 0, 0, GetModuleHandleW(nullptr), nullptr, nullptr, nullptr, nullptr, L"ILSDummy", nullptr};
    RegisterClassExW(&wc);
    HWND hwnd = CreateWindowW(wc.lpszClassName, L"", WS_OVERLAPPEDWINDOW, 0, 0, 100, 100, nullptr, nullptr, wc.hInstance, nullptr);
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 1;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    ID3D11Device *device = nullptr; ID3D11DeviceContext *ctx = nullptr; IDXGISwapChain *swap = nullptr;
    D3D_FEATURE_LEVEL level{}; const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
    if (FAILED(D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 2, D3D11_SDK_VERSION, &sd, &swap, &device, &level, &ctx))) {
        DestroyWindow(hwnd); UnregisterClassW(wc.lpszClassName, wc.hInstance); return false;
    }
    auto **vt = *reinterpret_cast<void ***>(swap);
    auto *present_addr = vt[8]; auto *resize_addr = vt[13];
    swap->Release(); ctx->Release(); device->Release();
    DestroyWindow(hwnd); UnregisterClassW(wc.lpszClassName, wc.hInstance);
    static safetyhook::InlineHook present_hook; static safetyhook::InlineHook resize_hook;
    auto p = safetyhook::InlineHook::create(present_addr, reinterpret_cast<void *>(presentHook));
    auto r = safetyhook::InlineHook::create(resize_addr, reinterpret_cast<void *>(resizeHook));
    if (!p || !r) return false;
    present_hook = std::move(*p); resize_hook = std::move(*r);
    g_original_present = reinterpret_cast<PresentFn>(present_hook.original<void *>());
    g_original_resize = reinterpret_cast<ResizeBuffersFn>(resize_hook.original<void *>());
    return true;
}

} // namespace

bool startOverlay() { return hookPresent(); }

void stopOverlay() {
    std::scoped_lock lock(g_frame_mutex);
    if (g_ready.load()) {
        if (g_original_wndproc && g_hwnd)
            SetWindowLongPtrW(g_hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(g_original_wndproc));
        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        cleanupRenderTarget();
        if (g_icon_srv) { g_icon_srv->Release(); g_icon_srv = nullptr; }
        if (g_tab_icon_srv) { g_tab_icon_srv->Release(); g_tab_icon_srv = nullptr; }
        if (g_context) { g_context->Release(); g_context = nullptr; }
        if (g_device) { g_device->Release(); g_device = nullptr; }
        g_ready.store(false);
    }
}

} // namespace oc::ui
