#include "StdAfx.h"
#include "TCPEndpoint.h"

TCPEndpoint::TCPEndpoint(const DWORD dwPoolHeaderSize)
{
	// initialize object name and tag
	m_szObjName = "TCP_ENDPOINT";
	m_dwTag = 0x45706354;
	m_dwPoolHeaderSize = dwPoolHeaderSize;

	// initialize default offset
	SetDefaultOffsetsAndSizes();
}

TCPEndpoint::~TCPEndpoint(void)
{
}

void TCPEndpoint::SetDefaultOffsetsAndSizes( void )
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
		m_isPID = false;

		// Vista/2008 have different offsets than Windows 7
		if(verInfo.dwMinorVersion == 0)
		{
#ifdef _WIN64
			m_dwConnectionOwnerOffset = 0x210;
#else // _WIN32
			m_dwConnectionOwnerOffset = 0x160;
#endif // _WIN64
		}
		else
		{
			// using Windows 7 x86 defaults
#ifdef _WIN64
			m_dwConnectionOwnerOffset = 0x238;
#else // _WIN32
			m_dwConnectionOwnerOffset = 0x174;
#endif // _WIN64
			
		}
	}
}