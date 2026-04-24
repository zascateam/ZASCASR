#pragma once

#include <string>
#include <windows.h>

LPVOID CreateChinaMirrorEnvBlock();
void FreeEnvBlock(LPVOID env);
BOOL ExecuteCommand(const std::string& cmd, DWORD flags, bool isProtected, LPHANDLE hProcessOut = NULL);
DWORD RunCommand(const std::string& cmd, bool isProtected = false);
DWORD RunCommandWithOutput(const std::string& cmd, std::string& output, bool isProtected = false);
void RefreshEnvironment();
