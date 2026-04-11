#include <windows.h>
#include <aclapi.h>
#include <string>
#include <vector>
#include <iostream>
#include <cwctype>
#include <algorithm>
#include <winhttp.h>
#include <shlobj.h>
#include <shellapi.h>
#include <fstream>
#include <ctime>
#include <sstream>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "shell32.lib")

#define SERVICE_NAME "DjangoGuardSvc"
#define DISPLAY_NAME "Django Environment Guard Service"
#define GITHUB_API_URL "api.zasca.cc.cd"
#define GITHUB_RELEASES_PATH "/repos/trustedinster/ZASCA/releases"
#define IDR_UV_INSTALLER 101

SERVICE_STATUS        g_ServiceStatus = {0};
SERVICE_STATUS_HANDLE g_StatusHandle = NULL;
HANDLE                g_ServiceStopEvent = INVALID_HANDLE_VALUE;
std::string           g_Port = "8000";
std::string           g_LogPath = "";

void LogInit();
void LogWrite(const std::string& level, const std::string& message);
void LogInfo(const std::string& message);
void LogError(const std::string& message);
void LogDebug(const std::string& message);
void LogHttpResponse(const std::string& host, const std::string& path, DWORD statusCode, const std::string& response);
void LogJsonParse(const std::string& json, const std::string& key, const std::string& result);

int ShowMessage(const std::string& title, const std::string& msg, UINT type);
bool IsNumber(const std::string& s);
DWORD GetServiceState();
void InstallAndStartService(const std::string& port);
void StopAndUninstallService();
void HandleControlCommand(const std::string& action, const std::string& port);
void HandlePassthrough(const std::vector<std::string>& args);
void HandleInit();
void HandleUpdate();

void ServiceMain(DWORD argc, LPTSTR* argv);
void ServiceCtrlHandler(DWORD CtrlCode);
void WorkerThread();
DWORD RunCommand(const std::string& cmd, bool isProtected = false);
DWORD RunCommandWithOutput(const std::string& cmd, std::string& output, bool isProtected = false);
void RefreshEnvironment();
PSECURITY_DESCRIPTOR CreateProtectedSD();

LPVOID CreateChinaMirrorEnvBlock();
void FreeEnvBlock(LPVOID env);
BOOL ExecuteCommand(const std::string& cmd, DWORD flags, bool isProtected, LPHANDLE hProcessOut = NULL);

std::string HttpGet(const std::string& host, const std::string& path);
bool DownloadFile(const std::string& url, const std::string& localPath);
bool ExtractZip(const std::string& zipPath, const std::string& destPath);
std::string ExtractFirstJsonObject(const std::string& json);
std::string ParseJsonString(const std::string& json, const std::string& key);
std::string GetTempFilePath();
std::string GetExeDirectory();
bool UpdateFromRelease(const std::string& zipUrl);
bool CopyDirectoryRecursive(const std::string& srcPath, const std::string& destPath);
bool RemoveDirectoryRecursive(const std::string& dirPath);
bool ExtractUvInstaller(std::string& outPath);
bool InstallUv();

std::wstring Utf8ToWide(const std::string& str) {
    if (str.empty()) return std::wstring();
    int size = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, NULL, 0);
    if (size == 0) return std::wstring();
    std::wstring result(size - 1, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &result[0], size);
    return result;
}

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
    
    std::string output;
    DWORD result = RunCommandWithOutput(cmd, output);
    
    if (result != 0) {
        LogError("UV installation failed with exit code: " + std::to_string(result));
        if (!output.empty()) {
            LogError("PowerShell output:\n" + output);
        }
    } else {
        LogInfo("UV installation completed successfully");
    }
    
    DeleteFileA(scriptPath.c_str());
    LogInfo("Cleaned up installer script: " + scriptPath);
    LogInfo("=== UV installation finished ===");
    
    return result == 0;
}

int ShowMessage(const std::string& title, const std::string& msg, UINT type = MB_OK) {
    return MessageBoxW(NULL, Utf8ToWide(msg).c_str(), Utf8ToWide(title).c_str(), type | MB_TOPMOST);
}

bool IsNumber(const std::string& s) {
    if (s.empty()) return false;
    for (char c : s) if (!std::isdigit(c)) return false;
    return true;
}

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

PSECURITY_DESCRIPTOR CreateProtectedSD() {
    PACL pACL = NULL;
    PSECURITY_DESCRIPTOR pSD = (PSECURITY_DESCRIPTOR)LocalAlloc(LPTR, SECURITY_DESCRIPTOR_MIN_LENGTH);
    if (!pSD || !InitializeSecurityDescriptor(pSD, SECURITY_DESCRIPTOR_REVISION)) return NULL;
    EXPLICIT_ACCESS ea[3]; ZeroMemory(ea, sizeof(ea));
    
    ea[0].grfAccessPermissions = PROCESS_TERMINATE; ea[0].grfAccessMode = DENY_ACCESS;
    ea[0].grfInheritance = NO_INHERITANCE; ea[0].Trustee.TrusteeForm = TRUSTEE_IS_NAME;
    ea[0].Trustee.TrusteeType = TRUSTEE_IS_GROUP; ea[0].Trustee.ptstrName = (LPTSTR)"INTERACTIVE";

    ea[1].grfAccessPermissions = PROCESS_ALL_ACCESS; ea[1].grfAccessMode = SET_ACCESS;
    ea[1].grfInheritance = NO_INHERITANCE; ea[1].Trustee.TrusteeForm = TRUSTEE_IS_NAME;
    ea[1].Trustee.TrusteeType = TRUSTEE_IS_WELL_KNOWN_GROUP; ea[1].Trustee.ptstrName = (LPTSTR)"SYSTEM";

    ea[2].grfAccessPermissions = PROCESS_ALL_ACCESS; ea[2].grfAccessMode = SET_ACCESS;
    ea[2].grfInheritance = NO_INHERITANCE; ea[2].Trustee.TrusteeForm = TRUSTEE_IS_NAME;
    ea[2].Trustee.TrusteeType = TRUSTEE_IS_WELL_KNOWN_GROUP; ea[2].Trustee.ptstrName = (LPTSTR)"Administrators";

    if (SetEntriesInAcl(3, ea, NULL, &pACL) != ERROR_SUCCESS) return NULL;
    SetSecurityDescriptorDacl(pSD, TRUE, pACL, FALSE);
    return pSD;
}

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

    envBlock += "UV_INSTALLER_GITHUB_BASE_URL=https://zasca.cc.cd/\0";
    envBlock += "UV_PYTHON_INSTALL_MIRROR=https://mirrors.tuna.tsinghua.edu.cn/python/\0";
    envBlock += "UV_INDEX_URL=https://pypi.tuna.tsinghua.edu.cn/simple\0";
    
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

std::string HttpGet(const std::string& host, const std::string& path) {
    LogInfo("HTTP GET Request started");
    LogDebug("Host: " + host + ", Path: " + path);
    
    std::string response;
    HINTERNET hSession = WinHttpOpen(L"ZASCA-Updater/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, NULL, NULL, 0);
    if (!hSession) {
        LogError("Failed to create WinHTTP session, error: " + std::to_string(GetLastError()));
        return response;
    }
    LogDebug("WinHTTP session created successfully");

    std::wstring wHost(host.begin(), host.end());
    HINTERNET hConnect = WinHttpConnect(hSession, wHost.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) {
        LogError("Failed to connect to host: " + host + ", error: " + std::to_string(GetLastError()));
        WinHttpCloseHandle(hSession);
        return response;
    }
    LogDebug("Connected to host: " + host);

    std::wstring wPath(path.begin(), path.end());
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", wPath.c_str(), NULL, NULL, NULL, WINHTTP_FLAG_SECURE);
    if (!hRequest) {
        LogError("Failed to create HTTP request, error: " + std::to_string(GetLastError()));
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return response;
    }
    LogDebug("HTTP request created for path: " + path);

    BOOL bResults = WinHttpSendRequest(hRequest, NULL, 0, NULL, 0, 0, 0);
    if (!bResults) {
        LogError("Failed to send HTTP request, error: " + std::to_string(GetLastError()));
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return response;
    }
    LogDebug("HTTP request sent successfully");

    bResults = WinHttpReceiveResponse(hRequest, NULL);
    if (!bResults) {
        LogError("Failed to receive HTTP response, error: " + std::to_string(GetLastError()));
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return response;
    }

    DWORD dwStatusCode = 0;
    DWORD dwSize = sizeof(dwStatusCode);
    WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, NULL, &dwStatusCode, &dwSize, NULL);
    LogDebug("HTTP Status Code: " + std::to_string(dwStatusCode));

    dwSize = 0;
    do {
        DWORD dwDownloaded = 0;
        if (!WinHttpQueryDataAvailable(hRequest, &dwSize)) {
            LogError("Failed to query data available, error: " + std::to_string(GetLastError()));
            break;
        }
        if (dwSize == 0) break;
        
        std::vector<char> buffer(dwSize + 1);
        if (!WinHttpReadData(hRequest, &buffer[0], dwSize, &dwDownloaded)) {
            LogError("Failed to read HTTP data, error: " + std::to_string(GetLastError()));
            break;
        }
        
        response.append(&buffer[0], dwDownloaded);
    } while (dwSize > 0);

    LogHttpResponse(host, path, dwStatusCode, response);

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    
    LogInfo("HTTP GET Request completed, response length: " + std::to_string(response.length()));
    return response;
}

bool DownloadFile(const std::string& url, const std::string& localPath) {
    LogInfo("=== DownloadFile started ===");
    LogInfo("URL: " + url);
    LogInfo("Local path: " + localPath);
    
    std::string host, path;
    size_t protoEnd = url.find("://");
    if (protoEnd == std::string::npos) {
        LogError("Invalid URL format: no protocol found");
        return false;
    }
    
    std::string afterProto = url.substr(protoEnd + 3);
    size_t pathStart = afterProto.find('/');
    if (pathStart == std::string::npos) {
        LogError("Invalid URL format: no path found");
        return false;
    }
    
    host = afterProto.substr(0, pathStart);
    path = afterProto.substr(pathStart);
    
    LogInfo("Host: " + host);
    LogInfo("Path: " + path);

    HINTERNET hSession = WinHttpOpen(L"ZASCA-Updater/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, NULL, NULL, 0);
    if (!hSession) {
        LogError("WinHttpOpen failed: " + std::to_string(GetLastError()));
        return false;
    }

    std::wstring wHost = Utf8ToWide(host);
    bool isHttps = (url.find("https://") == 0);
    INTERNET_PORT port = isHttps ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT;
    
    HINTERNET hConnect = WinHttpConnect(hSession, wHost.c_str(), port, 0);
    if (!hConnect) {
        LogError("WinHttpConnect failed: " + std::to_string(GetLastError()));
        WinHttpCloseHandle(hSession);
        return false;
    }

    std::wstring wPath = Utf8ToWide(path);
    DWORD flags = isHttps ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", wPath.c_str(), NULL, NULL, NULL, flags);
    if (!hRequest) {
        LogError("WinHttpOpenRequest failed: " + std::to_string(GetLastError()));
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    BOOL bResults = WinHttpSendRequest(hRequest, NULL, 0, NULL, 0, 0, 0);
    if (!bResults) {
        LogError("WinHttpSendRequest failed: " + std::to_string(GetLastError()));
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    bResults = WinHttpReceiveResponse(hRequest, NULL);
    if (!bResults) {
        LogError("WinHttpReceiveResponse failed: " + std::to_string(GetLastError()));
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    DWORD dwStatusCode = 0;
    DWORD dwSize = sizeof(dwStatusCode);
    WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, NULL, &dwStatusCode, &dwSize, NULL);
    LogInfo("HTTP Status Code: " + std::to_string(dwStatusCode));
    
    if (dwStatusCode != 200) {
        LogError("HTTP request failed with status: " + std::to_string(dwStatusCode));
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    std::ofstream outFile(localPath, std::ios::binary);
    if (!outFile.is_open()) {
        LogError("Failed to create file: " + localPath);
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    DWORD totalDownloaded = 0;
    dwSize = 0;
    do {
        DWORD dwDownloaded = 0;
        if (!WinHttpQueryDataAvailable(hRequest, &dwSize)) {
            LogError("WinHttpQueryDataAvailable failed: " + std::to_string(GetLastError()));
            break;
        }
        if (dwSize == 0) break;
        
        std::vector<char> buffer(dwSize);
        if (!WinHttpReadData(hRequest, &buffer[0], dwSize, &dwDownloaded)) {
            LogError("WinHttpReadData failed: " + std::to_string(GetLastError()));
            break;
        }
        
        outFile.write(&buffer[0], dwDownloaded);
        totalDownloaded += dwDownloaded;
    } while (dwSize > 0);
    
    outFile.close();
    
    LogInfo("Total downloaded: " + std::to_string(totalDownloaded) + " bytes");

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    
    LogInfo("=== DownloadFile completed ===");
    return totalDownloaded > 0;
}

std::string ExtractFirstJsonObject(const std::string& json) {
    size_t start = json.find('{');
    if (start == std::string::npos) return "";
    
    int depth = 0;
    bool inString = false;
    bool escape = false;
    
    for (size_t i = start; i < json.length(); i++) {
        char c = json[i];
        
        if (escape) {
            escape = false;
            continue;
        }
        
        if (c == '\\' && inString) {
            escape = true;
            continue;
        }
        
        if (c == '"') {
            inString = !inString;
            continue;
        }
        
        if (!inString) {
            if (c == '{') depth++;
            else if (c == '}') {
                depth--;
                if (depth == 0) {
                    return json.substr(start, i - start + 1);
                }
            }
        }
    }
    
    return "";
}

std::string ParseJsonString(const std::string& json, const std::string& key) {
    LogDebug("Parsing JSON for key: " + key);
    
    std::string searchKey = "\"" + key + "\"";
    size_t keyPos = json.find(searchKey);
    if (keyPos == std::string::npos) {
        LogDebug("Key not found in JSON: " + key);
        LogJsonParse(json, key, "");
        return "";
    }
    LogDebug("Key found at position: " + std::to_string(keyPos));
    
    size_t colonPos = json.find(':', keyPos);
    if (colonPos == std::string::npos) {
        LogDebug("Colon not found after key: " + key);
        LogJsonParse(json, key, "");
        return "";
    }
    
    size_t quoteStart = json.find('"', colonPos);
    if (quoteStart == std::string::npos) {
        LogDebug("Quote not found after colon for key: " + key);
        LogJsonParse(json, key, "");
        return "";
    }
    
    std::string result;
    bool escape = false;
    for (size_t i = quoteStart + 1; i < json.length(); i++) {
        char c = json[i];
        if (escape) {
            switch (c) {
                case '"': result += '"'; break;
                case '\\': result += '\\'; break;
                case '/': result += '/'; break;
                case 'b': result += '\b'; break;
                case 'f': result += '\f'; break;
                case 'n': result += '\n'; break;
                case 'r': result += '\r'; break;
                case 't': result += '\t'; break;
                case 'u': {
                    if (i + 4 < json.length()) {
                        unsigned int codepoint = 0;
                        bool validHex = true;
                        for (int j = 0; j < 4; j++) {
                            codepoint <<= 4;
                            char hex = json[i + 1 + j];
                            if (hex >= '0' && hex <= '9') codepoint |= (hex - '0');
                            else if (hex >= 'a' && hex <= 'f') codepoint |= (hex - 'a' + 10);
                            else if (hex >= 'A' && hex <= 'F') codepoint |= (hex - 'A' + 10);
                            else { validHex = false; break; }
                        }
                        if (validHex) {
                            if (codepoint < 0x80) {
                                result += static_cast<char>(codepoint);
                            } else if (codepoint < 0x800) {
                                result += static_cast<char>(0xC0 | (codepoint >> 6));
                                result += static_cast<char>(0x80 | (codepoint & 0x3F));
                            } else {
                                result += static_cast<char>(0xE0 | (codepoint >> 12));
                                result += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
                                result += static_cast<char>(0x80 | (codepoint & 0x3F));
                            }
                            i += 4;
                        } else {
                            result += '?';
                        }
                    } else {
                        result += '?';
                    }
                    break;
                }
                default: result += c; break;
            }
            escape = false;
        } else if (c == '\\') {
            escape = true;
        } else if (c == '"') {
            LogJsonParse(json, key, result);
            LogDebug("Parsed value for key '" + key + "': " + result);
            return result;
        } else {
            result += c;
        }
    }
    
    LogJsonParse(json, key, result);
    LogDebug("Parsed value for key '" + key + "': " + result);
    return result;
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

void LogInit() {
    std::string exeDir = GetExeDirectory();
    g_LogPath = exeDir + "\\zasca-guard.log";
    
    std::ofstream logFile(g_LogPath, std::ios::app);
    if (logFile.is_open()) {
        time_t now = time(NULL);
        struct tm* timeinfo = localtime(&now);
        char timeBuf[64];
        strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M:%S", timeinfo);
        logFile << "\n========================================\n";
        logFile << "[" << timeBuf << "] [INIT] Log system initialized\n";
        logFile << "Log file: " << g_LogPath << "\n";
        logFile << "========================================\n";
        logFile.close();
    }
}

void LogWrite(const std::string& level, const std::string& message) {
    if (g_LogPath.empty()) {
        LogInit();
    }
    
    std::ofstream logFile(g_LogPath, std::ios::app);
    if (logFile.is_open()) {
        time_t now = time(NULL);
        struct tm* timeinfo = localtime(&now);
        char timeBuf[64];
        strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M:%S", timeinfo);
        logFile << "[" << timeBuf << "] [" << level << "] " << message << "\n";
        logFile.close();
    }
}

void LogInfo(const std::string& message) {
    LogWrite("INFO", message);
}

void LogError(const std::string& message) {
    LogWrite("ERROR", message);
}

void LogDebug(const std::string& message) {
    LogWrite("DEBUG", message);
}

void LogHttpResponse(const std::string& host, const std::string& path, DWORD statusCode, const std::string& response) {
    std::ostringstream oss;
    oss << "HTTP Response:\n";
    oss << "  Host: " << host << "\n";
    oss << "  Path: " << path << "\n";
    oss << "  Status Code: " << statusCode << "\n";
    oss << "  Response Length: " << response.length() << " bytes\n";
    if (response.length() > 0 && response.length() < 2000) {
        oss << "  Response Body:\n" << response << "\n";
    } else if (response.length() >= 2000) {
        oss << "  Response Body (first 1000 chars):\n" << response.substr(0, 1000) << "\n";
        oss << "  ... (truncated, total " << response.length() << " bytes)\n";
    }
    LogWrite("HTTP", oss.str());
}

void LogJsonParse(const std::string& json, const std::string& key, const std::string& result) {
    std::ostringstream oss;
    oss << "JSON Parse:\n";
    oss << "  Searching for key: " << key << "\n";
    oss << "  Result: " << (result.empty() ? "(empty/not found)" : result) << "\n";
    if (json.length() < 500) {
        oss << "  JSON content: " << json << "\n";
    } else {
        size_t keyPos = json.find("\"" + key + "\"");
        if (keyPos != std::string::npos) {
            size_t start = (keyPos > 100) ? keyPos - 100 : 0;
            size_t end = (keyPos + key.length() + 200 < json.length()) ? keyPos + key.length() + 200 : json.length();
            oss << "  JSON context around key: ..." << json.substr(start, end - start) << "...\n";
        }
    }
    LogWrite("JSON", oss.str());
}

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

bool SetDirectoryPermissionsAdminOnly(const std::string& dirPath) {
    PACL pOldDACL = NULL;
    PSECURITY_DESCRIPTOR pSD = NULL;
    
    DWORD result = GetNamedSecurityInfoA(dirPath.c_str(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, NULL, NULL, &pOldDACL, NULL, &pSD);
    if (result != ERROR_SUCCESS) {
        if (pSD) LocalFree(pSD);
        return false;
    }
    
    PSID pAdminSid = NULL;
    SID_IDENTIFIER_AUTHORITY SIDAuthNT = SECURITY_NT_AUTHORITY;
    if (!AllocateAndInitializeSid(&SIDAuthNT, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &pAdminSid)) {
        if (pSD) LocalFree(pSD);
        return false;
    }
    
    PSID pSystemSid = NULL;
    if (!AllocateAndInitializeSid(&SIDAuthNT, 1, SECURITY_LOCAL_SYSTEM_RID, 0, 0, 0, 0, 0, 0, 0, &pSystemSid)) {
        FreeSid(pAdminSid);
        if (pSD) LocalFree(pSD);
        return false;
    }
    
    EXPLICIT_ACCESSA ea[2];
    ZeroMemory(ea, sizeof(ea));
    
    ea[0].grfAccessPermissions = GENERIC_ALL;
    ea[0].grfAccessMode = SET_ACCESS;
    ea[0].grfInheritance = SUB_CONTAINERS_AND_OBJECTS_INHERIT;
    ea[0].Trustee.TrusteeForm = TRUSTEE_IS_SID;
    ea[0].Trustee.TrusteeType = TRUSTEE_IS_WELL_KNOWN_GROUP;
    ea[0].Trustee.ptstrName = (LPSTR)pAdminSid;
    
    ea[1].grfAccessPermissions = GENERIC_ALL;
    ea[1].grfAccessMode = SET_ACCESS;
    ea[1].grfInheritance = SUB_CONTAINERS_AND_OBJECTS_INHERIT;
    ea[1].Trustee.TrusteeForm = TRUSTEE_IS_SID;
    ea[1].Trustee.TrusteeType = TRUSTEE_IS_WELL_KNOWN_GROUP;
    ea[1].Trustee.ptstrName = (LPSTR)pSystemSid;
    
    PACL pNewDACL = NULL;
    result = SetEntriesInAclA(2, ea, NULL, &pNewDACL);
    
    FreeSid(pAdminSid);
    FreeSid(pSystemSid);
    
    if (result != ERROR_SUCCESS) {
        if (pSD) LocalFree(pSD);
        return false;
    }
    
    result = SetNamedSecurityInfoA((LPSTR)dirPath.c_str(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, NULL, NULL, pNewDACL, NULL);
    
    LocalFree(pNewDACL);
    if (pSD) LocalFree(pSD);
    
    return (result == ERROR_SUCCESS);
}

void AutoInitOnFirstRun() {
    if (!IsFirstRun()) {
        return;
    }
    
    ShowMessage("首次启动", "检测到首次运行，正在自动初始化...", MB_ICONINFORMATION);
    
    std::string exeDir = GetExeDirectory();
    
    if (!SetDirectoryPermissionsAdminOnly(exeDir)) {
        ShowMessage("警告", "设置目录权限时遇到问题，继续初始化...", MB_ICONWARNING);
    }
    
    if (RunCommand("where uv") != 0) {
        ShowMessage("提示", "未检测到 uv，正在通过国内代理自动安装...", MB_ICONINFORMATION);
        if (!InstallUv()) {
            ShowMessage("错误", "uv 安装失败。可能是 GitHub 代理失效，请尝试手动安装 uv。", MB_ICONERROR);
            return;
        }
        RefreshEnvironment();
    }
    
    ShowMessage("提示", "即将弹出黑框执行 uv sync...\n(Python解释器和依赖包均使用国内镜像加速)", MB_ICONINFORMATION);
    
    HANDLE hProcess = NULL;
    if (ExecuteCommand("uv sync", CREATE_NEW_CONSOLE, false, &hProcess)) {
        WaitForSingleObject(hProcess, INFINITE);
        DWORD exitCode;
        GetExitCodeProcess(hProcess, &exitCode);
        CloseHandle(hProcess);
        
        if (exitCode == 0) {
            if (MarkAsInitialized()) {
                ShowMessage("成功", "首次启动初始化完成！\n\n目录权限已设置为仅管理员可访问。", MB_ICONINFORMATION);
            } else {
                ShowMessage("成功", "首次启动初始化完成！\n\n但标记文件创建失败。", MB_ICONWARNING);
            }
        } else {
            ShowMessage("警告", "uv sync 执行异常，请查看弹出的黑框日志。", MB_ICONWARNING);
        }
    } else {
        ShowMessage("错误", "无法执行 uv sync。", MB_ICONERROR);
    }
}

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
        LogError("Destination directory does not exist: " + destPath);
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
            
            if (extractedCount >= itemCount) {
                extractionComplete = true;
                LogInfo("Extraction completed after " + std::to_string(i + 1) + " seconds");
                break;
            }
        }
        
        LogDebug("Waiting... (" + std::to_string(i + 1) + "s), extracted items: " + std::to_string(extractedCount));
    }
    
    if (!extractionComplete) {
        LogError("Extraction timeout after 60 seconds");
    }
    
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
    CreateDirectoryA(tempDir.c_str(), NULL);
    
    LogInfo("=== UpdateFromRelease started ===");
    LogInfo("Download URL: " + zipUrl);
    LogInfo("Temp ZIP file: " + tempZip);
    LogInfo("Temp directory: " + tempDir);
    
    ShowMessage("更新中", "正在下载最新版本...", MB_ICONINFORMATION);
    
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
    
    ShowMessage("更新中", "正在解压文件...", MB_ICONINFORMATION);
    
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
    
    ShowMessage("更新中", "正在复制文件...", MB_ICONINFORMATION);
    
    searchPath = extractedDir + "\\*";
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
    ShowMessage("检查更新", "正在检查最新版本...", MB_ICONINFORMATION);
    
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
    
    std::string msg = "发现最新版本: " + tagName + "\n\n是否立即更新？\n\n下载源: " + zipUrl;
    int result = MessageBoxW(NULL, Utf8ToWide(msg).c_str(), Utf8ToWide("发现更新").c_str(), MB_YESNO | MB_ICONQUESTION | MB_TOPMOST);
    
    if (result == IDYES) {
        LogInfo("User confirmed update, starting download...");
        if (UpdateFromRelease(zipUrl)) {
            LogInfo("Update completed successfully, restarting...");
            ShowMessage("成功", "更新完成！程序即将重启。", MB_ICONINFORMATION);
            
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
        ShowMessage("取消", "更新已取消。", MB_ICONINFORMATION);
    }
    LogInfo("=== HandleUpdate completed ===");
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
        ShowMessage("成功", "服务已启动并在后台守护运行。", MB_ICONINFORMATION);
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
        ShowMessage("成功", "服务已停止并卸载。", MB_ICONINFORMATION);
    } else { ShowMessage("提示", "服务未运行或未安装。", MB_ICONINFORMATION); }
    CloseServiceHandle(scm);
}

void HandleControlCommand(const std::string& action, const std::string& port) {
    if (action == "stop") { StopAndUninstallService(); return; }
    if (action == "restart") { StopAndUninstallService(); while (GetServiceState() != SERVICE_STOPPED) Sleep(200); InstallAndStartService(port); return; }
    
    if (action == "start") {
        DWORD state = GetServiceState();
        if (state == SERVICE_RUNNING) {
            std::string msg = "检测到服务已在运行。\n\n【是】：关闭现有服务并重新启动。\n【否】：忽略警告，单开一个黑框前台运行(用于多端口调试，不守护)。\n【取消】：放弃操作。";
            int res = ShowMessage("冲突警告", msg, MB_YESNOCANCEL | MB_ICONWARNING);
            if (res == IDYES) { StopAndUninstallService(); while (GetServiceState() != SERVICE_STOPPED) Sleep(200); InstallAndStartService(port); return; }
            else if (res == IDNO) { ExecuteCommand("uv run python manage.py runserver " + port, CREATE_NEW_CONSOLE, false); return; }
            else return;
        } else { InstallAndStartService(port); }
    }
}

void HandlePassthrough(const std::vector<std::string>& args) {
    std::string fullCmd = "uv run python manage.py";
    for (const auto& arg : args) fullCmd += " " + arg;
    if (!ExecuteCommand(fullCmd, CREATE_NEW_CONSOLE, false)) {
        ShowMessage("执行出错", "无法启动进程，请检查 uv 和 python 环境。", MB_ICONERROR);
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
        ShowMessage("检查更新", "正在检查最新版本...", MB_ICONINFORMATION);
        
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
                            ShowMessage("成功", "更新完成！", MB_ICONINFORMATION);
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
        ShowMessage("提示", "未检测到 uv，正在通过国内代理自动安装...", MB_ICONINFORMATION);
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
    ShowMessage("提示", "即将弹出黑框执行 uv sync...\n(Python解释器和依赖包均使用国内镜像加速)", MB_ICONINFORMATION);
    
    HANDLE hProcess = NULL;
    if (ExecuteCommand("uv sync", CREATE_NEW_CONSOLE, false, &hProcess)) {
        WaitForSingleObject(hProcess, INFINITE);
        DWORD exitCode; GetExitCodeProcess(hProcess, &exitCode);
        CloseHandle(hProcess);
        LogInfo("uv sync exit code: " + std::to_string(exitCode));
        if (exitCode == 0) {
            LogInfo("uv sync completed successfully");
            ShowMessage("成功", "环境初始化 完成。", MB_ICONINFORMATION);
        } else {
            LogError("uv sync failed with exit code: " + std::to_string(exitCode));
            ShowMessage("警告", "uv sync 执行异常，请查看弹出的黑框日志。", MB_ICONWARNING);
        }
    } else {
        LogError("Failed to execute uv sync");
        ShowMessage("错误", "无法执行 uv sync。", MB_ICONERROR);
    }
    LogInfo("=== HandleInit completed ===");
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
    SECURITY_ATTRIBUTES saAttr;
    saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
    saAttr.bInheritHandle = TRUE;
    saAttr.lpSecurityDescriptor = NULL;

    HANDLE hReadPipe = NULL, hWritePipe = NULL;
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

    PSECURITY_DESCRIPTOR pSD = NULL;
    SECURITY_ATTRIBUTES procSA = {0};
    if (isProtected) {
        pSD = CreateProtectedSD();
        if (pSD) {
            procSA.nLength = sizeof(SECURITY_ATTRIBUTES);
            procSA.lpSecurityDescriptor = pSD;
            procSA.bInheritHandle = FALSE;
        }
    }

    LPVOID envBlock = CreateChinaMirrorEnvBlock();
    char* cmdBuf = new char[cmd.length() + 1];
    strcpy_s(cmdBuf, cmd.length() + 1, cmd.c_str());

    LogInfo("Executing command: " + cmd);
    
    BOOL success = CreateProcessA(NULL, cmdBuf, isProtected ? &procSA : NULL, NULL, TRUE,
                                   CREATE_NO_WINDOW, envBlock, NULL, &si, &pi);

    delete[] cmdBuf;
    FreeEnvBlock(envBlock);
    CloseHandle(hWritePipe);
    if (pSD) LocalFree(pSD);

    if (!success) {
        LogError("Failed to create process, error: " + std::to_string(GetLastError()));
        CloseHandle(hReadPipe);
        return GetLastError();
    }

    CloseHandle(pi.hThread);

    char buffer[4096];
    DWORD bytesRead;
    while (ReadFile(hReadPipe, buffer, sizeof(buffer) - 1, &bytesRead, NULL) && bytesRead > 0) {
        buffer[bytesRead] = '\0';
        output += buffer;
    }
    CloseHandle(hReadPipe);

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exitCode = 0;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hProcess);

    LogInfo("Command exit code: " + std::to_string(exitCode));
    if (!output.empty()) {
        if (output.length() > 2000) {
            LogInfo("Command output (first 2000 chars):\n" + output.substr(0, 2000) + "\n... (truncated)");
        } else {
            LogInfo("Command output:\n" + output);
        }
    }

    return exitCode;
}

void RefreshEnvironment() { SendMessageTimeout(HWND_BROADCAST, WM_SETTINGCHANGE, 0, (LPARAM)"Environment", SMTO_ABORTIFHUNG, 5000, NULL); }

void WorkerThread() {
    if (RunCommand("where uv") != 0) {
        if (!InstallUv()) return;
        RefreshEnvironment();
    }
    if (RunCommand("uv sync", true) != 0) return;

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
