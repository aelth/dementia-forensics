#include "StdAfx.h"
#include "UDPEndpoint.h"

UDPEndpoint::UDPEndpoint(const DWORD dwPoolHeaderSize)
{
	// initialize object name and tag
	m_szObjName = "UDP_ENDPOINT";
	m_dwTag = 0x41706455;
	m_dwPoolHeaderSize = dwPoolHeaderSize;

	// initialize default offset
	SetDefaultOffsetsAndSizes();
}

UDPEndpoint::~UDPEndpoint(void)
{
}

void UDPEndpoint::SetDefaultOffsetsAndSizes(void)
{
	// first obtain OS version
	OSVERSIONINFO verInfo;
	ZeroMemory(&verInfo, sizeof(OSVERSIONINFO));
	verInfo.dwOSVersionInfoSize = sizeof(OSVERSIONINFO);

	GetVersionEx(&verInfo);

	// THE CODE THAT FOLLOWS DOES NOT COVER ALL CASES
	// FIX ME!!!
	if(verInfo.dwMajorVersion == 5)
	{
		// using Windows XP x86
		m_isPID = true;
		m_dwConnectionOwnerOffset = 0x148;
	}
	else if(verInfo.dwMajorVersion == 6)
	{
		m_isPID = false;
		// using Windows 7 x86 defaults
		m_dwConnectionOwnerOffset = 0x18;
	}
}