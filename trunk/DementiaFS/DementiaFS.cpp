///////////////////////////////////////////////////////////////////////////////
///
/// Copyright (c) 2012 - <company name here>
///
/// Original filename: DementiaFS.cpp
/// Project          : DementiaFS
/// Date of creation : 2012-12-19
/// Author(s)        :	Influenced by Didier Stevens ARIAD - http://blog.didierstevens.com/programs/ariad/,
///						Capture project (https://projects.honeynet.org/svn/capture-hpc/capture-hpc/tags/2.5/capture-client/KernelDrivers/CaptureKernelDrivers/FileMonitor/CaptureFileMonitor.c),
///						and MiniSpy example driver from Microsoft
///
/// Purpose          : <description>
///
/// Revisions:
///  0000 [2012-12-19] Initial revision.
///
///////////////////////////////////////////////////////////////////////////////

// $Id$

#ifdef __cplusplus
extern "C" {
#endif
#include <fltKernel.h>
#include <dontuse.h>
#include <suppress.h>
#include "DementiaFS.h"
#include "../Common/DementiaFSCommon.h"
#include "../Common/CommonTypesDrv.h"
#include "FileMonitor.h"
#include "../DementiaKM/SymbolEngine.h"

#ifdef __cplusplus
}; // extern "C"
#endif

#ifdef __cplusplus
namespace { // anonymous namespace to limit the scope of this global variable!
#endif

PDRIVER_OBJECT pdoGlobalDrvObj = NULL;

typedef struct _DEMENTIA_FS_DATA
{
	PDRIVER_OBJECT pDriverObject;
	PFLT_FILTER pFilter;
	PFLT_PORT pServerPort;
	PFLT_PORT pClientPort;
	BOOLEAN bReady;
} DEMENTIA_FS_DATA, *PDEMENTIA_FS_DATA;

DEMENTIA_FS_DATA globalData;

#ifdef __cplusplus
}; // anonymous namespace
#endif

LONG ExceptionFilter (IN PEXCEPTION_POINTERS ExceptionPointer, IN BOOLEAN AccessingUserBuffer)
{
	NTSTATUS Status;

	Status = ExceptionPointer->ExceptionRecord->ExceptionCode;

	// pass to higher exception handler if not accessing user buffer or the status is not expected
	if (!FsRtlIsNtstatusExpected(Status) && !AccessingUserBuffer) {

			return EXCEPTION_CONTINUE_SEARCH;
	}

	return EXCEPTION_EXECUTE_HANDLER;
}

NTSTATUS PortConnectCallback(IN PFLT_PORT ClientPort, IN PVOID ServerPortCookie, PVOID ConnectionContext, IN ULONG SizeOfContext, PVOID *ConnectionCookie)
{
	// accept the connection and save the client port
	globalData.pClientPort = ClientPort;
	return STATUS_SUCCESS;
}

VOID PortDisconnectCallback(IN OPTIONAL PVOID ConnectionCookie)
{
	// just close the connection
	FltCloseClientPort(globalData.pFilter, &globalData.pClientPort);
}

NTSTATUS PortMessageCallback(IN PVOID ConnectionCookie, IN PVOID InputBuffer, IN ULONG InputBufferSize, OUT PVOID OutputBuffer, IN ULONG OutputBufferSize, OUT PULONG ReturnOutputBufferLength)
{
	NTSTATUS status = STATUS_SUCCESS;

	DEMENTIAFS_COMMAND command;

	// minifilter does not need to do probing of Input/Output buffer because FM already did that
	// the filter manager is NOT doing any alignment checking on the pointers - must do that in the code
	// the minifilter MUST continue to use a try/except around any access to these buffers
	if(InputBuffer != NULL) 
	{
		__try  
		{
			// probe and capture the buffer - it's in the user mode so protect it with exception handler
			command = ((PCOMMAND_MESSAGE) InputBuffer)->Command;
		}
		__except (ExceptionFilter( GetExceptionInformation(), TRUE)) 
		{
			return GetExceptionCode();
		}

		ULONG uSizeOfHeader = FIELD_OFFSET(COMMAND_MESSAGE, pData);

		switch (command) {
			case DmfsGetSymbols:
				// using brackets for variable declaration/definition
				{
					KdPrint(("[DEBUG] Creating list of symbols...\n"));

					if(OutputBuffer == NULL)
					{
						KdPrint(("[DEBUG] ERROR - Cannot get symbols because the output buffer is NULL\n"));
						status = STATUS_INVALID_PARAMETER;
						break;
					}

					ULONG uArrayLen = 0;
					PINTERNAL_SYMBOL pSymbolsArray = SymGetSymbols(&uArrayLen);

					if(pSymbolsArray == NULL)
					{
						KdPrint(("[DEBUG] ERROR - Error while creating array of symbols\n"));
						status = STATUS_MEMORY_NOT_ALLOCATED;
						break;
					}

					ULONG uOutputSize = uArrayLen * sizeof(INTERNAL_SYMBOL);
					if(OutputBufferSize < uOutputSize)
					{
						KdPrint(("[DEBUG] ERROR - Output buffer size (%d) is less than required size (%d) \n", OutputBufferSize, uOutputSize));

						// release the memory!
						ExFreePoolWithTag(pSymbolsArray, TAG_INTERNAL_SYMBOL_ARRAY);

						status = STATUS_INVALID_BUFFER_SIZE;
						break;
					}

					// copy the symbol array to the buffer
					__try
					{
						RtlCopyMemory(OutputBuffer, pSymbolsArray, uOutputSize);
					}
					__except(ExceptionFilter(GetExceptionInformation(), TRUE)) 
					{
						return GetExceptionCode();
					}
					
					// caller must release the memory
					ExFreePoolWithTag(pSymbolsArray, TAG_INTERNAL_SYMBOL_ARRAY);

					*ReturnOutputBufferLength = uOutputSize;
					status = STATUS_SUCCESS;
				}
				break;
			case DmfsSaveSymbols:
				// using brackets for variable declaration/definition
				{
					KdPrint(("[DEBUG] Storing symbol addresses received from the user-mode...\n"));

					ULONG uRequiredInputSize = sizeof(INTERNAL_SYMBOL) * SymGetSymbolCount();
					ULONG uArraySize = InputBufferSize - uSizeOfHeader;

					// input buffer must be an array of symbol_count INTERNAL_SYMBOL structures
					// output buffer must be 0
					
					if(uArraySize != uRequiredInputSize || OutputBufferSize != 0)
					{
						KdPrint(("[DEBUG] ERROR - Wrong input or output buffer size - input: %d (must be %d); output: %d (must be 0)\n", InputBufferSize, uRequiredInputSize, OutputBufferSize));
						status = STATUS_INVALID_BUFFER_SIZE;
						break;
					}

					__try
					{
						// copy input buffer to our array - this will make sure that the buffer isn't changed in user mode while it is being used in the kernel mode
						PINTERNAL_SYMBOL pSymbolsArray = (PINTERNAL_SYMBOL) ExAllocatePoolWithTag(NonPagedPool, uArraySize, TAG_INTERNAL_SYMBOL_ARRAY);
						RtlCopyMemory(pSymbolsArray, (((PCOMMAND_MESSAGE) InputBuffer)->pData), uArraySize);

						// one very important thing to note - SymbolEngine is not responsible for checking validity of received symbols!
						// SymbolEngine is just a storage place for symbols -- users of symbols are responsible for checking the validity of the used symbols
						status = SymAddSymbols(pSymbolsArray, uArraySize);

						ExFreePoolWithTag(pSymbolsArray, TAG_INTERNAL_SYMBOL_ARRAY);

						if(!NT_SUCCESS(status))
						{
							break;
						}
					}
					__except(ExceptionFilter(GetExceptionInformation(), TRUE )) 
					{
						return GetExceptionCode();
					}
				}
				break;
			case DmfsStartHiding:
				// using brackets for variable declaration/definition
				{
					KdPrint(("[DEBUG] Starting hiding modules...\n"));

					ULONG uTargetObjArraySize = InputBufferSize - uSizeOfHeader;

					// input buffer must be an array of TARGET_OBJECT structures, at least one structure
					if(uTargetObjArraySize == 0 || uTargetObjArraySize < sizeof(TARGET_OBJECT))
					{
						KdPrint(("[DEBUG] ERROR - Wrong input buffer size - input: %d (must be at least %d)\n", InputBufferSize, sizeof(TARGET_OBJECT)));
						status = STATUS_INVALID_BUFFER_SIZE;
						break;
					}

					__try
					{
						// copy input buffer to our array - this will make sure that the buffer isn't changed in user mode while it is being used in the kernel mode
						PTARGET_OBJECT pTargetObjectArray = (PTARGET_OBJECT) ExAllocatePoolWithTag(NonPagedPool, uTargetObjArraySize, TAG_TARGET_OBJECTS_ARRAY);
						RtlCopyMemory(pTargetObjectArray, (((PCOMMAND_MESSAGE) InputBuffer)->pData), uTargetObjArraySize);

						status = FmStartHiding(pTargetObjectArray, uTargetObjArraySize);

						ExFreePoolWithTag(pTargetObjectArray, TAG_TARGET_OBJECTS_ARRAY);

						if(!NT_SUCCESS(status))
						{
							break;
						}
					}
					__except(ExceptionFilter(GetExceptionInformation(), TRUE)) 
					{
						return GetExceptionCode();
					}
					
				}
				break;
			default:
				status = STATUS_INVALID_DEVICE_REQUEST;
				break;
			}

	}
	// input is NULL
	else
	{
		status = STATUS_INVALID_PARAMETER;
	}

	return status;
}

NTSTATUS UnloadCallback(IN FLT_FILTER_UNLOAD_FLAGS uFlags)
{
	// first close the port
	FltCloseCommunicationPort(globalData.pServerPort);

	// unregister the filter
	FltUnregisterFilter(globalData.pFilter);

	// if symbols list have been initialized
	if(SymIsInitialized())
	{
		// delete symbols data structures
		SymUnInit();
	}

	// uninitialize the file monitor/hider
	FmUnInit();

	KdPrint(("[DEBUG] Unload called!\n"));

	return STATUS_SUCCESS;
}

NTSTATUS InstanceSetupCallback (IN PCFLT_RELATED_OBJECTS  FltObjects, IN FLT_INSTANCE_SETUP_FLAGS  Flags,
						IN DEVICE_TYPE  VolumeDeviceType, IN FLT_FILESYSTEM_TYPE  VolumeFilesystemType)
{
	// attach to all volumes currently active and all volumes connected in the future (USB flashdrives etc)
	return STATUS_SUCCESS;
}

#ifdef __cplusplus
extern "C" {
#endif
NTSTATUS DriverEntry(IN OUT PDRIVER_OBJECT pDriverObject, IN PUNICODE_STRING puszRegistryPath)
{
	NTSTATUS status;

	// register callbacks/IRPs that will be handled
	const FLT_OPERATION_REGISTRATION callbacks[] = {
													{IRP_MJ_WRITE, 0, FmPreWrite, NULL},
													{IRP_MJ_OPERATION_END}
												   };

	// initialize context registration
	const FLT_CONTEXT_REGISTRATION contextRegistration[] = {
															{FLT_CONTEXT_END}	// none
															};
	// initialize filter registration
	const FLT_REGISTRATION filterRegistration = {
												sizeof(FLT_REGISTRATION),		// size
												FLT_REGISTRATION_VERSION,		// version
												0,								// flags
												contextRegistration,			// use defined context registration
												callbacks,						// use defined callbacks
												UnloadCallback,					// Unload callback
												InstanceSetupCallback,			// InstanceSetup callback
												NULL,							// InstanceQueryTeardown callback
												NULL,							// InstanceTeardownStart callback
												NULL,							// InstanceTeardownComplete callback
												NULL,							// GenerateFileName callback
												NULL,							// GenerateDestinationFileName callback
												NULL							// NormalizeNameComponent callback
	};

	// initialize symbol helper
	SymInit();

	// initialize FileMonitor
	if(!NT_SUCCESS(FmInit()))
	{
		KdPrint(("[DEBUG] ERROR - Initialization of file monitor and hider failed. Driver will be unloaded...\n"));
		return STATUS_INVALID_PARAMETER;
	}

	// register the filter
	status = FltRegisterFilter(pDriverObject, &filterRegistration, &globalData.pFilter);

	if (!NT_SUCCESS(status))
	{
		KdPrint(("[DEBUG] ERROR - Registration of file system minifilter failed - status 0x%x\n", status));
		return status;
	}

	// create communication port that will be used for communication between the user and the kernel mode

	// first build the default descriptor with all access
	PSECURITY_DESCRIPTOR pSecurityDescriptor;
	status = FltBuildDefaultSecurityDescriptor(&pSecurityDescriptor, FLT_PORT_ALL_ACCESS);

	if (!NT_SUCCESS(status))
	{
		KdPrint(("[DEBUG] ERROR - Could not build default security descriptor - communication port was not created...\n"));
		return status;
	}

	// initialize the port attributes - will be accessed from kernel only and is case insensitive
	UNICODE_STRING usPortName;
	RtlInitUnicodeString(&usPortName, PORT_NAME);

	OBJECT_ATTRIBUTES objectAttributes;
	InitializeObjectAttributes(	&objectAttributes,							// pointer to object attributes (return)
								&usPortName,								// name of the communication port
								OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE,	// handle can only be accessed in kernel mode and is case insensitive
								NULL,										// root directory is NULL
								pSecurityDescriptor							// security descriptor for the port
							  );

	// create the communication port
	status = FltCreateCommunicationPort( globalData.pFilter,				// filter pointer
										&globalData.pServerPort,			// return server port - handle to the port
										&objectAttributes,					// define attributes of the port
										NULL,								// no server port cookie, using just one port
										PortConnectCallback,				// connect notify callback							
										PortDisconnectCallback,				// disconnect notify callback
										PortMessageCallback,				// message callback - will be called when message arrives from the user mode
										1									// only one connection is allowed!
									   );


	FltFreeSecurityDescriptor(pSecurityDescriptor);
	if (!NT_SUCCESS(status)) 
	{
		KdPrint(("[DEBUG] ERROR - Could not create communication port - error 0x%x...\n", status));
		//unregister filter
		FltUnregisterFilter(globalData.pFilter);
		return status;
	}

	// start filtering the specified requests
	status = FltStartFiltering(globalData.pFilter);

	// if filtering could not be started, exit
	if(!NT_SUCCESS(status))
	{
		// unregister the filter, close the port and return status
		KdPrint(("[DEBUG] ERROR - Could not start filtering - error 0x%x...\n", status));
		FltUnregisterFilter(globalData.pFilter);
		FltCloseCommunicationPort(globalData.pServerPort);
		return status;
	}

	KdPrint(("[DEBUG] DementiaFS mini-filter successfully created\n"));

	return STATUS_SUCCESS;
	
}

#ifdef __cplusplus
}; // extern "C"
#endif
