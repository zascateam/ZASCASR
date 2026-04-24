#include "config.h"
#include "logger.h"
#include "utils.h"
#include "process.h"
#include "win_service.h"
#include "updater.h"
#include "initializer.h"
#include <vector>
#include <string>

void HandleControlCommand(const std::string& action, const std::string& port) {
    if (action == "stop") { StopAndUninstallService(); return; }
    if (action == "restart") { StopAndUninstallService(); while (GetServiceState() != SERVICE_STOPPED) Sleep(200); InstallAndStartService(port); return; }

    if (action == "start") {
        DWORD state = GetServiceState();
        if (state == SERVICE_RUNNING) {
            std::string msg = "检测到服务已在运行。\n\n【是】：关闭现有服务并重新启动。\n【否】：忽略警告，单开一个黑框前台运行(用于多端口调试，不守护)。\n【取消】：放弃操作。";
            int res = ShowMessage("冲突警告", msg, MB_YESNOCANCEL | MB_ICONWARNING);
            if (res == IDYES) { StopAndUninstallService(); while (GetServiceState() != SERVICE_STOPPED) Sleep(200); InstallAndStartService(port); return; }
            else if (res == IDNO) { ExecuteCommand("uv run python manage.py runserver " + port, 0, false); return; }
            else return;
        } else { InstallAndStartService(port); }
    }
}

void HandlePassthrough(const std::vector<std::string>& args) {
    std::string fullCmd = "uv run python manage.py";
    for (const auto& arg : args) fullCmd += " " + arg;
    if (!ExecuteCommand(fullCmd, 0, false)) {
        ShowMessage("执行出错", "无法启动进程，请检查 uv 和 python 环境。", MB_ICONERROR);
    }
}

int main(int argc, char* argv[]) {
    LogInit();
    LogInfo("=== ZASCA Guard started ===");
    LogDebug("Arguments count: " + std::to_string(argc));
    for (int i = 0; i < argc; i++) {
        LogDebug("  argv[" + std::to_string(i) + "]: " + std::string(argv[i]));
    }

    if (argc > 1 && std::string(argv[1]) == "--run") {
        LogInfo("Running as service mode");
        g_Port = (argc > 2) ? argv[2] : "8000";
        SERVICE_TABLE_ENTRYA ServiceTable[] = { {(LPSTR)SERVICE_NAME, (LPSERVICE_MAIN_FUNCTIONA)ServiceMain}, {NULL, NULL} };
        StartServiceCtrlDispatcherA(ServiceTable);
        return 0;
    }

    InitConsole();

    AutoInitOnFirstRun();

    std::string action = "start"; std::string port = "8000"; std::vector<std::string> passthrough_args;
    if (argc > 1) {
        std::string arg1 = argv[1];
        if (IsNumber(arg1)) { port = arg1; }
        else if (arg1 == "start" || arg1 == "restart") { action = arg1; if (argc > 2 && IsNumber(argv[2])) port = argv[2]; }
        else if (arg1 == "stop" || arg1 == "init" || arg1 == "update") { action = arg1; }
        else { action = "passthrough"; passthrough_args.assign(argv + 1, argv + argc); }
    }

    LogInfo("Action: " + action + ", Port: " + port);

    if (action == "stop") StopAndUninstallService();
    else if (action == "start" || action == "restart") HandleControlCommand(action, port);
    else if (action == "init") HandleInit();
    else if (action == "update") HandleUpdate();
    else if (action == "passthrough") HandlePassthrough(passthrough_args);

    LogInfo("=== ZASCA Guard exited ===");
    return 0;
}
