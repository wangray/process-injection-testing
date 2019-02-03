#include "pch.h"
#include <iostream>
#include <filesystem>
#include <Windows.h>
#include <atlconv.h>
#include <atlbase.h>
#include <tlhelp32.h>
#include <vector>

using namespace std;

void getTidsByPid(HANDLE hProcessSnap, DWORD pid, vector<DWORD>& tids) {
	THREADENTRY32 te = { sizeof(te) };
	Thread32First(hProcessSnap, &te);
	do {
		if (te.th32OwnerProcessID == pid) {
			tids.push_back(te.th32ThreadID);
		}
	} while (Thread32Next(hProcessSnap, &te));
}

int getProcessIdByName(HANDLE hProcessSnap, std::wstring processName) {

	PROCESSENTRY32 pe32;
	pe32.dwSize = sizeof(PROCESSENTRY32);

	if (!Process32First(hProcessSnap, &pe32))
	{
		cout << "Could not snapshot" << endl;
		CloseHandle(hProcessSnap);          // clean the snapshot object
		return -1;
	}

	// Walk process list, print some info 

	bool foundProcess = false;

	do {
		//wprintf(TEXT("processName: %s\n"), pe32.szExeFile);

		if (std::wstring(pe32.szExeFile).find(processName) != string::npos) {
			wprintf(TEXT("Found process %s\n"), pe32.szExeFile);
			foundProcess = true;
			break;
		}
	} while (Process32Next(hProcessSnap, &pe32));

	// Now, inject into DLL
	if (!foundProcess) {
		cout << "Could not find process" << endl;
		return -1;
	}

	auto pid = pe32.th32ProcessID;
	return pid;
}

void print_error(std::wstring error_msg) {
	LPTSTR messageBuffer;

	FormatMessage(
		FORMAT_MESSAGE_ALLOCATE_BUFFER |
		FORMAT_MESSAGE_FROM_SYSTEM |
		FORMAT_MESSAGE_IGNORE_INSERTS,
		NULL,
		GetLastError(),
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		(LPTSTR)&messageBuffer,
		0, NULL);

	wprintf(L"%s failed: %s\n", error_msg.c_str(), (LPTSTR)messageBuffer);
}
