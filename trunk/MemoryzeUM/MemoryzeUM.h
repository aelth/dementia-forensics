#pragma once

#ifdef __cplusplus 
extern "C" 
{ 
#endif // __cplusplus  
#ifdef _MEMORYZEUM_EXPORTS
#define MEMORYZEUM_API __declspec(dllexport)  
#else 
#define MEMORYZEUM_API __declspec(dllimport) 
#endif // _MEMORYZEUM_EXPORTS

	MEMORYZEUM_API BOOL TODO(void);

#undef MEMORYZEUM_API
#ifdef __cplusplus 
} 
#endif // __cplusplus

// global variables
typedef BOOL(WINAPI *DEVICEIOCONTROL)(HANDLE, DWORD, LPVOID, DWORD, LPVOID, DWORD, LPDWORD, LPOVERLAPPED);
extern DEVICEIOCONTROL g_pfnDeviceIoControl;

VOID InitSearchManager(VOID);
PROC WINAPI CreateHook(PSTR pszTargetDLL, PSTR pszFunctionName, PROC pfnHook);
BOOL WINAPI DeviceIoControlHook(HANDLE hHandle, DWORD dwIoControlCode, LPVOID lpInBuffer, DWORD nInBufferSize, 
								LPVOID lpOutBuffer, DWORD nOutBufferSize, LPDWORD lpBytesReturned, LPOVERLAPPED lpOverlapped);