#include "updater.h"
#include "http_client.h"
#include "json_parser.h"
#include "logger.h"
#include "utils.h"
#include "config.h"
#include <windows.h>
#include <shlobj.h>
#include <shellapi.h>
#include <fstream>

bool ExtractZip(const std::string& zipPath, const std::string& destPath) {
    LogInfo("=== ExtractZip started ===");
    LogInfo("ZIP file: " + zipPath);
    LogInfo("Destination: " + destPath);

    std::wstring wZipPath = Utf8ToWide(zipPath);
    std::wstring wDestPath = Utf8ToWide(destPath);

    if (wZipPath.empty() || wDestPath.empty()) {
        LogError("Failed to convert paths to wide string");
        return false;
    }

    DWORD attrs = GetFileAttributesW(wDestPath.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES || !(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
        LogInfo("Destination directory does not exist, creating: " + destPath);
        int result = SHCreateDirectoryExA(NULL, destPath.c_str(), NULL);
        if (result != ERROR_SUCCESS && result != ERROR_ALREADY_EXISTS) {
            LogError("Failed to create destination directory: " + destPath + ", error: " + std::to_string(result));
            return false;
        }
        LogInfo("Destination directory created successfully");
        Sleep(200);
    }

    std::string tempFile = destPath + "\\.placeholder";
    HANDLE hTemp = CreateFileA(tempFile.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hTemp == INVALID_HANDLE_VALUE) {
        LogError("Failed to create placeholder file, error: " + std::to_string(GetLastError()));
    } else {
        CloseHandle(hTemp);
        LogInfo("Created placeholder file: " + tempFile);
    }

    attrs = GetFileAttributesW(wDestPath.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        LogError("Destination directory still not accessible after creation");
        return false;
    }

    attrs = GetFileAttributesW(wZipPath.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        LogError("ZIP file does not exist: " + zipPath);
        return false;
    }

    HRESULT hr = CoInitialize(NULL);
    if (FAILED(hr)) {
        LogError("CoInitialize failed: " + std::to_string(hr));
        return false;
    }
    LogDebug("COM initialized");

    IShellDispatch* pShell = NULL;
    hr = CoCreateInstance(CLSID_Shell, NULL, CLSCTX_INPROC_SERVER, IID_IShellDispatch, (void**)&pShell);
    if (FAILED(hr)) {
        LogError("CoCreateInstance failed: " + std::to_string(hr));
        CoUninitialize();
        return false;
    }
    LogDebug("Shell instance created");

    Folder* pZipFile = NULL;
    VARIANT varZipPath;
    varZipPath.vt = VT_BSTR;
    varZipPath.bstrVal = SysAllocString(wZipPath.c_str());
    hr = pShell->NameSpace(varZipPath, &pZipFile);
    SysFreeString(varZipPath.bstrVal);

    if (FAILED(hr) || !pZipFile) {
        LogError("Failed to open ZIP file: " + zipPath + ", hr: " + std::to_string(hr));
        pShell->Release();
        CoUninitialize();
        return false;
    }
    LogDebug("ZIP file opened successfully");

    Folder* pDestFolder = NULL;
    VARIANT varDestPath;
    varDestPath.vt = VT_BSTR;
    varDestPath.bstrVal = SysAllocString(wDestPath.c_str());
    hr = pShell->NameSpace(varDestPath, &pDestFolder);
    SysFreeString(varDestPath.bstrVal);

    if (FAILED(hr) || !pDestFolder) {
        LogError("Failed to open destination folder: " + destPath + ", hr: " + std::to_string(hr));
        pZipFile->Release();
        pShell->Release();
        CoUninitialize();
        return false;
    }
    LogDebug("Destination folder opened successfully");

    FolderItems* pItems = NULL;
    hr = pZipFile->Items(&pItems);
    if (FAILED(hr) || !pItems) {
        LogError("Failed to get ZIP items, hr: " + std::to_string(hr));
        pDestFolder->Release();
        pZipFile->Release();
        pShell->Release();
        CoUninitialize();
        return false;
    }

    long itemCount = 0;
    pItems->get_Count(&itemCount);
    LogInfo("ZIP contains " + std::to_string(itemCount) + " items");

    if (itemCount == 0) {
        LogError("ZIP file is empty");
        pItems->Release();
        pDestFolder->Release();
        pZipFile->Release();
        pShell->Release();
        CoUninitialize();
        return false;
    }

    VARIANT varItems;
    varItems.vt = VT_DISPATCH;
    varItems.pdispVal = (IDispatch*)pItems;
    VARIANT varOptions;
    varOptions.vt = VT_I4;
    varOptions.lVal = 0;

    LogInfo("Starting extraction...");
    hr = pDestFolder->CopyHere(varItems, varOptions);

    if (FAILED(hr)) {
        LogError("CopyHere failed: " + std::to_string(hr));
        pItems->Release();
        pDestFolder->Release();
        pZipFile->Release();
        pShell->Release();
        CoUninitialize();
        return false;
    }

    LogDebug("Waiting for extraction to complete...");
    bool extractionComplete = false;
    int lastPercent = -1;
    for (int i = 0; i < 60; i++) {
        Sleep(1000);

        long currentCount = 0;
        pItems->get_Count(&currentCount);

        WIN32_FIND_DATAA findData;
        std::string searchPath = destPath + "\\*";
        HANDLE hFind = FindFirstFileA(searchPath.c_str(), &findData);
        int extractedCount = 0;
        if (hFind != INVALID_HANDLE_VALUE) {
            do {
                if (strcmp(findData.cFileName, ".") != 0 && strcmp(findData.cFileName, "..") != 0) {
                    extractedCount++;
                }
            } while (FindNextFileA(hFind, &findData));
            FindClose(hFind);

            int percent = (itemCount > 0) ? (extractedCount * 100) / static_cast<int>(itemCount) : 0;
            if (percent != lastPercent) {
                PrintProgressBar("解压", percent);
                lastPercent = percent;
            }

            if (extractedCount >= itemCount) {
                extractionComplete = true;
                PrintProgressBar("解压", 100);
                LogInfo("Extraction completed after " + std::to_string(i + 1) + " seconds");
                break;
            }
        }

        LogDebug("Waiting... (" + std::to_string(i + 1) + "s), extracted items: " + std::to_string(extractedCount));
    }

    if (!extractionComplete) {
        LogError("Extraction timeout after 60 seconds");
    }

    DeleteFileA(tempFile.c_str());
    LogDebug("Removed placeholder file");

    pItems->Release();
    pDestFolder->Release();
    pZipFile->Release();
    pShell->Release();
    CoUninitialize();

    LogInfo("=== ExtractZip completed ===");
    return extractionComplete;
}

bool UpdateFromRelease(const std::string& zipUrl) {
    std::string tempZip = GetTempFilePath() + ".zip";
    std::string tempDir = tempZip.substr(0, tempZip.length() - 4);
    DeleteFileA(tempDir.c_str());
    SHCreateDirectoryExA(NULL, tempDir.c_str(), NULL);

    LogInfo("=== UpdateFromRelease started ===");
    LogInfo("Download URL: " + zipUrl);
    LogInfo("Temp ZIP file: " + tempZip);
    LogInfo("Temp directory: " + tempDir);

    PrintInfo("正在下载最新版本...");

    if (!DownloadFile(zipUrl, tempZip)) {
        LogError("Download failed");
        ShowMessage("错误", "下载失败，请检查网络连接。", MB_ICONERROR);
        DeleteFileA(tempZip.c_str());
        RemoveDirectoryA(tempDir.c_str());
        return false;
    }

    LogInfo("Download completed successfully");

    DWORD zipAttrs = GetFileAttributesA(tempZip.c_str());
    if (zipAttrs == INVALID_FILE_ATTRIBUTES) {
        LogError("ZIP file does not exist after download: " + tempZip);
        ShowMessage("错误", "下载的文件不存在。", MB_ICONERROR);
        DeleteFileA(tempZip.c_str());
        RemoveDirectoryA(tempDir.c_str());
        return false;
    }

    LARGE_INTEGER fileSize;
    HANDLE hFile = CreateFileA(tempZip.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (hFile != INVALID_HANDLE_VALUE) {
        GetFileSizeEx(hFile, &fileSize);
        CloseHandle(hFile);
        LogInfo("ZIP file size: " + std::to_string(fileSize.QuadPart) + " bytes");

        if (fileSize.QuadPart < 1024) {
            LogError("ZIP file too small, likely corrupted");
            ShowMessage("错误", "下载的文件太小，可能已损坏。", MB_ICONERROR);
            DeleteFileA(tempZip.c_str());
            RemoveDirectoryA(tempDir.c_str());
            return false;
        }
    }

    PrintInfo("正在解压文件...");

    if (!ExtractZip(tempZip, tempDir)) {
        ShowMessage("错误", "解压失败。", MB_ICONERROR);
        DeleteFileA(tempZip.c_str());
        RemoveDirectoryA(tempDir.c_str());
        return false;
    }

    std::string exeDir = GetExeDirectory();

    WIN32_FIND_DATAA findData;
    std::string searchPath = tempDir + "\\*";
    HANDLE hFind = FindFirstFileA(searchPath.c_str(), &findData);

    std::string extractedDir = tempDir;
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                if (strcmp(findData.cFileName, ".") != 0 && strcmp(findData.cFileName, "..") != 0) {
                    extractedDir = tempDir + "\\" + findData.cFileName;
                    break;
                }
            }
        } while (FindNextFileA(hFind, &findData));
        FindClose(hFind);
    }

    PrintInfo("正在复制文件...");

    searchPath = extractedDir + "\\*";
    hFind = FindFirstFileA(searchPath.c_str(), &findData);

    int totalFiles = 0;
    int copiedFiles = 0;
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (strcmp(findData.cFileName, ".") != 0 && strcmp(findData.cFileName, "..") != 0) {
                totalFiles++;
            }
        } while (FindNextFileA(hFind, &findData));
        FindClose(hFind);
    }

    hFind = FindFirstFileA(searchPath.c_str(), &findData);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (strcmp(findData.cFileName, ".") != 0 && strcmp(findData.cFileName, "..") != 0) {
                std::string srcPath = extractedDir + "\\" + findData.cFileName;
                std::string destPath = exeDir + "\\" + findData.cFileName;

                if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                    CopyDirectoryRecursive(srcPath, destPath);
                } else {
                    CopyFileA(srcPath.c_str(), destPath.c_str(), FALSE);
                }
                copiedFiles++;
                PrintProgress("复制", copiedFiles, totalFiles);
            }
        } while (FindNextFileA(hFind, &findData));
        FindClose(hFind);
    }

    DeleteFileA(tempZip.c_str());

    searchPath = tempDir + "\\*";
    hFind = FindFirstFileA(searchPath.c_str(), &findData);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                if (strcmp(findData.cFileName, ".") != 0 && strcmp(findData.cFileName, "..") != 0) {
                    std::string subDir = tempDir + "\\" + findData.cFileName;
                    RemoveDirectoryRecursive(subDir);
                }
            } else {
                std::string filePath = tempDir + "\\" + findData.cFileName;
                DeleteFileA(filePath.c_str());
            }
        } while (FindNextFileA(hFind, &findData));
        FindClose(hFind);
    }
    RemoveDirectoryA(tempDir.c_str());

    return true;
}

void HandleUpdate() {
    LogInfo("=== HandleUpdate started ===");
    PrintInfo("正在检查最新版本...");

    LogInfo("Fetching releases from GitHub API");
    LogDebug("URL: https://" + std::string(GITHUB_API_URL) + GITHUB_RELEASES_PATH);

    std::string response = HttpGet(GITHUB_API_URL, GITHUB_RELEASES_PATH);
    if (response.empty()) {
        LogError("GitHub API response is empty");
        ShowMessage("错误", "无法连接到 GitHub API，请检查网络连接。", MB_ICONERROR);
        return;
    }

    LogInfo("GitHub API response received, length: " + std::to_string(response.length()));

    LogInfo("Extracting first release object from JSON array");
    std::string firstRelease = ExtractFirstJsonObject(response);
    if (firstRelease.empty()) {
        LogError("Failed to extract first JSON object from response");
        LogDebug("Response content: " + response.substr(0, 500));
        ShowMessage("错误", "无法解析 GitHub API 响应。", MB_ICONERROR);
        return;
    }
    LogDebug("Extracted first release object, length: " + std::to_string(firstRelease.length()));

    LogInfo("Parsing zipball_url from first release");
    std::string zipUrl = ParseJsonString(firstRelease, "zipball_url");
    LogInfo("Parsing tag_name from first release");
    std::string tagName = ParseJsonString(firstRelease, "tag_name");

    if (zipUrl.empty()) {
        LogError("zipball_url is empty after parsing");
        LogDebug("First release JSON: " + firstRelease.substr(0, 1000));
        ShowMessage("错误", "无法获取下载链接。", MB_ICONERROR);
        return;
    }

    LogInfo("Successfully parsed release info:");
    LogInfo("  tag_name: " + tagName);
    LogInfo("  zipball_url: " + zipUrl);

    PrintInfo("发现最新版本: " + tagName);
    std::string msg = "发现最新版本: " + tagName + "\n\n是否立即更新？\n\n下载源: " + zipUrl;
    int result = MessageBoxW(NULL, Utf8ToWide(msg).c_str(), Utf8ToWide("发现更新").c_str(), MB_YESNO | MB_ICONQUESTION | MB_TOPMOST);

    if (result == IDYES) {
        LogInfo("User confirmed update, starting download...");
        if (UpdateFromRelease(zipUrl)) {
            LogInfo("Update completed successfully, restarting...");
            PrintSuccess("更新完成！程序即将重启。");

            char exePath[MAX_PATH];
            GetModuleFileNameA(NULL, exePath, MAX_PATH);

            STARTUPINFOA si; PROCESS_INFORMATION pi;
            ZeroMemory(&si, sizeof(si)); si.cb = sizeof(si);
            ZeroMemory(&pi, sizeof(pi));

            CreateProcessA(NULL, exePath, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi);
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);

            exit(0);
        } else {
            LogError("UpdateFromRelease failed");
        }
    } else {
        LogInfo("User cancelled update");
        PrintInfo("更新已取消。");
    }
    LogInfo("=== HandleUpdate completed ===");
}
