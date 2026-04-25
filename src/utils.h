#pragma once

#include <string>
#include <windows.h>

std::wstring Utf8ToWide(const std::string& str);
bool IsNumber(const std::string& s);
std::string GetTempFilePath();
std::string GetExeDirectory();
bool CopyDirectoryRecursive(const std::string& srcPath, const std::string& destPath);
bool RemoveDirectoryRecursive(const std::string& dirPath);
int ShowMessage(const std::string& title, const std::string& msg, UINT type = MB_OK);
void InitConsole();
void ConsolePrint(const std::string& msg);
void PrintInfo(const std::string& msg);
void PrintSuccess(const std::string& msg);
void PrintWarning(const std::string& msg);
void PrintError(const std::string& msg);
void PrintProgress(const std::string& label, int current, int total);
void PrintProgressBar(const std::string& label, int percent);
