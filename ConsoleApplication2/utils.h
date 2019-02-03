#pragma once
#include "pch.h"
#include <iostream>
#include <filesystem>
#include <Windows.h>
#include <atlconv.h>
#include <atlbase.h>
#include <tlhelp32.h>
#include <vector>


int getProcessIdByName(HANDLE hProcessSnap, std::wstring processName);
void getTidsByPid(HANDLE hProcessSnap, DWORD pid, std::vector<DWORD>& tids);

void print_error(std::wstring error_msg);