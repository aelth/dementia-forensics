#include "StdAfx.h"
#include "Injector.h"
#include "Logger.h"

Injector::Injector(TCHAR *pszDLLFullPath)
{
	if(pszDLLFullPath == NULL)
	{
		Logger::Instance().Log(_T("DLL path is NULL -- check the code"), CRITICAL_ERROR);
		exit(EXIT_FAILURE);
	}

	m_szDLLFullPath.reset(new TCHAR[MAX_PATH + 1]);
	_tcsncpy_s(m_szDLLFullPath.get(), MAX_PATH + 1, pszDLLFullPath, _tcslen(pszDLLFullPath));
}

Injector::~Injector(void)
{
	// intentionally left empty
}

bool Injector::InjectToProcess(DWORD dwProcessID)
{
	if(dwProcessID == 0 || dwProcessID == 4)
	{
		Logger::Instance().Log(_T("Cannot inject in System or Idle processes, try again"), ERR);
		return false;
	}

	// this is the main reason why we needed SeDebugPrivilege and Administrator
	HANDLE hProcess = OpenProcess(PROCESS_VM_WRITE | PROCESS_VM_OPERATION | PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, dwProcessID);
	if(hProcess == NULL)
	{
		Logger::Instance().Log(_T("Cannot open process with PID ") + boost::lexical_cast<TCharString>(dwProcessID) + _T(" for thread injection"), ERR);
		return false;
	}

	// allocate memory necessary to write DLL name in target process address space
	PVOID pAddress = VirtualAllocEx(hProcess, NULL, MAX_PATH + 1, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
	if(pAddress == NULL)
	{
		Logger::Instance().Log(_T("Allocation of memory within the process with PID ") + boost::lexical_cast<TCharString>(dwProcessID) + _T(" failed"), ERR);
		return false;
	}

	// write full DLL path in target process address space
	SIZE_T nSize = 0;
	if(!WriteProcessMemory(hProcess, pAddress, (PVOID) m_szDLLFullPath.get(), MAX_PATH + 1, &nSize))
	{
		Logger::Instance().Log(_T("Writing DLL name within the memory of the process with PID ") + boost::lexical_cast<TCharString>(dwProcessID) + _T(" failed"), ERR);
		return false;
	}

	HMODULE hModule = GetModuleHandle(_T("kernel32"));
	// explicitly using wide-char version!
	FARPROC pLoadLibrary = GetProcAddress(hModule, "LoadLibraryW");

	CreateRemoteThread(hProcess, NULL, 0, (PTHREAD_START_ROUTINE) pLoadLibrary,(PTCHAR) pAddress, 0, NULL);

	return true;

}