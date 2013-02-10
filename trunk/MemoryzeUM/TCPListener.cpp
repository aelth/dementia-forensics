#include "StdAfx.h"
#include "TCPListener.h"

TCPListener::TCPListener(const DWORD dwPoolHeaderSize)
{
	// initialize object name and tag
	m_szObjName = "TCP_LISTENER";
	m_dwTag = 0x4c706354;
	m_dwPoolHeaderSize = dwPoolHeaderSize;

	// initialize default offset
	SetDefaultOffsetsAndSizes();
}

TCPListener::~TCPListener(void)
{
	// intentionally left empty
}

void TCPListener::SetDefaultOffsetsAndSizes(void)
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
		m_dwConnectionOwnerOffset = 0x18;
	}
	else if(verInfo.dwMajorVersion == 6)
	{
		// using Windows 7 x86 defaults
		m_isPID = false;
		m_dwConnectionOwnerOffset = 0x18;
	}
}