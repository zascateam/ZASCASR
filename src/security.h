#pragma once

#include <string>
#include <windows.h>

PSECURITY_DESCRIPTOR CreateProtectedSD();
bool SetDirectoryPermissionsAdminOnly(const std::string& dirPath);
bool ResetDirectoryPermissionsToInherited(const std::string& dirPath);
