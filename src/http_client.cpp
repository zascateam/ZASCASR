#include "http_client.h"
#include "logger.h"
#include "utils.h"
#include <winhttp.h>
#include <fstream>
#include <vector>

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
