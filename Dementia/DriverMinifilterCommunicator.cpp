#include "StdAfx.h"
#include "DriverMinifilterCommunicator.h"
#include "Logger.h"
#include "Utils.h"
#include "../Common/DementiaFSCommon.h"

DriverMinifilterCommunicator::DriverMinifilterCommunicator(void)
: IDriverCommunicator()
{
	m_driverPort = NULL;
}

DriverMinifilterCommunicator::~DriverMinifilterCommunicator(void)
{
	RemoveDriver();
}

bool DriverMinifilterCommunicator::InstallDriver(void)
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
	Logger::Instance().Log(_T("Running as administrator -- connecting to driver port"), SUCCESS);

	// connect to communication port
	HRESULT hResult = FilterConnectCommunicationPort(	PORT_NAME,						// port name
														0,								// options must be zero (documentation)
														NULL,							// don't pass context to connect routine
														0,								// size of context
														NULL,							// don't inherit this handle
														&m_driverPort					// handle to communication port
													 );

	if (IS_ERROR( hResult ))
	{
		Logger::Instance().Log(_T("Cannot connect to driver port"), CRITICAL_ERROR);
		return false;
	}

	m_isInstalled = true;
	return true;
}

bool DriverMinifilterCommunicator::RemoveDriver(void)
{
	// check if driver is installed in the first place
	if(!m_isInstalled)
	{
		return true;
	}

	// close the port
	if(m_driverPort != INVALID_HANDLE_VALUE)
	{
		CloseHandle(m_driverPort);
	}

	return true;
}

bool DriverMinifilterCommunicator::StartHiding(const TargetObjectList &targetObjects)
{
	// do nothing if the driver is not installed
	if(!m_isInstalled)
	{
		return false;
	}

	if(targetObjects.empty())
	{
		Logger::Instance().Log(_T("Objects to be hidden are not defined... Not sending information to driver"), WARNING);
		return false;
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

	// format new message (be cautious when setting the required size)
	ULONG uCommandBlockSize = FIELD_OFFSET(COMMAND_MESSAGE, pData[dwArraySize]);
	PCOMMAND_MESSAGE pCommandMessage = (PCOMMAND_MESSAGE) VirtualAlloc(0, uCommandBlockSize, MEM_COMMIT, PAGE_READWRITE);
	if(pCommandMessage == NULL)
	{
		Logger::Instance().Log(_T("Failed to allocate memory for port command message block"), ERR);
		return false;
	}

	pCommandMessage->Command = DmfsStartHiding;
	memcpy_s(pCommandMessage->pData, uCommandBlockSize - FIELD_OFFSET(COMMAND_MESSAGE, pData), pTargetObjectArray, dwArraySize);

	// send symbols to driver
	DWORD dwBytesReturned = 0;
	if(IS_ERROR(FilterSendMessage(	m_driverPort,					// communication port
									pCommandMessage,				// command message
									uCommandBlockSize,				// size of command message
									NULL,							// no output array
									0,								// no output array size
									&dwBytesReturned				// actual size of the array (in bytes)
								 )))
	{
		Logger::Instance().Log(_T("Error starting hiding engine"), ERR);
		VirtualFree(pTargetObjectArray, 0, MEM_RELEASE);
		return false;
	}

	Logger::Instance().Log(_T("Hiding engine started!"), SUCCESS);
	VirtualFree(pTargetObjectArray, 0, MEM_RELEASE);
	VirtualFree(pCommandMessage, 0, MEM_RELEASE);

	// TEST ONLY!
	Sleep(900000);

	return true;
}

SymbolList DriverMinifilterCommunicator::GetDriverSymbols(void)
{
	SymbolList symbolList;

	// allocate space for 100 symbols -- will increase it if necessary
	DWORD dwArraySize = sizeof(INTERNAL_SYMBOL) * 100;

	COMMAND_MESSAGE command;

	command.Command = DmfsGetSymbols;

	// initialize array which will hold private symbols
	PINTERNAL_SYMBOL pSymbolsArray = (PINTERNAL_SYMBOL) VirtualAlloc(0, dwArraySize, MEM_COMMIT, PAGE_READWRITE);
	if(pSymbolsArray == NULL)
	{
		Logger::Instance().Log(_T("Failed to allocate memory for driver symbols"), ERR);
		return symbolList;
	}

	// get symbol array from the driver
	DWORD dwReturnedArraySize = 0;
	if(IS_ERROR(FilterSendMessage(	m_driverPort,				// communication port
									&command,					// command message specifying the action
									sizeof(COMMAND_MESSAGE),	// size of the message
									pSymbolsArray,				// output array that will hold the symbols from the driver
									dwArraySize,				// size of the output array
									&dwReturnedArraySize		// actual size of the array (in bytes)
								  )))
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

bool DriverMinifilterCommunicator::SendSymbolsToDriver(const SymbolList &symbolList)
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

	// format new message (be cautious when setting the required size)
	ULONG uCommandBlockSize = FIELD_OFFSET(COMMAND_MESSAGE, pData[dwArraySize]);
	PCOMMAND_MESSAGE pCommandMessage = (PCOMMAND_MESSAGE) VirtualAlloc(0, uCommandBlockSize, MEM_COMMIT, PAGE_READWRITE);
	if(pCommandMessage == NULL)
	{
		Logger::Instance().Log(_T("Failed to allocate memory for port command message block"), ERR);
		return false;
	}

	pCommandMessage->Command = DmfsSaveSymbols;
	memcpy_s(pCommandMessage->pData, uCommandBlockSize - FIELD_OFFSET(COMMAND_MESSAGE, pData), pSymbolsArray, dwArraySize);

	// send symbols to driver
	DWORD dwBytesReturned = 0;
	if(IS_ERROR(FilterSendMessage(	m_driverPort,					// communication port
									pCommandMessage,				// command message
									uCommandBlockSize,				// size of command message
									NULL,							// no output array
									0,								// no output array size
									&dwBytesReturned				// actual size of the array (in bytes)
									)))
	{
		Logger::Instance().Log(_T("Error sending symbols to driver"), ERR);
		VirtualFree(pSymbolsArray, 0, MEM_RELEASE);
		return false;
	}

	Logger::Instance().Log(_T("Symbols successfully sent to kernel-mode!"), SUCCESS);
	VirtualFree(pSymbolsArray, 0, MEM_RELEASE);
	VirtualFree(pCommandMessage, 0, MEM_RELEASE);

	return true;
}