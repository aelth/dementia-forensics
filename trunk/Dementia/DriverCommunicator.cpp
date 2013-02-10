#include "StdAfx.h"
#include "DriverCommunicator.h"
#include "Logger.h"
#include "Utils.h"
#include "../Common/IOCTLCodes.h"

DriverCommunicator::DriverCommunicator(const TCharString &driverName, const TCharString &driverPath, const TCharString &displayName, bool shouldUnloadDriver)
	: IDriverCommunicator(), m_driverPath(driverPath), m_displayName(displayName) 
{	
	// should unload the driver after it has finished hiding?
	m_unloadDriver = shouldUnloadDriver;

	// SCM handles
	m_service = NULL;
	m_serviceManager = NULL;

	// driver handle
	m_driver = NULL;

	// real driver name has the following format: \\.\DeviceName
	m_driverName = _T("\\\\.\\") + driverName;

	// event handle
	m_hidingFinishedEvent = NULL;
}

DriverCommunicator::~DriverCommunicator(void)
{	
	// check if driver should be unloaded
	if(m_unloadDriver)
	{
		// no driver will be left hanging
		// theoretically, this could crash the system, but do it anyway
		RemoveDriver();
	}
	
	// else, don't do nothing - driver will be left "hanging" and working in kernel mode
	// it must be stopped and removed manually
}

bool DriverCommunicator::InstallDriver(void)
{
	// don't install the driver if it already exists
	if(m_isInstalled)
	{
		return true;
	}

	// check if current user is administrator
	Logger::Instance().Log(_T("Check if running under administrator context..."), INFO);
	if(!Utils::IsAdmin())
	{
		Logger::Instance().Log(_T("Not admin - try to run the program as administrator"), CRITICAL_ERROR);
		return false;
	}
	Logger::Instance().Log(_T("Running as administrator"), SUCCESS);

	// open the service manager
	m_serviceManager = OpenSCManager(	NULL,						// connect to SCM on this machine 
										NULL,						// open SERVICES_ACTIVE_DATABASE
										SC_MANAGER_CREATE_SERVICE	// creation of new service is the desired access
									);
	if (m_serviceManager == NULL)
	{
		Logger::Instance().Log(_T("Error while opening SCM database - check your rights please"), CRITICAL_ERROR);
		return false;
	}

	TCHAR szFullDriverPath[MAX_PATH];
	if(!GetFullPathName(m_driverPath.c_str(), MAX_PATH - 1, szFullDriverPath, NULL))
	{
		Logger::Instance().Log(_T("Error while obtaining full path of the specified driver \"") + m_driverName + _T("\" - check your rights please"), CRITICAL_ERROR);
		return false;
	}

	// create new service
	m_service = CreateService(	m_serviceManager,					// handle to SCM
								m_displayName.c_str(),				// service name
								m_displayName.c_str(),				// display of the service
								SERVICE_ALL_ACCESS,					// get all access to the service
								SERVICE_KERNEL_DRIVER,				// driver service
								SERVICE_DEMAND_START,				// service will be started when StartService() is called
								SERVICE_ERROR_IGNORE,				// do not log any errors when starting service				
								szFullDriverPath,					// path to driver service
								NULL,								// ignore load order group
								NULL,								// not changing the existing tags
								NULL,								// no dependencies
								NULL,								// driver should use the default name specified by the I/O system
								NULL								// drivers don't have passwords
							 );

	// check if service (and thus the driver) already exists somehow
	// this should not happen, since this check was made at the beginning of this function
	if (GetLastError() == ERROR_SERVICE_EXISTS)
	{
		// just open the handle to the service
		m_service = OpenService(m_serviceManager, m_driverName.c_str(), SERVICE_ALL_ACCESS);
	}

	// if service creation failed, exit
	if (m_service == NULL)
	{
		Logger::Instance().Log(_T("Cannot create service/driver \"") + m_driverName + _T("\"!"), CRITICAL_ERROR);
		return false;
	}

	// start the driver
	if (!StartService(m_service, 0, NULL))
	{
		// start service failed -- maybe somehow the driver/service is already running
		if (GetLastError() != ERROR_SERVICE_ALREADY_RUNNING)
		{
			// no it's not -- exit with failure
			Logger::Instance().Log(_T("Cannot start service/driver \"") + m_driverName + _T("\"!"), CRITICAL_ERROR);
			return false;
		}
	}

	// finally obtain the handle to newly created device
	m_driver = CreateFile(	m_driverName.c_str(),					// name of the driver we want to communicate with
							GENERIC_READ | GENERIC_WRITE,			// access rights, we want generic read/write permissions
							FILE_SHARE_READ | FILE_SHARE_WRITE,		// share the handle because multiple threads could be communicating with the driver
							NULL,									// default security descriptor - real driver access security is contained within the driver
							OPEN_EXISTING,							// try to open existing file, fail if it doesn't exist
							FILE_ATTRIBUTE_NORMAL,					// use normal file attributes
							NULL									// don't use template file
						  );

	if(m_driver == INVALID_HANDLE_VALUE)
	{
		// driver/device handle is not valid
		Logger::Instance().Log(_T("Cannot obtain handle of the driver driver \"") + m_driverName + _T("\"!"), CRITICAL_ERROR);
		return false;
	}

	m_isInstalled = true;
	return true;
}

bool DriverCommunicator::RemoveDriver(void)
{
	// check if driver is installed in the first place
	if(!m_isInstalled)
	{
		return true;
	}

	// first close the opened device handle
	if(m_driver != NULL)
	{
		CloseHandle(m_driver);
	}

	// if handles got somehow messed up, try to open SCM and service again
	if(m_serviceManager == NULL)
	{
		// open the service manager
		m_serviceManager = OpenSCManager(NULL, NULL, SC_MANAGER_CREATE_SERVICE);
		if(m_serviceManager == NULL)
		{
			Logger::Instance().Log(_T("Error while opening SCM database - check your rights please"), CRITICAL_ERROR);
			return false;
		}
	}

	if(m_service == NULL)
	{
		// just open the handle to the service
		m_service = OpenService(m_serviceManager, m_driverName.c_str(), SERVICE_ALL_ACCESS);
		// if service creation failed, exit
		if (m_service == NULL)
		{
			Logger::Instance().Log(_T("Cannot open service/driver \"") + m_driverName + _T("\"!"), CRITICAL_ERROR);
			return false;
		}
	}
	
	SERVICE_STATUS serviceStatus;
	BOOL bStatus = FALSE;
	// stop the service
	bStatus = ControlService(	m_service,					// service handle
								SERVICE_CONTROL_STOP,		// notify the driver that it should stop
								&serviceStatus				// current service status -- not used
							 );

	if(!bStatus)
	{
		Logger::Instance().Log(_T("Service \"") + m_driverName + _T("\" not stopped, but will continue..."), WARNING);
	}

	if(!DeleteService(m_service))
	{
		Logger::Instance().Log(_T("Service \"") + m_driverName + _T("\" not deleted, but will continue..."), WARNING);
	}

	// close the handles
	CloseServiceHandle(m_service);
	CloseServiceHandle(m_serviceManager);

	Logger::Instance().Log(_T("Driver successfully removed"), SUCCESS);

	m_isInstalled = false;
	return true;
}

bool DriverCommunicator::StartHiding(const TargetObjectList &targetObjects)
{
	if(targetObjects.empty())
	{
		Logger::Instance().Log(_T("Objects to be hidden are not defined... Not sending information to driver"), WARNING);
		return false;
	}

	bool unloadManually = false;
	// first check whether the driver will be unloaded immediately after hiding of objects has finished (i.e. when acquisition app exits)
	if(m_unloadDriver)
	{
		if(!SendSynchronizationEventToDriver())
		{
			Logger::Instance().Log(_T("Synchronization event not initialized - driver will stay loaded after application exit and must be unloaded manually"), WARNING);
			unloadManually = true;
		}
	}

	// allocate space for all objects to be hidden
	DWORD dwArraySize = sizeof(TARGET_OBJECT) * targetObjects.size();

	// initialize array which will hold all objects for hiding
	PTARGET_OBJECT pTargetObjectArray = (PTARGET_OBJECT) VirtualAlloc(0, dwArraySize, MEM_COMMIT, PAGE_READWRITE);
	if(pTargetObjectArray == NULL)
	{
		Logger::Instance().Log(_T("Failed to allocate memory for array of objects to be hidden"), ERR);
		return false;
	}

	// "linearize" all objects to be hidden to array
	DWORD dwObjIndex = 0;
	TargetObjectList::const_iterator iter;
	for(iter = targetObjects.begin(); iter != targetObjects.end(); ++iter)
	{
		TargetObjectPtr targetObject = *iter;
		pTargetObjectArray[dwObjIndex] = targetObject->LinearizeObject();
		dwObjIndex++;
	}

	// send symbols to driver
	DWORD dwBytesReturned = 0;
	if(!DeviceIoControl(m_driver,						// driver handle -- should be initialized after InstallDriver()
						IOCTL_DEMENTIAKM_START_HIDING,	// IOCTL to start hiding
						pTargetObjectArray,				// hide object array
						dwArraySize,					// hide object array size
						NULL,							// no output array
						0,								// no output array size
						&dwBytesReturned,				// actual size of the array (in bytes)
						NULL							// overlapped structure is not used
		))
	{
		Logger::Instance().Log(_T("Error starting hiding engine"), ERR);
		VirtualFree(pTargetObjectArray, 0, MEM_RELEASE);
		return false;
	}

	Logger::Instance().Log(_T("Hiding engine started!"), SUCCESS);
	VirtualFree(pTargetObjectArray, 0, MEM_RELEASE);
	

	// if driver should be unloaded and event was successfully created and passed to the driver
	if(m_unloadDriver && !unloadManually)
	{
		// wait for exit
		DWORD dwStatus = WaitForSingleObject(m_hidingFinishedEvent, INFINITE);
		if(dwStatus == WAIT_OBJECT_0)
		{
			Logger::Instance().Log(_T("Event received - hiding finished! Unloading the driver..."), SUCCESS);
		}
		else
		{
			Logger::Instance().Log(_T("Failure while waiting for the driver event. Driver will be unloaded..."), ERR);
		}
	}

	// else the driver won't be unloaded and the application exits immediately!
	return true;
}

SymbolList DriverCommunicator::GetDriverSymbols(void)
{
	SymbolList symbolList;

	// allocate space for 100 symbols -- will increase it if necessary
	DWORD dwArraySize = sizeof(INTERNAL_SYMBOL) * 100;
	
	// initialize array which will hold private symbols
	PINTERNAL_SYMBOL pSymbolsArray = (PINTERNAL_SYMBOL) VirtualAlloc(0, dwArraySize, MEM_COMMIT, PAGE_READWRITE);
	if(pSymbolsArray == NULL)
	{
		Logger::Instance().Log(_T("Failed to allocate memory for driver symbols"), ERR);
		return symbolList;
	}

	// get symbol array from the driver
	DWORD dwReturnedArraySize = 0;
	if(!DeviceIoControl(m_driver,					// driver handle -- should be initialized after InstallDriver()
						IOCTL_DEMENTIAKM_GET_SYMS,	// IOCTL to get symbols from the driver
						NULL,						// no input buffer
						0,							// no input buffer length
						(LPVOID) pSymbolsArray,		// output buffer will contain the list of symbols necessary for driver's internal operations
						dwArraySize,				// initial size of the array (in bytes)
						&dwReturnedArraySize,		// actual size of the array (in bytes)
						NULL						// overlapped structure is not used
						))
	{
		Logger::Instance().Log(_T("Error getting symbols from the driver"), ERR);
		VirtualFree(pSymbolsArray, 0, MEM_RELEASE);
		return symbolList;
	}

	// get number of the symbols returned from the driver
	m_driverSymbolCount = dwReturnedArraySize / sizeof(INTERNAL_SYMBOL);
	
	// put symbols into symbol list which will be passed to SymbolHelper
	TransformFromArrayToSymbolList(symbolList, pSymbolsArray, m_driverSymbolCount);

	// release allocated memory
	VirtualFree(pSymbolsArray, 0, MEM_RELEASE);

	return symbolList;
}

bool DriverCommunicator::SendSymbolsToDriver(const SymbolList &symbolList)
{
	// allocate symbol array which will be passed to the driver
	// count of the symbols is checked inside the driver and must be equal to the number of symbols obtained from the driver
	DWORD dwArraySize = m_driverSymbolCount * sizeof(INTERNAL_SYMBOL);
	PINTERNAL_SYMBOL pSymbolsArray = (PINTERNAL_SYMBOL) VirtualAlloc(0, dwArraySize, MEM_COMMIT, PAGE_READWRITE);
	if(pSymbolsArray == NULL)
	{
		Logger::Instance().Log(_T("Failed to allocate memory for driver symbols"), ERR);
		return false;
	}

	DWORD symIndex = 0;
	symIndex = TransformFromSymbolListToArray(symbolList, pSymbolsArray, symIndex, _T(""));

	// simple error checking - number of transformed items should be the same as the initial array size
	if(m_driverSymbolCount != symIndex)
	{
		Logger::Instance().Log(_T("Number of transformed driver symbols not equal to number of symbols received from the driver!"), WARNING);
		// do not exit, this is probably a non-fatal error
	}

	// send symbols to driver
	DWORD dwBytesReturned = 0;
	if(!DeviceIoControl(m_driver,						// driver handle -- should be initialized after InstallDriver()
						IOCTL_DEMENTIAKM_STORE_SYMS,	// IOCTL to get symbols from the driver
						pSymbolsArray,					// symbol array is an input buffer
						dwArraySize,					// it is num_of_symbols * sizeof(SYMBOL) big
						NULL,							// no output array
						0,								// no output array size
						&dwBytesReturned,				// actual size of the array (in bytes)
						NULL							// overlapped structure is not used
						))
	{
		Logger::Instance().Log(_T("Error sending symbols to driver"), ERR);
		VirtualFree(pSymbolsArray, 0, MEM_RELEASE);
		return false;
	}

	Logger::Instance().Log(_T("Symbols successfully sent to kernel-mode!"), SUCCESS);
	VirtualFree(pSymbolsArray, 0, MEM_RELEASE);
	return true;
}

bool DriverCommunicator::SendSynchronizationEventToDriver(void)
{
	// create synchronization event which will be signaled when the driver has finished hiding
	// CreateEventEx requires OS above Vista, so use plain CreateEvent instead
	m_hidingFinishedEvent = CreateEvent(	NULL,						// use default security attributes
											TRUE,						// manual reset event (this is irrelevant in this case)
											FALSE,						// event is not signaled initially	
											NULL						// event is not named because it will be passed to the driver directly
										);
	if(m_hidingFinishedEvent == NULL)
	{
		Logger::Instance().Log(_T("Cannot create synchronization event"), ERR);
		return false;
	}

	// send created event to driver
	DWORD dwBytesReturned = 0;
	if(!DeviceIoControl(m_driver,								// driver handle -- should be initialized after InstallDriver()
						IOCTL_DEMENTIAKM_STORE_FINISH_EVENT,	// IOCTL to get symbols from the driver
						&m_hidingFinishedEvent,					// event handle is the input buffer
						sizeof(HANDLE),							// size of handle
						NULL,									// no output array
						0,										// no output array size
						&dwBytesReturned,						// not needed
						NULL									// overlapped structure is not used
						))
	{
		Logger::Instance().Log(_T("Error sending synchronization event to driver"), ERR);
		return false;
	}

	Logger::Instance().Log(_T("Synchronization event successfully sent to kernel-mode!"), SUCCESS);
	return true;
}