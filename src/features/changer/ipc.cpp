#include "features/changer/ipc.h"
#include "features/changer/Changer.h"
#include "features/changer/catalog/catalog.h"
#include "features/changer/loadout/loadout.h"
#include "app/app.h"

#include <glaze/glaze.hpp>
#include <windows.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

namespace oc::changer {

struct IpcSticker { uint8_t Slot=0; uint32_t Kit=0; float Wear=0,Scale=1,Rotation=0,OffsetX=0,OffsetY=0; int32_t Schema=-1; };
struct IpcPatch { uint8_t Slot=0; uint32_t Kit=0; };
struct IpcCharm { uint32_t Kit=0,Pattern=0,Highlight=0; float OffsetX=0,OffsetY=0,OffsetZ=0; };
struct IpcItem {
    uint8_t Team=0; uint16_t Slot=0, Definition=0; uint32_t Paint=0,Seed=0; float Wear=0;
    bool Legacy=false, StatTrak=false; uint32_t StatTrakCount=0; std::string Name;
    std::vector<IpcSticker> Stickers; std::vector<IpcPatch> Patches; std::optional<IpcCharm> Charm;
    uint32_t SouvenirEvent=0, Music=0; bool Shown=false;
};
struct IpcCmd { std::string action; IpcItem item; std::vector<IpcItem> items; };

namespace {

std::atomic<bool> g_running{false};
HANDLE g_thread = nullptr;

std::filesystem::path baseDir() {
    const wchar_t *app = _wgetenv(L"APPDATA");
    std::filesystem::path p = app ? app : L"."; 
    p /= L"Zoowie";
    std::filesystem::create_directories(p);
    return p;
}

ItemRecord toRecord(const IpcItem &i) {
    ItemRecord r{};
    r.Team=i.Team; r.Slot=i.Slot; r.Definition=i.Definition; r.Paint=i.Paint; r.Seed=i.Seed; r.Wear=i.Wear;
    r.Legacy=i.Legacy; r.StatTrak=i.StatTrak; r.StatTrakCount=i.StatTrakCount; r.Name=i.Name;
    for (auto &s: i.Stickers) r.Stickers.push_back({s.Slot,s.Kit,s.Wear,s.Scale,s.Rotation,s.OffsetX,s.OffsetY,s.Schema});
    for (auto &p: i.Patches) r.Patches.push_back({p.Slot,p.Kit});
    if (i.Charm) r.Charm = oc::changer::Charm{i.Charm->Kit,i.Charm->Pattern,i.Charm->Highlight,i.Charm->OffsetX,i.Charm->OffsetY,i.Charm->OffsetZ};
    r.SouvenirEvent=i.SouvenirEvent; r.Music=i.Music; r.Shown=i.Shown;
    return r;
}

void writeFile(const std::filesystem::path &p, std::string_view s) {
    auto tmp = p; tmp += L".tmp";
    std::ofstream f(tmp, std::ios::binary); f << s; f.close();
    std::error_code ec; std::filesystem::rename(tmp, p, ec);
    if (ec) { std::filesystem::remove(p, ec); std::filesystem::rename(tmp, p, ec); }
}

void exportState() {
    auto &ch = Changer::get();
    auto loadout = ch.loadout().get();
    std::string json = glz::write_json(loadout).value_or("{}");
    writeFile(baseDir() / L"skinchanger_state.json", json);
}

void exportCatalog() {
    auto &cat = Changer::get().catalog();
    std::string json = glz::write_json(cat).value_or("{}");
    writeFile(baseDir() / L"skinchanger_catalog.json", json);
}

void processCommands() {
    auto cmdPath = baseDir() / L"skinchanger_cmd.json";
    std::error_code ec;
    if (!std::filesystem::exists(cmdPath, ec)) return;
    std::ifstream f(cmdPath, std::ios::binary);
    std::string s((std::istreambuf_iterator<char>(f)), {});
    f.close();
    std::filesystem::remove(cmdPath, ec);
    auto cmd = glz::read_json<IpcCmd>(s);
    if (!cmd) { writeFile(baseDir() / L"skinchanger_status.txt", "bad json"); return; }
    auto &ch = Changer::get();
    std::string status = "ok";
    if (cmd->action == "equip") {
        auto r = ch.equip(toRecord(cmd->item));
        if (!r) status = "equip error: " + r.error();
    } else if (cmd->action == "unequip") {
        ch.unequip(toRecord(cmd->item));
        status = "unequipped";
    } else if (cmd->action == "refresh") {
        exportCatalog(); exportState(); status = "refreshed";
    } else if (cmd->action == "apply") {
        auto cur = ch.loadout().get();
        if (cur) for (auto &it : cur->Items) ch.unequip(it);
        for (auto &it : cmd->items) { auto r = ch.equip(toRecord(it)); if (!r) status = "apply error: " + r.error(); }
        if (status == "ok") status = "applied";
    } else if (cmd->action == "unload") {
        writeFile(baseDir() / L"skinchanger_status.txt", "unloading");
        exportState();
        g_running.store(false);
        CreateThread(nullptr, 0, [](LPVOID)->DWORD {
            Sleep(200);
            oc::app::unhook();
            oc::app::stop();
            if (HMODULE m = GetModuleHandleW(L"zoowie_internal.dll")) FreeLibraryAndExitThread(m, 0);
            return 0;
        }, nullptr, 0, nullptr);
        return;
    } else {
        status = "unknown action";
    }
    writeFile(baseDir() / L"skinchanger_status.txt", status);
    exportState();
}

DWORD WINAPI loop(LPVOID) {
    exportCatalog();
    exportState();
    while (g_running.load()) {
        processCommands();
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    return 0;
}

} // namespace

void startIpc() {
    if (g_running.exchange(true)) return;
    g_thread = CreateThread(nullptr, 0, loop, nullptr, 0, nullptr);
}

void stopIpc() {
    if (!g_running.exchange(false)) return;
    if (g_thread) { WaitForSingleObject(g_thread, 2000); CloseHandle(g_thread); g_thread = nullptr; }
}

} // namespace oc::changer
