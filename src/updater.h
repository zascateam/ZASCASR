#pragma once

#include <string>

bool ExtractZip(const std::string& zipPath, const std::string& destPath);
bool UpdateFromRelease(const std::string& zipUrl);
void HandleUpdate();
