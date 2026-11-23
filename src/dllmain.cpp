#include <Windows.h>

namespace {

DWORD WINAPI init_thread(LPVOID module) {
    // TODO: initialize hooks, schema/offset cache, and visual features.
    (void)module;
    return 0;
}

} // namespace

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        if (auto *thread = CreateThread(nullptr, 0, init_thread, module, 0, nullptr)) {
            CloseHandle(thread);
        }
    }
    return TRUE;
}
