#include "WinVerProvider.h"

// static OS version structure - all members will be initialized to 0
static RTL_OSVERSIONINFOEXW versionInfoEx;

VOID WinpGetWinVersion(VOID);

ULONG WinGetMajorVersion(VOID)
{
	// check if version has not yet been obtained
	if(versionInfoEx.dwMajorVersion == 0 && versionInfoEx.dwMinorVersion == 0)
	{
		WinpGetWinVersion();
	}
	
	return versionInfoEx.dwMajorVersion;
}

ULONG WinGetMinorVersion(VOID)
{
	// check if version has not yet been obtained
	if(versionInfoEx.dwMajorVersion == 0 && versionInfoEx.dwMinorVersion == 0)
	{
		WinpGetWinVersion();
	}

	return versionInfoEx.dwMinorVersion;
}

VOID WinpGetWinVersion(VOID)
{
	versionInfoEx.dwOSVersionInfoSize = sizeof(RTL_OSVERSIONINFOEXW);
	RtlGetVersion((RTL_OSVERSIONINFOW *) &versionInfoEx);
}