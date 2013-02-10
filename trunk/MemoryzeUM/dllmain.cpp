// dllmain.cpp : Defines the entry point for the DLL application.
#include "stdafx.h"
#include "MemoryzeUM.h"

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
	switch (ul_reason_for_call)
	{
		case DLL_PROCESS_ATTACH:
			// notice the type of the parameters! Explicitly no UNICODE is used, because all IAT (INT actually) names are
			// strictly ASCII single byte representation. LoadLibrary code also requires ASCII strings
			g_pfnDeviceIoControl = (DEVICEIOCONTROL) CreateHook("kernel32.dll", "DeviceIoControl", (PROC) DeviceIoControlHook);
			break;
		case DLL_THREAD_ATTACH:
		case DLL_THREAD_DETACH:
		case DLL_PROCESS_DETACH:
			break;
	}
	return TRUE;
}

