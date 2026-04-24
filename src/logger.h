#pragma once

#include <string>
#include <windows.h>

void LogInit();
void LogWrite(const std::string& level, const std::string& message);
void LogInfo(const std::string& message);
void LogError(const std::string& message);
void LogDebug(const std::string& message);
void LogHttpResponse(const std::string& host, const std::string& path, DWORD statusCode, const std::string& response);
void LogJsonParse(const std::string& json, const std::string& key, const std::string& result);
