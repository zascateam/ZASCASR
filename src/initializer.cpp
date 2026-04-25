#include "initializer.h"
#include "logger.h"
#include "utils.h"
#include "security.h"
#include "process.h"
#include "http_client.h"
#include "json_parser.h"
#include "uv_installer.h"
#include "updater.h"
#include "config.h"
#include <windows.h>

bool IsFirstRun() {
    std::string exeDir = GetExeDirectory();
    std::string markerFile = exeDir + "\\.initialized";

    DWORD attrs = GetFileAttributesA(markerFile.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        return true;
    }
    return false;
}

bool MarkAsInitialized() {
    std::string exeDir = GetExeDirectory();
    std::string markerFile = exeDir + "\\.initialized";

    HANDLE hFile = CreateFileA(markerFile.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        return false;
    }
    CloseHandle(hFile);
    return true;
}

void AutoInitOnFirstRun() {
    if (!IsFirstRun()) {
        return;
    }

    PrintInfo("检测到首次运行，正在自动初始化...");

    std::string exeDir = GetExeDirectory();
    bool initSuccess = false;

    int updateChoice = ShowMessage("初始化", "是否拉取最新版本？\n\n【是】：从 GitHub 拉取最新版本后初始化\n【否】：直接使用当前代码初始化", MB_YESNO | MB_ICONQUESTION);

    if (updateChoice == IDYES) {
        PrintInfo("正在检查最新版本...");

        std::string response = HttpGet(GITHUB_API_URL, GITHUB_RELEASES_PATH);
        if (!response.empty()) {
            std::string firstRelease = ExtractFirstJsonObject(response);
            if (!firstRelease.empty()) {
                std::string zipUrl = ParseJsonString(firstRelease, "zipball_url");
                std::string tagName = ParseJsonString(firstRelease, "tag_name");

                if (!zipUrl.empty()) {
                    PrintInfo("发现最新版本: " + tagName + ", 下载源: " + zipUrl);

                    if (UpdateFromRelease(zipUrl)) {
                        PrintSuccess("更新完成！");
                    } else {
                        int retry = ShowMessage("更新失败", "更新失败，是否继续使用当前代码初始化？", MB_YESNO | MB_ICONWARNING);
                        if (retry != IDYES) return;
                    }
                }
            }
        }
    }

    if (RunCommand("where uv") != 0) {
        PrintInfo("未检测到 uv，正在通过国内代理自动安装...");
        if (!InstallUv()) {
            ShowMessage("错误", "uv 安装失败。可能是 GitHub 代理失效，请尝试手动安装 uv。", MB_ICONERROR);
            return;
        }
        RefreshEnvironment();
        Sleep(1000);
    }

    PrintInfo("正在执行 uv sync (Python解释器和依赖包均使用国内镜像加速)...");

    ResetDirectoryPermissionsToInherited(exeDir);

    HANDLE hProcess = NULL;
    if (ExecuteCommand("uv sync", 0, false, &hProcess)) {
        PrintProgressBar("uv sync", 0);
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

        if (exitCode == 0) {
            initSuccess = true;
        } else {
            PrintWarning("uv sync 执行异常，请查看上方日志。");
            return;
        }
    } else {
        ShowMessage("错误", "无法执行 uv sync。", MB_ICONERROR);
        return;
    }

    if (initSuccess) {
        if (!SetDirectoryPermissionsAdminOnly(exeDir)) {
            PrintWarning("初始化完成，但设置目录权限时遇到问题。");
        }

        if (MarkAsInitialized()) {
            PrintSuccess("首次启动初始化完成！目录权限已设置为仅管理员可访问。");
        } else {
            PrintWarning("首次启动初始化完成！但标记文件创建失败。");
        }
    }
}

void HandleInit() {
    LogInfo("=== HandleInit started ===");
    int updateChoice = ShowMessage("初始化", "是否拉取最新版本？\n\n【是】：从 GitHub 拉取最新版本后初始化\n【否】：直接使用当前代码初始化", MB_YESNOCANCEL | MB_ICONQUESTION);

    LogInfo("User choice for update: " + std::to_string(updateChoice) + " (IDYES=6, IDNO=7, IDCANCEL=2)");

    if (updateChoice == IDCANCEL) {
        LogInfo("User cancelled init");
        return;
    }

    if (updateChoice == IDYES) {
        LogInfo("User chose to update from GitHub");
        PrintInfo("正在检查最新版本...");

        LogInfo("Fetching releases from GitHub API");
        std::string response = HttpGet(GITHUB_API_URL, GITHUB_RELEASES_PATH);
        if (response.empty()) {
            LogError("GitHub API response is empty");
            int retry = ShowMessage("错误", "无法连接到 GitHub API，请检查网络连接。\n\n是否继续使用当前代码初始化？", MB_YESNO | MB_ICONWARNING);
            if (retry != IDYES) {
                LogInfo("User chose not to continue after API error");
                return;
            }
        } else {
            LogInfo("GitHub API response received, length: " + std::to_string(response.length()));

            LogInfo("Extracting first release object from JSON array");
            std::string firstRelease = ExtractFirstJsonObject(response);
            if (firstRelease.empty()) {
                LogError("Failed to extract first JSON object from response");
                int retry = ShowMessage("错误", "无法解析 GitHub API 响应。\n\n是否继续使用当前代码初始化？", MB_YESNO | MB_ICONWARNING);
                if (retry != IDYES) return;
            } else {
                LogDebug("Extracted first release object, length: " + std::to_string(firstRelease.length()));

                LogInfo("Parsing zipball_url from first release");
                std::string zipUrl = ParseJsonString(firstRelease, "zipball_url");
                LogInfo("Parsing tag_name from first release");
                std::string tagName = ParseJsonString(firstRelease, "tag_name");

                if (zipUrl.empty()) {
                    LogError("zipball_url is empty after parsing");
                    LogDebug("First release JSON: " + firstRelease.substr(0, 1000));
                    int retry = ShowMessage("错误", "无法获取下载链接。\n\n是否继续使用当前代码初始化？", MB_YESNO | MB_ICONWARNING);
                    if (retry != IDYES) return;
                } else {
                    LogInfo("Successfully parsed release info:");
                    LogInfo("  tag_name: " + tagName);
                    LogInfo("  zipball_url: " + zipUrl);

                    PrintInfo("发现最新版本: " + tagName);
                    std::string msg = "发现最新版本: " + tagName + "\n\n是否立即下载更新？\n\n下载源: " + zipUrl;
                    int result = ShowMessage("发现更新", msg, MB_YESNO | MB_ICONQUESTION);

                    if (result == IDYES) {
                        LogInfo("User confirmed download, starting...");
                        if (!UpdateFromRelease(zipUrl)) {
                            LogError("UpdateFromRelease failed");
                            int retry = ShowMessage("更新失败", "更新失败，是否继续使用当前代码初始化？", MB_YESNO | MB_ICONWARNING);
                            if (retry != IDYES) return;
                        } else {
                            LogInfo("Update completed successfully");
                            PrintSuccess("更新完成！");
                        }
                    } else {
                        LogInfo("User declined download");
                    }
                }
            }
        }
    } else {
        LogInfo("User chose to skip update, using current code");
    }

    LogInfo("Checking for uv installation...");
    if (RunCommand("where uv") != 0) {
        LogInfo("uv not found, installing...");
        PrintInfo("未检测到 uv，正在通过国内代理自动安装...");
        if (!InstallUv()) {
            LogError("uv installation failed");
            ShowMessage("错误", "uv 安装失败。可能是 GitHub 代理失效，请尝试手动安装 uv。", MB_ICONERROR); return;
        }
        LogInfo("uv installed successfully");
        RefreshEnvironment();
    } else {
        LogInfo("uv already installed");
    }

    LogInfo("Running uv sync...");

    std::string exeDir = GetExeDirectory();
    LogInfo("Resetting directory permissions before uv sync...");
    ResetDirectoryPermissionsToInherited(exeDir);

    PrintInfo("正在执行 uv sync (Python解释器和依赖包均使用国内镜像加速)...");

    HANDLE hProcess = NULL;
    if (ExecuteCommand("uv sync", 0, false, &hProcess)) {
        PrintProgressBar("uv sync", 0);
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
        LogInfo("uv sync exit code: " + std::to_string(exitCode));
        if (exitCode == 0) {
            LogInfo("uv sync completed successfully");
            if (!SetDirectoryPermissionsAdminOnly(exeDir)) {
                LogError("Failed to set admin-only permissions after uv sync");
            }
            PrintSuccess("环境初始化完成。");
        } else {
            LogError("uv sync failed with exit code: " + std::to_string(exitCode));
            PrintWarning("uv sync 执行异常，请查看上方日志。");
        }
    } else {
        LogError("Failed to execute uv sync");
        ShowMessage("错误", "无法执行 uv sync。", MB_ICONERROR);
    }
    LogInfo("=== HandleInit completed ===");
}
