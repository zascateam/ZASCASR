#include "logger.h"
#include "utils.h"
#include <fstream>
#include <ctime>
#include <sstream>

static std::string g_LogPath;

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
