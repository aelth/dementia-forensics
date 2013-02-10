#pragma once
#include "../Common/WinternalStructures.h"
#include "../Common/Singleton.h"

// is singleton a good idea?
class DumpOverwriter : public Singleton<DumpOverwriter>
{
public:
	bool WriteDump(DWORD_PTR dwOffset, PBYTE pBuffer, DWORD dwBufSize, const std::vector<std::pair<DWORD_PTR, DWORD_PTR>> &changesList);
	bool WriteDump(DWORD_PTR dwWriteOffset, PBYTE pNewBuffer, DWORD dwBufSize);

private:
	HANDLE m_dumpFileHandle;

	friend class Singleton<DumpOverwriter>;
	
	// typedefs necessary for obtaining the list of open handles and a file name
	typedef NTSTATUS (WINAPI *NTQSI)(DWORD, PVOID, DWORD, PDWORD);
	typedef NTSTATUS (WINAPI *NTQO)(HANDLE, OBJECT_INFORMATION_CLASS, PVOID, DWORD, PDWORD);

	HANDLE GetDumpFileHandle(void);
	TCharString GetObjectName(HANDLE hObject, OBJECT_INFORMATION_CLASS objInfoClass);
	bool EndsWith(const TCharString &str, const TCharString &end);

	// this is singleton pattern by Meyers -  hide constructor, copy constructor,
	// destructor and assignment operator
	DumpOverwriter(void);
	DumpOverwriter(DumpOverwriter const &);
	DumpOverwriter &operator=(DumpOverwriter const &);
	~DumpOverwriter(void);
};
