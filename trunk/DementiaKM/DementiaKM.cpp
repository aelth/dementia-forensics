///////////////////////////////////////////////////////////////////////////////
///
/// Copyright (c) 2012 - <company name here>
///
/// Original filename: DementiaKM.cpp
/// Project          : DementiaKM
/// Date of creation : 2012-09-17
/// Author(s)        : 
///
/// Purpose          : <description>
///
/// Revisions:
///  0000 [2012-09-17] Initial revision.
///
///////////////////////////////////////////////////////////////////////////////

// $Id$

#ifdef __cplusplus
extern "C" {
#endif
#include <ntddk.h>
#include <string.h>
#include "SymbolEngine.h"
#include "HookEngine.h"
#include "HideEngine.h"
#ifdef __cplusplus
}; // extern "C"
#endif

#include "DementiaKM.h"

#ifdef __cplusplus
namespace { // anonymous namespace to limit the scope of this global variable!
#endif
PDRIVER_OBJECT pdoGlobalDrvObj = 0;
PKEVENT pHidingFinishedEvent = NULL;
#ifdef __cplusplus
}; // anonymous namespace
#endif

NTSTATUS DispatchCreateClose(IN PDEVICE_OBJECT DeviceObject, IN PIRP Irp)
{
	NTSTATUS status = STATUS_SUCCESS;
	Irp->IoStatus.Status = status;
	Irp->IoStatus.Information = 0;
	IoCompleteRequest(Irp, IO_NO_INCREMENT);
	return status;
}

NTSTATUS DispatchDeviceControl(IN PDEVICE_OBJECT DeviceObject, IN PIRP Irp)
{
	PAGED_CODE();
	NTSTATUS status = STATUS_SUCCESS;
	PIO_STACK_LOCATION irpSp = IoGetCurrentIrpStackLocation(Irp);
	PVOID pInputBuffer = NULL;
	PVOID pOutputBuffer = NULL;
	ULONG uRequiredInputSize = 0;
	ULONG information = 0;

	pInputBuffer = pOutputBuffer = Irp->AssociatedIrp.SystemBuffer;
	ULONG uInputBufferLen = irpSp->Parameters.DeviceIoControl.InputBufferLength;
	ULONG uOutputBufferLen = irpSp->Parameters.DeviceIoControl.OutputBufferLength;

	switch(irpSp->Parameters.DeviceIoControl.IoControlCode)
	{
	case IOCTL_DEMENTIAKM_GET_SYMS:
		// using brackets for variable declaration/definition
		{
			KdPrint(("[DEBUG] Creating list of symbols...\n"));

			ULONG uArrayLen = 0;
			PINTERNAL_SYMBOL pSymbolsArray = SymGetSymbols(&uArrayLen);

			if(pSymbolsArray == NULL)
			{
				KdPrint(("[DEBUG] ERROR - Error while creating array of symbols\n"));
				status = STATUS_MEMORY_NOT_ALLOCATED;
				information = 0;
				break;
			}

			ULONG uOutputSize = uArrayLen * sizeof(INTERNAL_SYMBOL);
			if(uOutputBufferLen < uOutputSize)
			{
				KdPrint(("[DEBUG] ERROR - Output buffer size (%d) is less than required size (%d) \n", uOutputBufferLen, uOutputSize));
				
				// release the memory!
				ExFreePoolWithTag(pSymbolsArray, TAG_INTERNAL_SYMBOL_ARRAY);

				status = STATUS_INVALID_BUFFER_SIZE;
				information = 0;
				break;
			}

			RtlCopyMemory(pOutputBuffer, pSymbolsArray, uOutputSize);

			// caller must release the memory
			ExFreePoolWithTag(pSymbolsArray, TAG_INTERNAL_SYMBOL_ARRAY);

			information = uOutputSize;
		}
		break;
	case IOCTL_DEMENTIAKM_STORE_SYMS:
		// using brackets for variable declaration/definition
		{
			KdPrint(("[DEBUG] Storing symbol addresses received from the user-mode...\n"));

			uRequiredInputSize = sizeof(INTERNAL_SYMBOL) * SymGetSymbolCount();

			// input buffer must be an array of symbol_count INTERNAL_SYMBOL structures
			// output buffer must be 0
			if(uInputBufferLen != uRequiredInputSize || uOutputBufferLen != 0)
			{
				KdPrint(("[DEBUG] ERROR - Wrong input or output buffer size - input: %d (must be %d); output: %d (must be 0)\n", uInputBufferLen, uRequiredInputSize, uOutputBufferLen));
				status = STATUS_INVALID_BUFFER_SIZE;
				information = 0;
				break;
			}

			PINTERNAL_SYMBOL pSymbolsArray = (PINTERNAL_SYMBOL) pInputBuffer;
			// one very important thing to note - SymbolEngine is not responsible for checking validity of received symbols!
			// SymbolEngine is just a storage place for symbols -- users of symbols are responsible for checking the validity of the used symbols
			status = SymAddSymbols(pSymbolsArray, uInputBufferLen);

			if(!NT_SUCCESS(status))
			{
				information = 0;
				break;
			}

			information = irpSp->Parameters.DeviceIoControl.OutputBufferLength;
		}
		break;
	case IOCTL_DEMENTIAKM_STORE_FINISH_EVENT:
		// using brackets for variable declaration/definition
		{
			KdPrint(("[DEBUG] Storing synchronization event received from the user-mode...\n"));

			if(uInputBufferLen != sizeof(HANDLE))
			{
				KdPrint(("[DEBUG] ERROR - Wrong input size - input size must be equal to size of the HANDLE (%d)\n", sizeof(HANDLE)));
				status = STATUS_INVALID_BUFFER_SIZE;
				information = 0;
				break;
			}

			if(pInputBuffer == NULL)
			{
				KdPrint(("[DEBUG] ERROR - Invalid handle received from the user mode\n"));
				status = STATUS_INVALID_PARAMETER;
				information = 0;
				break;
			}

			HANDLE hUMEvent = *((PHANDLE) pInputBuffer);
			if(hUMEvent == NULL)
			{
				KdPrint(("[DEBUG] ERROR - Invalid handle received from the user mode\n"));
				status = STATUS_INVALID_PARAMETER;
				information = 0;
				break;
			}

			// using the received handle, get the underlying event object
			if(!NT_SUCCESS(ObReferenceObjectByHandle(	hUMEvent,						// handle from the user mode
														SYNCHRONIZE | DELETE,			// interested in synchronization and deletion
														*ExEventObjectType,				// event type
														UserMode,						// perform access checks
														(PVOID *) &pHidingFinishedEvent,// pointer to underlying event object
														NULL							// no handle information
													)))
			{
				KdPrint(("[DEBUG] ERROR - Could not reference event object passed from the user mode\n"));
				status = STATUS_INVALID_PARAMETER;
				information = 0;
				break;
			}

			information = irpSp->Parameters.DeviceIoControl.OutputBufferLength;
		}
		break;
	case IOCTL_DEMENTIAKM_START_HIDING:
		// using brackets for variable declaration/definition
		{
			KdPrint(("[DEBUG] Starting hiding modules...\n"));

			// input buffer must be an array of TARGET_OBJECT structures, at least one structure
			if(uInputBufferLen == 0 || uInputBufferLen < sizeof(TARGET_OBJECT))
			{
				KdPrint(("[DEBUG] ERROR - Wrong input buffer size - input: %d (must be at least %d)\n", uInputBufferLen, sizeof(TARGET_OBJECT)));
				status = STATUS_INVALID_BUFFER_SIZE;
				information = 0;
				break;
			}

			PTARGET_OBJECT pTargetObjectArray = (PTARGET_OBJECT) pInputBuffer;
			status = HidStartHiding(pTargetObjectArray, uInputBufferLen, pHidingFinishedEvent);

			if(!NT_SUCCESS(status))
			{
				information = 0;
				break;
			}
		}
		break;
	default:
		status = STATUS_INVALID_DEVICE_REQUEST;
		information = 0;
		break;
	}
	Irp->IoStatus.Status = status;
	Irp->IoStatus.Information = information;

	IoCompleteRequest(Irp, IO_NO_INCREMENT);
	return status;
}

VOID DriverUnload(IN PDRIVER_OBJECT DriverObject)
{
	PDEVICE_OBJECT pdoNextDeviceObj = pdoGlobalDrvObj->DeviceObject;
	
	// first deinitialize hook engine
	HkUnInit();

	// if symbols list have been initialized
	if(SymIsInitialized())
	{
		// delete symbols data structures
		SymUnInit();
	}

	// uninitialize hide engine
	HidUnInit();

	IoDeleteSymbolicLink(&usSymlinkName);

	// dereference the hiding event
	if(pHidingFinishedEvent != NULL)
	{
		ObDereferenceObject(pHidingFinishedEvent);
	}

	// Delete all the device objects
	while(pdoNextDeviceObj)
	{
		PDEVICE_OBJECT pdoThisDeviceObj = pdoNextDeviceObj;
		pdoNextDeviceObj = pdoThisDeviceObj->NextDevice;
		IoDeleteDevice(pdoThisDeviceObj);
	}

	DbgPrint("Unload called!\n");
}

#ifdef __cplusplus
extern "C" {
#endif
NTSTATUS DriverEntry(IN OUT PDRIVER_OBJECT DriverObject, IN PUNICODE_STRING RegistryPath)
{
	PDEVICE_OBJECT pdoDeviceObj = 0;
	NTSTATUS status = STATUS_UNSUCCESSFUL;
	pdoGlobalDrvObj = DriverObject;


	// Create the device object.
	if(!NT_SUCCESS(status = IoCreateDevice(	DriverObject,						// pointer to driver object (parameter to DriverEntry)
											0,									// device extensions are not used
											&usDeviceName,						// UNICODE_STRING containing the name of the device object
											FILE_DEVICE_UNKNOWN,				// type of the device - using UNKNOWN
											FILE_DEVICE_SECURE_OPEN,			// device characteristics
											FALSE,								// our driver is not exclusive - it should be, but setting TRUE here leads to problems
											&pdoDeviceObj						// device object to be initialized
										    )
										    )
											)
	{
		// Device not created successfully, we must exit (the driver is unloaded)
		KdPrint(("[DEBUG] ERROR - Creation of device object failed\n"));
		return status;
	};

	// After creation of the device, we have to create a symbolic link in order to allow user -> kernel communication
	if(!NT_SUCCESS(status = IoCreateSymbolicLink(	&usSymlinkName,			// symbolic link name which is accessible from user mode
													&usDeviceName			// device name passed to IoCreateDevice
													)
													)
													)
	{
		// Symbolic link creation failed, we delete created DeviceObject and exit
		IoDeleteDevice(pdoDeviceObj);
		KdPrint(("[DEBUG] ERROR - Failure while creating symbolic link\n"));
		return status;
	}

	// set all dispatchers to NULL
	for(int i = 0; i < IRP_MJ_MAXIMUM_FUNCTION; i++)
	{
		DriverObject->MajorFunction[i] = NULL;
	}

	DriverObject->MajorFunction[IRP_MJ_CREATE] =
	DriverObject->MajorFunction[IRP_MJ_CLOSE] = DispatchCreateClose;
	DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = DispatchDeviceControl;
	DriverObject->DriverUnload = DriverUnload;

	// initialize symbol helper
	SymInit();

	// initialize hook engine
	HkInit();

	// initialize hide engine
	HidInit();

	KdPrint(("[DEBUG] Device successfully created\n"));

	return STATUS_SUCCESS;
}
#ifdef __cplusplus
}; // extern "C"
#endif
