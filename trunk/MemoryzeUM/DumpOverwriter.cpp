#include "StdAfx.h"
#include "DumpOverwriter.h"

DumpOverwriter::DumpOverwriter(void)
{
	// initial value of the handle is NULL
	m_dumpFileHandle = NULL;
}

DumpOverwriter::~DumpOverwriter(void)
{
	// intentionally left empty
}

bool DumpOverwriter::WriteDump(DWORD_PTR dwOffset, PBYTE pBuffer, DWORD dwBufSize, const std::vector<std::pair<DWORD_PTR, DWORD_PTR>> &changesList)
{
	// if this is the first time that the function is being called...
	if(m_dumpFileHandle == NULL)
	{
		// obtain file handle
		m_dumpFileHandle = GetDumpFileHandle();

		// if the call fails, notify the caller
		if(m_dumpFileHandle == NULL)
		{
			OutputDebugString(TEXT("Cannot obtain dump file handle..."));
			return false;
		}		
	}

	// get current file pointer
	LARGE_INTEGER liFileOffset;
	liFileOffset.QuadPart = 0; 
	if(!SetFilePointerEx(m_dumpFileHandle, liFileOffset, &liFileOffset, FILE_CURRENT))
	{
		OutputDebugString(TEXT("Cannot get current file position"));
		return false;
	}

	std::vector<std::pair<DWORD_PTR, DWORD_PTR>>::const_iterator iter;

	// if current position is equal to the object being modified, write modified object into a current block!
#ifdef _WIN64
	// use quad part
	if(pBuffer != NULL && liFileOffset.QuadPart == dwOffset)
#else // _WIN32
	if(pBuffer != NULL && liFileOffset.LowPart == dwOffset)
#endif // _WIN64
	{
		for(iter = changesList.begin(); iter != changesList.end(); ++iter)
		{
			DWORD_PTR dwAddressOfChange = (*iter).first;
			DWORD_PTR dwNewValue = (*iter).second;
			DWORD_PTR dwWriteOffset = dwAddressOfChange - dwOffset;
			*((PDWORD_PTR)(pBuffer + dwWriteOffset)) = dwNewValue;
		}
	}
	else
	{
		for(iter = changesList.begin(); iter != changesList.end(); ++iter)
		{
			DWORD_PTR dwAddressOfChange = (*iter).first;
			DWORD_PTR dwNewValue = (*iter).second;

			LARGE_INTEGER liNewFileOffset;
#ifdef _WIN64
			liNewFileOffset.QuadPart = dwAddressOfChange;
#else // _WIN32
			liNewFileOffset.LowPart = dwAddressOfChange;
			liNewFileOffset.HighPart = 0;
#endif // _WIN64
			// move file pointer to desired address
			if(!SetFilePointerEx(m_dumpFileHandle, liNewFileOffset, NULL, FILE_BEGIN))
			{
				OutputDebugString(TEXT("File pointer change unsuccessful, won't change object"));
				return false;
			}

			// write new buffer
			DWORD dwBytesWritten = 0;
			if(!WriteFile(m_dumpFileHandle, &dwNewValue, sizeof(DWORD_PTR), &dwBytesWritten, NULL))
			{
				OutputDebugString(TEXT("Writing to file was unsuccessful, moving file pointer..."));
				SetFilePointerEx(m_dumpFileHandle, liFileOffset, NULL, FILE_BEGIN);
				return false;
			}
		}
		
		// move file pointer to the old position
		SetFilePointerEx(m_dumpFileHandle, liFileOffset, NULL, FILE_BEGIN);
	}

	return true;
}

bool DumpOverwriter::WriteDump(DWORD_PTR dwWriteOffset, PBYTE pNewBuffer, DWORD dwBufSize)
{
	// if this is the first time that the function is being called...
	if(m_dumpFileHandle == NULL)
	{
		// obtain file handle
		m_dumpFileHandle = GetDumpFileHandle();

		// if the call fails, notify the caller
		if(m_dumpFileHandle == NULL)
		{
			OutputDebugString(TEXT("Cannot obtain dump file handle..."));
			return false;
		}		
	}

	// get current file pointer
	LARGE_INTEGER liFileOffset;
	liFileOffset.QuadPart = 0; 
	if(!SetFilePointerEx(m_dumpFileHandle, liFileOffset, &liFileOffset, FILE_CURRENT))
	{
		OutputDebugString(TEXT("Cannot get current file position"));
		return false;
	}

	LARGE_INTEGER liNewFileOffset;
#ifdef _WIN64
	liNewFileOffset.QuadPart = dwWriteOffset;
#else // _WIN32
	liNewFileOffset.LowPart = dwWriteOffset;
	liNewFileOffset.HighPart = 0;
#endif // _WIN64
	// move file pointer to desired address
	if(!SetFilePointerEx(m_dumpFileHandle, liNewFileOffset, NULL, FILE_BEGIN))
	{
		OutputDebugString(TEXT("File pointer change unsuccessful, won't change object"));
		return false;
	}

	// write new buffer
	DWORD dwBytesWritten = 0;

	if(pNewBuffer == NULL)
	{
		DWORD dwNullByte = 0;
		if(!WriteFile(m_dumpFileHandle, &dwNullByte, dwBufSize, &dwBytesWritten, NULL))
		{
			OutputDebugString(TEXT("Writing to file was unsuccessful, moving file pointer..."));
			SetFilePointerEx(m_dumpFileHandle, liFileOffset, NULL, FILE_BEGIN);
			return false;
		}
	}
	else
	{
		if(!WriteFile(m_dumpFileHandle, pNewBuffer, dwBufSize, &dwBytesWritten, NULL))
		{
			OutputDebugString(TEXT("Writing to file was unsuccessful, moving file pointer..."));
			SetFilePointerEx(m_dumpFileHandle, liFileOffset, NULL, FILE_BEGIN);
			return false;
		}
	}
		
	// move file pointer to the old position
	SetFilePointerEx(m_dumpFileHandle, liFileOffset, NULL, FILE_BEGIN);

	return true;
}

HANDLE DumpOverwriter::GetDumpFileHandle(void)
{
	// if name was already retrieved, return the "cached" HANDLE
	if(m_dumpFileHandle != NULL)
	{
		return m_dumpFileHandle;
	}

	HANDLE hFile = NULL;
	NTQSI pNTQSI = (NTQSI) GetProcAddress(GetModuleHandle(_T("NTDLL.DLL")), "NtQuerySystemInformation");

	if(pNTQSI == NULL)
	{
		OutputDebugString(TEXT("NtQuerySystemInformation address not found!"));
		return hFile;
	}

	// obtain information for all handles on the system
	DWORD dwSize = sizeof(SYSTEM_HANDLE_INFORMATION);
	PSYSTEM_HANDLE_INFORMATION pHandleInfo = (PSYSTEM_HANDLE_INFORMATION) new BYTE[dwSize];
	DWORD ntReturn = pNTQSI(SystemHandleInformation, pHandleInfo, dwSize, &dwSize);

	// size probably won't match, so allocate enough memory this time (returned by the first call in dwSize)
	// make the buffer a bit bigger then returned because size can change between several calls
	while(ntReturn == STATUS_INFO_LENGTH_MISMATCH)
	{
		delete[] pHandleInfo;
		dwSize += 0x1000;
		pHandleInfo = (PSYSTEM_HANDLE_INFORMATION) new BYTE[dwSize];
		ntReturn = pNTQSI(SystemHandleInformation, pHandleInfo, dwSize, &dwSize);
	}

	if(ntReturn != STATUS_SUCCESS)
	{
		OutputDebugString(TEXT("NtQuerySystemInformation failure when obtaining SystemHandleInformation!"));
		return hFile;
	}

	DWORD_PTR dwMemoryzePID = GetCurrentProcessId();

	// iterate through all obtained handles...
	for(DWORD dwIdx = 0; dwIdx < pHandleInfo->uCount; dwIdx++)
	{
		// ... interesting are the ones for this process
		if(pHandleInfo->Handles[dwIdx].uIdProcess == dwMemoryzePID)
		{
			hFile = (HANDLE) pHandleInfo->Handles[dwIdx].Handle;
			TCharString handleName = GetObjectName(hFile, ObjectNameInformation);
			// interested in *.img file only -- regexes could be used, but this is much simpler
			if(EndsWith(handleName, _T(".img")))
			{
				// img file found - return handle
				m_dumpFileHandle = hFile;
				return m_dumpFileHandle;
			}
		}
	}

	hFile = NULL;
	return hFile;
}

TCharString DumpOverwriter::GetObjectName(HANDLE hObject, OBJECT_INFORMATION_CLASS objInfoClass)
{
	LPWSTR lpwsReturn = NULL;
	TCharString retString = _T("");

	NTQO pNTQO = (NTQO) GetProcAddress(GetModuleHandle(_T("NTDLL.DLL")), "NtQueryObject");

	if(pNTQO == NULL)
	{
		OutputDebugString(TEXT("NtQueryObject address not found!"));
		return retString;
	}

	DWORD dwSize = sizeof(OBJECT_NAME_INFORMATION);
	POBJECT_NAME_INFORMATION pObjectInfo = (POBJECT_NAME_INFORMATION) new BYTE[dwSize];
	NTSTATUS ntReturn = pNTQO(hObject, objInfoClass, pObjectInfo, dwSize, &dwSize);

	// same as for the NtQuerySystemInformation function -- length will probably be wrong
	while(ntReturn == STATUS_BUFFER_OVERFLOW || ntReturn == STATUS_INFO_LENGTH_MISMATCH)
	{
		delete[] pObjectInfo;
		dwSize += 0x500;
		pObjectInfo = (POBJECT_NAME_INFORMATION) new BYTE[dwSize];
		ntReturn = pNTQO(hObject, objInfoClass, pObjectInfo, dwSize, &dwSize);
	}

	if((ntReturn >= STATUS_SUCCESS) && (pObjectInfo->Buffer != NULL))
	{
		DWORD dwSize = pObjectInfo->Length + sizeof(WCHAR);
		lpwsReturn = (LPWSTR) new BYTE[dwSize];
		ZeroMemory(lpwsReturn, pObjectInfo->Length + sizeof(WCHAR));
		memcpy_s(lpwsReturn, dwSize, pObjectInfo->Buffer, pObjectInfo->Length);
		retString.assign(lpwsReturn);
	}

	delete[] pObjectInfo;
	return retString;
}

// taken from http://stackoverflow.com/questions/874134/find-if-string-endswith-another-string-in-c
bool DumpOverwriter::EndsWith(const TCharString &str, const TCharString &end)
{
	if (str.length() >= end.length()) {
		return (0 == str.compare (str.length() - end.length(), end.length(), end));
	} else {
		return false;
	}
}