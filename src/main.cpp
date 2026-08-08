// Entry point. Keeps startup work to a minimum: no COM, no heavy initialisation
// until a document actually needs it.
#include "win_viewer.h"

#include <commctrl.h>
#include <shellapi.h>

// Ask for version 6 common controls so the standard controls and scrollbars use
// the modern (and dark-mode aware) themes.
#pragma comment(linker,                                                        \
                "/manifestdependency:\"type='win32' "                          \
                "name='Microsoft.Windows.Common-Controls' version='6.0.0.0' "  \
                "processorArchitecture='*' publicKeyToken='6595b64144ccf1df' " \
                "language='*'\"")

namespace {

void enablePerMonitorDpi() {
    using SetContextFn = BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT);
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (!user32) return;
    auto setContext = reinterpret_cast<SetContextFn>(
        GetProcAddress(user32, "SetProcessDpiAwarenessContext"));
    if (setContext && setContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) return;
    SetProcessDPIAware();
}

std::wstring firstCommandLineArgument() {
    int count = 0;
    LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &count);
    std::wstring result;
    if (arguments) {
        if (count > 1) result = arguments[1];
        LocalFree(arguments);
    }
    return result;
}

} // namespace

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int showCommand) {
    enablePerMonitorDpi();

    INITCOMMONCONTROLSEX controls = {sizeof(controls), ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&controls);

    app::AppWindow window;
    if (!window.create(instance, showCommand, firstCommandLineArgument())) return 1;
    return window.runMessageLoop();
}
