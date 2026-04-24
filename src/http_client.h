#pragma once

#include <string>

std::string HttpGet(const std::string& host, const std::string& path);
bool DownloadFile(const std::string& url, const std::string& localPath);
