// MemoryzeUM.cpp : Defines the exported functions for the DLL application.
#include "stdafx.h"
#include "MemoryzeUM.h"
#include "SearchManager.h"

DEVICEIOCONTROL g_pfnDeviceIoControl = NULL;

// SearchManager creates synchronization events and map shared memory for data transfer, along with symbols necessary for search
boost::scoped_ptr<SearchManager> g_pSearchManager(new SearchManager(ALLOCATION));

BOOL SuspendOrResumeAllThreads(BOOL bShouldSuspend)
{
	THREADENTRY32 te32; 

	// create thread snapshot for all the threads in the system
	HANDLE hThreadSnap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0); 
	if(hThreadSnap == INVALID_HANDLE_VALUE) 
	{
		return FALSE;
	}

	// initialize thread entry structure and receive information about the first thread
	te32.dwSize = sizeof(THREADENTRY32 ); 
	if(!Thread32First(hThreadSnap, &te32)) 
	{
		CloseHandle(hThreadSnap);
		return FALSE;
	}

	// walk the list of threads for the current process
	DWORD dwPID = GetCurrentProcessId();
	do 
	{ 
		if(te32.th32OwnerProcessID == dwPID)
		{
			DWORD dwThreadID = GetCurrentThreadId();
			if(dwThreadID == te32.th32ThreadID)
			{
				// don't suspend this thread, or it will cause complete halt of the program:)
				continue;
			}

			HANDLE hThread = OpenThread(THREAD_SUSPEND_RESUME, FALSE, te32.th32ThreadID);
			if(hThread == NULL)
			{
				return FALSE;
			}

			if(bShouldSuspend)
			{
				SuspendThread(hThread);
			}
			else
			{
				ResumeThread(hThread);
			}
			CloseHandle(hThread);
		}
	} while(Thread32Next(hThreadSnap, &te32));

	CloseHandle(hThreadSnap);
	return TRUE;
}

PROC WINAPI CreateHook(PSTR pszTargetDLL, PSTR pszFunctionName, PROC pfnHook)
{
	// get current module (PE file)
	HMODULE hModule = GetModuleHandle(NULL);
	if(hModule == NULL)
	{
		return NULL;
	}

	// module start is also a DOS header start
	PIMAGE_DOS_HEADER pDosHeader = (PIMAGE_DOS_HEADER) hModule;

	// NT headers = start of module + lfanew field
	PIMAGE_NT_HEADERS pNTHeader = (PIMAGE_NT_HEADERS) ((DWORD_PTR) pDosHeader + pDosHeader->e_lfanew);

	// check if PE is valid (NT signature)
	if(pNTHeader->Signature != IMAGE_NT_SIGNATURE)
	{
		return NULL;
	}

	// get import table inside IMAGE_DIRECTORY
	IMAGE_DATA_DIRECTORY importTable = pNTHeader->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];

	// it is possible that no import table is present (for example, DLL can theoretically have only export table)
	if(importTable.VirtualAddress == 0)
	{
		return NULL;
	}

	// IMAGE_IMPORT_DESCRIPTOR struct defines one DLL - number of structure is equal to the number of DLLs
	PIMAGE_IMPORT_DESCRIPTOR pImportDescriptor = (PIMAGE_IMPORT_DESCRIPTOR) ((DWORD_PTR) pDosHeader + importTable.VirtualAddress);

	// first find target DLL which contains our target function
	// Name member is RVA!
	while (pImportDescriptor->Name)
	{
		PSTR pszCurrentDLLName = (PSTR) ((DWORD_PTR) pDosHeader + pImportDescriptor->Name);

		// target DLL found, break
		// traditional stricmp function is used
		if (!_stricmp(pszCurrentDLLName, pszTargetDLL)) 
		{
			break;
		}

		// check next module descriptor
		pImportDescriptor++;
	}

	// obtain address of original (target) function
	// in theory we SHOULD NOT call GetProcAddress within DLL_PROCESS_ATTACH because of possible DLL dependency "hell"
	// however, this call does not seem to cause any problems so far, so I'll leave it for now
	FARPROC pfnOrig = GetProcAddress(GetModuleHandleA(pszTargetDLL), pszFunctionName);
	if(pfnOrig == NULL)
	{
		return NULL;
	}

	// get IAT address
	PIMAGE_THUNK_DATA pIATThunk = (PIMAGE_THUNK_DATA) ((DWORD_PTR) pDosHeader + pImportDescriptor->FirstThunk);

	// go through the entire IAT - all functions imported from the target DLL
	// end of the table is marked by 0
	while(pIATThunk->u1.Function)
	{
		// if we found our function...
		if ((DWORD_PTR)pIATThunk->u1.Function == (DWORD_PTR) pfnOrig)
		{
			// change memory permissions - it is interesting that this is not always necessary, but do it anyway for Memoryze
			MEMORY_BASIC_INFORMATION IATmemPage;
			VirtualQuery(pIATThunk, &IATmemPage, sizeof(MEMORY_BASIC_INFORMATION));

			// mark as READWRITE in order to write our hook
			if(!VirtualProtect(IATmemPage.BaseAddress, IATmemPage.RegionSize, PAGE_READWRITE, &IATmemPage.Protect))
			{
				return NULL;
			}
				
			// replace it with hook function
			// theoretically, problems can always occur when hooking - another thread might be calling this piece of code
			// when the assignment is being performed
			// solve this problem by first suspending all threads and resuming the execution after hooking
			// however, if this function fails, some of the threads might remain suspended
			//SuspendOrResumeAllThreads(TRUE);

			pIATThunk->u1.Function = (DWORD_PTR) pfnHook; 
			
			//SuspendOrResumeAllThreads(FALSE);

			// re-protect the pages as READONLY
			DWORD dwDummy = 0;
			if(!VirtualProtect(IATmemPage.BaseAddress, IATmemPage.RegionSize, IATmemPage.Protect, &dwDummy))
			{
				return NULL;
			}
					
			return pfnOrig;
		}

		pIATThunk++;
	}

	return NULL;
}

BOOL WINAPI DeviceIoControlHook(HANDLE hHandle, DWORD dwIoControlCode, LPVOID lpInBuffer, DWORD nInBufferSize, 
								LPVOID lpOutBuffer, DWORD nOutBufferSize, LPDWORD lpBytesReturned, LPOVERLAPPED lpOverlapped)
{
	BOOL bRetVal = g_pfnDeviceIoControl(hHandle, dwIoControlCode, lpInBuffer, nInBufferSize, lpOutBuffer, nOutBufferSize, lpBytesReturned, lpOverlapped);

	// dwIoControlCode for MmMapIoSpace method - this is the final call to the driver
	if(dwIoControlCode == 0x70f7800c)
	{
		// this function is in-lined, so performance hit should be almost invisible
		// for example, in case of 512 MB of memory, this comparison will be made 131k times
		// measurements show that this slows down the whole acquiring process by 1-2 seconds
		if(!g_pSearchManager->AreSymbolsInitialized())
		{
			// if symbols could not be obtained, default values should be used
			if(!g_pSearchManager->InitSymbols())
			{
				g_pSearchManager->UseDefaultSymbols();
			}
		}

		PBYTE pBuffer = (PBYTE) lpOutBuffer;
		g_pSearchManager->Search(pBuffer, nOutBufferSize, *((PDWORD_PTR) lpInBuffer));
	}

	return bRetVal;
}