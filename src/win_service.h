#pragma once

#include <string>
#include <windows.h>

extern std::string g_Port;

DWORD GetServiceState();
void InstallAndStartService(const std::string& port);
void StopAndUninstallService();
void ServiceMain(DWORD argc, LPTSTR* argv);
void ServiceCtrlHandler(DWORD CtrlCode);
void WorkerThread();
