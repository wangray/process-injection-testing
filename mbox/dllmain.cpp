// dllmain.cpp : Defines the entry point for the DLL application.
#include"stdafx.h"
#include <Windows.h>

BOOL APIENTRY DllMain( HMODULE hModule,
                       DWORD  ul_reason_for_call,
                       LPVOID lpReserved
                     )
{
	MessageBox(NULL, TEXT("Injected DLL!"), TEXT("Injected DLL!"), MB_OKCANCEL);
}

