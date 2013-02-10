#pragma once

typedef struct _SHARED_BLOCK
{
	TCHAR szInfoMessage[512];
	DWORD_PTR dwAddress;
	BYTE bCapturedBuffer[4096];
	DWORD dwHighlightOffset;
	DWORD dwHighlightLength;
} SHARED_BLOCK, *PSHARED_BLOCK;

typedef struct _TARGET_PROCESS_BLOCK
{
	// only 15 characters are actually used, but OK
	// explicitly using CHARs, in order to avoid transformations
	CHAR szProcessName[128];
} TARGET_PROCESS_BLOCK, *PTARGET_PROCESS_BLOCK;

template<class T>
class SharedMemory
{
public:
	SharedMemory(TCharString szReadEventName, TCharString szWriteEventName, TCharString szMappingName);
	~SharedMemory(void);

	T Read(VOID);
	bool Write(T *pSharedBlock);
private:
	HANDLE m_hReadEvent;
	TCharString m_szReadEventName;
	HANDLE m_hWriteEvent;
	TCharString m_szWriteEventName;
	HANDLE m_hSharedMemory;
	TCharString m_szSharedMemoryName;
	T *m_pSharedMemoryBlock;

	bool CreateSyncEvent(TCharString szEventName, HANDLE &hEvent, bool bIsSignaled);
	bool CreateMappingAndMapView(TCharString szMappingName, DWORD dwMappingSize);

};

template<class T>
SharedMemory<T>::SharedMemory(TCharString szReadEventName, TCharString szWriteEventName, TCharString szMappingName)
{
	m_szReadEventName = szReadEventName;
	m_szWriteEventName = szWriteEventName;
	m_szSharedMemoryName = szMappingName;

	// create all necessary events
	m_hReadEvent = m_hWriteEvent = NULL;
	if(	!CreateSyncEvent(szReadEventName, m_hReadEvent, true) || !CreateSyncEvent(szWriteEventName, m_hWriteEvent, false))
	{
		// should not be using cout, but what the heck...
		tcout << _T("Error while creating sync events...") << std::endl;
		exit(EXIT_FAILURE);
	}

	// create shared memory and map view
	m_pSharedMemoryBlock = NULL;
	m_hSharedMemory = NULL;
	if(!CreateMappingAndMapView(m_szSharedMemoryName, sizeof(T)))
	{
		// should not be using cout, but what the heck...
		tcout << _T("Error while creating shared memory mapping...") << std::endl;
		exit(EXIT_FAILURE);
	}
}
template<class T>
SharedMemory<T>::~SharedMemory()
{
	// close all handles to events and shared memory
	if(m_hReadEvent != NULL)
	{
		CloseHandle(m_hReadEvent);
	}
	if(m_hWriteEvent != NULL)
	{
		CloseHandle(m_hWriteEvent);
	}
	if(m_pSharedMemoryBlock != NULL)
	{
		UnmapViewOfFile(m_pSharedMemoryBlock);
	}
	if(m_hSharedMemory != NULL)
	{
		CloseHandle(m_hSharedMemory);
	}
}


template<class T>
T SharedMemory<T>::Read(VOID)
{
	T sharedBlock;
	memset(&sharedBlock, 0, sizeof(sharedBlock));

	// make sure all writing operations are finished before reading
	// wait infinitely until this event is signaled
	DWORD dwWaitResult = WaitForSingleObject(m_hWriteEvent, INFINITE);
	if (dwWaitResult == WAIT_OBJECT_0)
	{
		// read the shared memory
		memcpy_s(&sharedBlock, sizeof(T), m_pSharedMemoryBlock, sizeof(T));

		// signal read event so writer can continue to write
		SetEvent(m_hReadEvent);
	}

	return sharedBlock;
}

template<class T>
bool SharedMemory<T>::Write(T *pSharedBlock)
{
	bool bRet = false;

	// make sure all read operations are finished before reading
	DWORD dwWaitResult = WaitForSingleObject(m_hReadEvent, INFINITE);
	if (dwWaitResult == WAIT_OBJECT_0)
	{
		memcpy_s(m_pSharedMemoryBlock, sizeof(T), pSharedBlock, sizeof(T));
		// signal write event so reader can continue to read
		SetEvent(m_hWriteEvent);
		bRet = true;
	}

	return bRet;
}

template<class T>
bool SharedMemory<T>::CreateSyncEvent(TCharString szEventName, HANDLE &hEvent, bool bIsSignaled)
{
	hEvent = CreateEvent(	NULL,             // default security -- should change in the future, since anyone can open/signal this event now
							FALSE,            // auto reset the event
							bIsSignaled,      // is event signaled?
							szEventName.c_str());

	if (hEvent == NULL)
	{
		return false;
	}

	return true;
}

template<class T>
bool SharedMemory<T>::CreateMappingAndMapView(TCharString szMappingName, DWORD dwMappingSize)
{
	m_hSharedMemory = CreateFileMapping(INVALID_HANDLE_VALUE,    // mapping is backed by system paging file
										NULL,                    // default security descriptor -- similar to event creation, this should change in the future
										PAGE_READWRITE,          // read/write access to shared memory
										0,                       // maximum size - high bytes 
										dwMappingSize,			 // size of the mapping
										szMappingName.c_str());  // name of mapping object

	if (m_hSharedMemory == NULL)
	{
		return false;
	}

	m_pSharedMemoryBlock = (T *) MapViewOfFile(	m_hSharedMemory,		// handle to map object
															FILE_MAP_ALL_ACCESS,	// read/write access
															0,						// zero offset
															0,						// zero offset
															dwMappingSize);           

	if (m_pSharedMemoryBlock == NULL) 
	{ 
		return false;
	}

	return true;
}
