#include "uv_installer.h"
#include "logger.h"
#include "process.h"
#include "utils.h"
#include <windows.h>
#include <fstream>
#include "resource.h"

bool ExtractUvInstaller(std::string& outPath) {
    HMODULE hModule = GetModuleHandleA(NULL);
    HRSRC hResource = FindResourceA(hModule, MAKEINTRESOURCEA(IDR_UV_INSTALLER), RT_RCDATA);
    if (!hResource) {
        LogError("Failed to find UV installer resource");
        return false;
    }

    HGLOBAL hData = LoadResource(hModule, hResource);
    if (!hData) {
        LogError("Failed to load UV installer resource");
        return false;
    }

    DWORD size = SizeofResource(hModule, hResource);
    const char* data = (const char*)LockResource(hData);
    if (!data || size == 0) {
        LogError("Failed to lock UV installer resource");
        FreeResource(hData);
        return false;
    }

    char tempPath[MAX_PATH];
    GetTempPathA(MAX_PATH, tempPath);
    char tempFile[MAX_PATH];
    GetTempFileNameA(tempPath, "uv", 0, tempFile);

    std::string scriptPath = std::string(tempFile) + ".ps1";
    MoveFileA(tempFile, scriptPath.c_str());

    std::ofstream outFile(scriptPath, std::ios::binary);
    if (!outFile) {
        LogError("Failed to create temp script file");
        FreeResource(hData);
        return false;
    }

    outFile.write(data, size);
    outFile.close();

    FreeResource(hData);

    outPath = scriptPath;
    LogInfo("Extracted UV installer to: " + scriptPath);
    return true;
}

static bool RefreshPathFromRegistry() {
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, "Environment", 0, KEY_READ, &hKey) != ERROR_SUCCESS) {
        LogError("Failed to open HKCU\\Environment registry key");
        return false;
    }

    char buffer[32767];
    DWORD bufferSize = sizeof(buffer);
    DWORD type;
    bool success = false;

    if (RegQueryValueExA(hKey, "Path", NULL, &type, (LPBYTE)buffer, &bufferSize) == ERROR_SUCCESS && type == REG_EXPAND_SZ) {
        char expandedPath[32767];
        if (ExpandEnvironmentStringsA(buffer, expandedPath, sizeof(expandedPath)) > 0) {
            if (SetEnvironmentVariableA("PATH", expandedPath)) {
                LogInfo("Refreshed PATH from registry: " + std::string(expandedPath).substr(0, 200) + "...");
                success = true;
            } else {
                LogError("Failed to set PATH environment variable");
            }
        }
    } else {
        LogError("Failed to read Path from registry or type mismatch");
    }

    RegCloseKey(hKey);
    return success;
}

bool InstallUv() {
    LogInfo("=== Starting UV installation ===");

    std::string scriptPath;
    if (!ExtractUvInstaller(scriptPath)) {
        LogError("Failed to extract UV installer script");
        return false;
    }
    LogInfo("UV installer script extracted to: " + scriptPath);

    std::string cmd = "powershell -ExecutionPolicy ByPass -NoProfile -File \"" + scriptPath + "\"";
    LogInfo("Executing PowerShell installer...");
    PrintProgressBar("uv安装", 0);

    std::string output;
    DWORD result = RunCommandWithOutput(cmd, output);

    if (result != 0) {
        LogError("UV installation failed with exit code: " + std::to_string(result));
        if (!output.empty()) {
            LogError("PowerShell output:\n" + output);
        }
        PrintProgressBar("uv安装", 0);
    } else {
        LogInfo("UV installation completed successfully");
        PrintProgressBar("uv安装", 100);
    }

    DeleteFileA(scriptPath.c_str());
    LogInfo("Cleaned up installer script: " + scriptPath);

    if (result == 0) {
        RefreshEnvironment();
        Sleep(500);
        RefreshPathFromRegistry();
    }

    LogInfo("=== UV installation finished ===");

    return result == 0;
}
