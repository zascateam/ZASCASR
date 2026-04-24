#pragma once

#include <string>

std::string ExtractFirstJsonObject(const std::string& json);
std::string ParseJsonString(const std::string& json, const std::string& key);
