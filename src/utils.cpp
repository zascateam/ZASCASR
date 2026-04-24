#include "utils.h"
#include <iostream>
#include <cwctype>
#include <algorithm>

std::wstring Utf8ToWide(const std::string& str) {
    if (str.empty()) return std::wstring();
    int size = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, NULL, 0);
    if (size == 0) return std::wstring();
    std::wstring result(size - 1, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &result[0], size);
    return result;
}

bool IsNumber(const std::string& s) {
    if (s.empty()) return false;
    for (char c : s) if (!std::isdigit(c)) return false;
    return true;
}

std::string GetTempFilePath() {
    char tempPath[MAX_PATH];
    GetTempPathA(MAX_PATH, tempPath);
    char tempFile[MAX_PATH];
    GetTempFileNameA(tempPath, "ZASCA", 0, tempFile);
    return std::string(tempFile);
}

std::string GetExeDirectory() {
    char exePath[MAX_PATH];
    GetModuleFileNameA(NULL, exePath, MAX_PATH);
    std::string dir = exePath;
    size_t pos = dir.find_last_of("\\");
    return (pos != std::string::npos) ? dir.substr(0, pos) : dir;
}

bool CopyDirectoryRecursive(const std::string& srcPath, const std::string& destPath) {
    if (!CreateDirectoryA(destPath.c_str(), NULL)) {
        if (GetLastError() != ERROR_ALREADY_EXISTS) return false;
    }

    WIN32_FIND_DATAA findData;
    std::string searchPath = srcPath + "\\*";
    HANDLE hFind = FindFirstFileA(searchPath.c_str(), &findData);

    if (hFind == INVALID_HANDLE_VALUE) return false;

    bool success = true;
    do {
        if (strcmp(findData.cFileName, ".") == 0 || strcmp(findData.cFileName, "..") == 0) continue;

        std::string srcItem = srcPath + "\\" + findData.cFileName;
        std::string destItem = destPath + "\\" + findData.cFileName;

        if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (!CopyDirectoryRecursive(srcItem, destItem)) success = false;
        } else {
            if (!CopyFileA(srcItem.c_str(), destItem.c_str(), FALSE)) success = false;
        }
    } while (FindNextFileA(hFind, &findData));

    FindClose(hFind);
    return success;
}

bool RemoveDirectoryRecursive(const std::string& dirPath) {
    WIN32_FIND_DATAA findData;
    std::string searchPath = dirPath + "\\*";
    HANDLE hFind = FindFirstFileA(searchPath.c_str(), &findData);

    if (hFind == INVALID_HANDLE_VALUE) return false;

    bool success = true;
    do {
        if (strcmp(findData.cFileName, ".") == 0 || strcmp(findData.cFileName, "..") == 0) continue;

        std::string itemPath = dirPath + "\\" + findData.cFileName;

        if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (!RemoveDirectoryRecursive(itemPath)) success = false;
        } else {
            if (!DeleteFileA(itemPath.c_str())) success = false;
        }
    } while (FindNextFileA(hFind, &findData));

    FindClose(hFind);

    if (!RemoveDirectoryA(dirPath.c_str())) success = false;
    return success;
}

int ShowMessage(const std::string& title, const std::string& msg, UINT type) {
    return MessageBoxW(NULL, Utf8ToWide(msg).c_str(), Utf8ToWide(title).c_str(), type | MB_TOPMOST);
}

void InitConsole() {
    if (!GetConsoleWindow()) {
        if (!AttachConsole(ATTACH_PARENT_PROCESS)) {
            AllocConsole();
        }
        freopen("CONOUT$", "w", stdout);
        freopen("CONOUT$", "w", stderr);
        setvbuf(stdout, NULL, _IONBF, 0);
        setvbuf(stderr, NULL, _IONBF, 0);
    }
    SetConsoleOutputCP(65001);
    SetConsoleCP(65001);
}

void ConsolePrint(const std::string& msg) {
    std::cout << msg << std::endl;
}

void PrintInfo(const std::string& msg) {
    ConsolePrint("[INFO] " + msg);
}

void PrintSuccess(const std::string& msg) {
    ConsolePrint("[OK] " + msg);
}

void PrintWarning(const std::string& msg) {
    ConsolePrint("[WARN] " + msg);
}

void PrintError(const std::string& msg) {
    ConsolePrint("[ERROR] " + msg);
}
