#include "win_service.h"
#include "config.h"
#include "logger.h"
#include "utils.h"
#include "security.h"
#include "process.h"
#include "uv_installer.h"

std::string g_Port = "8000";

static SERVICE_STATUS        g_ServiceStatus = {0};
static SERVICE_STATUS_HANDLE g_StatusHandle = NULL;
static HANDLE                g_ServiceStopEvent = INVALID_HANDLE_VALUE;

DWORD GetServiceState() {
    SC_HANDLE scm = OpenSCManagerA(NULL, NULL, SC_MANAGER_CONNECT);
    if (!scm) return SERVICE_STOPPED;
    SC_HANDLE svc = OpenServiceA(scm, SERVICE_NAME, SERVICE_QUERY_STATUS);
    if (!svc) { CloseServiceHandle(scm); return SERVICE_STOPPED; }
    SERVICE_STATUS status;
    if (QueryServiceStatus(svc, &status)) { CloseServiceHandle(svc); CloseServiceHandle(scm); return status.dwCurrentState; }
    CloseServiceHandle(svc); CloseServiceHandle(scm);
    return SERVICE_STOPPED;
}

void InstallAndStartService(const std::string& port) {
    char exePath[MAX_PATH]; GetModuleFileNameA(NULL, exePath, MAX_PATH);
    std::string cmdLine = std::string("\"") + exePath + "\" --run " + port;
    SC_HANDLE schSCManager = OpenSCManagerA(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (!schSCManager) { ShowMessage("错误", "无法打开服务控制管理器，请确保以管理员身份运行。", MB_ICONERROR); return; }

    SC_HANDLE schService = CreateServiceA(schSCManager, SERVICE_NAME, DISPLAY_NAME, SERVICE_ALL_ACCESS, SERVICE_WIN32_OWN_PROCESS, SERVICE_AUTO_START, SERVICE_ERROR_IGNORE, cmdLine.c_str(), NULL, NULL, NULL, NULL, NULL);
    if (!schService && GetLastError() == ERROR_SERVICE_EXISTS) {
        schService = OpenServiceA(schSCManager, SERVICE_NAME, SERVICE_ALL_ACCESS);
        ChangeServiceConfigA(schService, SERVICE_WIN32_OWN_PROCESS, SERVICE_AUTO_START, SERVICE_ERROR_IGNORE, cmdLine.c_str(), NULL, NULL, NULL, NULL, NULL, NULL);
    }
    if (schService) {
        std::string dir = exePath; dir = dir.substr(0, dir.find_last_of("\\"));
        std::string icaclsCmd = "icacls \"" + dir + "\" /deny \"Users:(OI)(CI)(DE,DC)\" /deny \"INTERACTIVE:(OI)(CI)(DE,DC)\" /T /C /Q";
        WinExec(icaclsCmd.c_str(), SW_HIDE);
        StartServiceA(schService, 0, NULL);
        PrintSuccess("服务已启动并在后台守护运行。");
        CloseServiceHandle(schService);
    } else { ShowMessage("错误", "无法创建或打开服务。", MB_ICONERROR); }
    CloseServiceHandle(schSCManager);
}

void StopAndUninstallService() {
    SC_HANDLE scm = OpenSCManagerA(NULL, NULL, SC_MANAGER_ALL_ACCESS); if (!scm) return;
    SC_HANDLE svc = OpenServiceA(scm, SERVICE_NAME, SERVICE_STOP | DELETE);
    if (svc) {
        SERVICE_STATUS status; ControlService(svc, SERVICE_CONTROL_STOP, &status);
        while (GetServiceState() != SERVICE_STOPPED) Sleep(500);
        DeleteService(svc); CloseHandle(svc);
        PrintSuccess("服务已停止并卸载。");
    } else { PrintInfo("服务未运行或未安装。"); }
    CloseServiceHandle(scm);
}

void WorkerThread() {
    if (RunCommand("where uv") != 0) {
        if (!InstallUv()) return;
        RefreshEnvironment();
    }
    {
        std::string exeDir = GetExeDirectory();
        ResetDirectoryPermissionsToInherited(exeDir);
        PrintProgressBar("uv sync", 0);
        HANDLE hProcess = NULL;
        if (ExecuteCommand("uv sync", CREATE_NO_WINDOW, true, &hProcess)) {
            DWORD exitCode = STILL_ACTIVE;
            int progress = 0;
            while (exitCode == STILL_ACTIVE) {
                Sleep(500);
                if (!GetExitCodeProcess(hProcess, &exitCode)) break;
                progress += 5;
                if (progress > 95) progress = 95;
                PrintProgressBar("uv sync", progress);
            }
            CloseHandle(hProcess);
            PrintProgressBar("uv sync", 100);
            if (exitCode != 0) return;
        } else {
            return;
        }
        SetDirectoryPermissionsAdminOnly(exeDir);
    }

    std::string serverCmd = "uv run python manage.py runserver " + g_Port;
    while (WaitForSingleObject(g_ServiceStopEvent, 0) == WAIT_TIMEOUT) {
        HANDLE hProcess = NULL;
        if (ExecuteCommand(serverCmd, CREATE_NO_WINDOW, true, &hProcess)) {
            HANDLE handles[2] = { hProcess, g_ServiceStopEvent };
            WaitForMultipleObjects(2, handles, FALSE, INFINITE);
            CloseHandle(hProcess);
        }
        if (WaitForSingleObject(g_ServiceStopEvent, 0) == WAIT_TIMEOUT) Sleep(3000);
    }
}

void ServiceCtrlHandler(DWORD CtrlCode) {
    if (CtrlCode == SERVICE_CONTROL_STOP) { g_ServiceStatus.dwCurrentState = SERVICE_STOP_PENDING; SetServiceStatus(g_StatusHandle, &g_ServiceStatus); SetEvent(g_ServiceStopEvent); }
}

void ServiceMain(DWORD argc, LPTSTR* argv) {
    g_StatusHandle = RegisterServiceCtrlHandlerA(SERVICE_NAME, ServiceCtrlHandler);
    g_ServiceStopEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    g_ServiceStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS; g_ServiceStatus.dwCurrentState = SERVICE_START_PENDING;
    SetServiceStatus(g_StatusHandle, &g_ServiceStatus);
    g_ServiceStatus.dwCurrentState = SERVICE_RUNNING;
    SetServiceStatus(g_StatusHandle, &g_ServiceStatus);
    WorkerThread();
    CloseHandle(g_ServiceStopEvent);
    g_ServiceStatus.dwCurrentState = SERVICE_STOPPED;
    SetServiceStatus(g_StatusHandle, &g_ServiceStatus);
}
