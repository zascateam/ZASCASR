#include "process.h"
#include "security.h"
#include "logger.h"
#include <vector>

LPVOID CreateChinaMirrorEnvBlock() {
    LPCH parentEnv = GetEnvironmentStrings();
    std::string envBlock;

    for (LPCH p = parentEnv; *p; p += strlen(p) + 1) {
        std::string entry = p;
        if (entry.find("UV_INDEX_URL=") == 0 || entry.find("UV_EXTRA_INDEX_URL=") == 0) continue;
        if (entry.find("UV_PYTHON_INSTALL_MIRROR=") == 0) continue;
        if (entry.find("installer_base_url") == 0) continue;

        envBlock += entry + '\0';
    }
    FreeEnvironmentStrings(parentEnv);

    envBlock += "UV_INSTALLER_GITHUB_BASE_URL=https://zasca.cc.cd";
    envBlock += '\0';
    envBlock += "UV_PYTHON_INSTALL_MIRROR=https://mirrors.tuna.tsinghua.edu.cn/python/";
    envBlock += '\0';
    envBlock += "UV_INDEX_URL=https://pypi.tuna.tsinghua.edu.cn/simple";
    envBlock += '\0';

    envBlock += '\0';

    LPVOID envMem = LocalAlloc(LPTR, envBlock.length());
    if (envMem) memcpy(envMem, envBlock.c_str(), envBlock.length());
    return envMem;
}

void FreeEnvBlock(LPVOID env) { if (env) LocalFree(env); }

BOOL ExecuteCommand(const std::string& cmd, DWORD flags, bool isProtected, LPHANDLE hProcessOut) {
    STARTUPINFOA si; PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si)); si.cb = sizeof(si);
    if (flags & CREATE_NO_WINDOW) { si.dwFlags = STARTF_USESHOWWINDOW; si.wShowWindow = SW_HIDE; }
    ZeroMemory(&pi, sizeof(pi));

    SECURITY_ATTRIBUTES sa = {0}; PSECURITY_DESCRIPTOR pSD = NULL;
    if (isProtected) {
        pSD = CreateProtectedSD();
        if (pSD) { sa.nLength = sizeof(SECURITY_ATTRIBUTES); sa.lpSecurityDescriptor = pSD; sa.bInheritHandle = FALSE; }
    }

    LPVOID envBlock = CreateChinaMirrorEnvBlock();
    char* cmdBuf = new char[cmd.length() + 1]; strcpy_s(cmdBuf, cmd.length() + 1, cmd.c_str());

    BOOL success = CreateProcessA(NULL, cmdBuf, isProtected ? &sa : NULL, NULL, FALSE, flags, envBlock, NULL, &si, &pi);

    delete[] cmdBuf;
    FreeEnvBlock(envBlock);
    if (pSD) LocalFree(pSD);

    if (success) {
        CloseHandle(pi.hThread);
        if (hProcessOut) *hProcessOut = pi.hProcess; else CloseHandle(pi.hProcess);
    }
    return success;
}

DWORD RunCommand(const std::string& cmd, bool isProtected) {
    HANDLE hProcess = NULL;
    if (!ExecuteCommand(cmd, CREATE_NO_WINDOW, isProtected, &hProcess)) return GetLastError();
    WaitForSingleObject(hProcess, INFINITE);
    DWORD exitCode = 0; GetExitCodeProcess(hProcess, &exitCode);
    CloseHandle(hProcess);
    return exitCode;
}

DWORD RunCommandWithOutput(const std::string& cmd, std::string& output, bool isProtected) {
    HANDLE hReadPipe = NULL, hWritePipe = NULL;
    HANDLE hProcess = NULL;
    PSECURITY_DESCRIPTOR pSD = NULL;
    LPVOID envBlock = NULL;
    char* cmdBuf = NULL;
    DWORD exitCode = 1;

    SECURITY_ATTRIBUTES saAttr;
    saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
    saAttr.bInheritHandle = TRUE;
    saAttr.lpSecurityDescriptor = NULL;

    if (!CreatePipe(&hReadPipe, &hWritePipe, &saAttr, 0)) {
        LogError("Failed to create pipe for command output, error: " + std::to_string(GetLastError()));
        return GetLastError();
    }
    SetHandleInformation(hReadPipe, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = hWritePipe;
    si.hStdError = hWritePipe;
    ZeroMemory(&pi, sizeof(pi));

    SECURITY_ATTRIBUTES procSA = {0};
    if (isProtected) {
        pSD = CreateProtectedSD();
        if (pSD) {
            procSA.nLength = sizeof(SECURITY_ATTRIBUTES);
            procSA.lpSecurityDescriptor = pSD;
            procSA.bInheritHandle = FALSE;
        }
    }

    envBlock = CreateChinaMirrorEnvBlock();
    cmdBuf = new char[cmd.length() + 1];
    strcpy_s(cmdBuf, cmd.length() + 1, cmd.c_str());

    LogInfo("Executing command: " + cmd);

    BOOL success = CreateProcessA(NULL, cmdBuf, isProtected ? &procSA : NULL, NULL, TRUE,
                                   CREATE_NO_WINDOW, envBlock, NULL, &si, &pi);

    if (!success) {
        LogError("Failed to create process, error: " + std::to_string(GetLastError()));
        exitCode = GetLastError();
        goto cleanup;
    }

    hProcess = pi.hProcess;
    CloseHandle(pi.hThread);

    CloseHandle(hWritePipe);
    hWritePipe = NULL;

    char buffer[4096];
    DWORD bytesRead;
    while (ReadFile(hReadPipe, buffer, sizeof(buffer) - 1, &bytesRead, NULL) && bytesRead > 0) {
        buffer[bytesRead] = '\0';
        output += buffer;
    }

    WaitForSingleObject(hProcess, INFINITE);
    GetExitCodeProcess(hProcess, &exitCode);

    LogInfo("Command exit code: " + std::to_string(exitCode));
    if (!output.empty()) {
        if (output.length() > 2000) {
            LogInfo("Command output (first 2000 chars):\n" + output.substr(0, 2000) + "\n... (truncated)");
        } else {
            LogInfo("Command output:\n" + output);
        }
    }

cleanup:
    if (hReadPipe) CloseHandle(hReadPipe);
    if (hWritePipe) CloseHandle(hWritePipe);
    if (hProcess) CloseHandle(hProcess);
    if (cmdBuf) delete[] cmdBuf;
    if (envBlock) FreeEnvBlock(envBlock);
    if (pSD) LocalFree(pSD);

    return exitCode;
}

void RefreshEnvironment() { SendMessageTimeout(HWND_BROADCAST, WM_SETTINGCHANGE, 0, (LPARAM)"Environment", SMTO_ABORTIFHUNG, 5000, NULL); }
