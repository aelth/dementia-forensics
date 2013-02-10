#include "CrashDumpDetector.h"
#include "CrashDumpDetectorPrivateIncludes.h"

// boolean representing status of CrashDumpDetector
static BOOLEAN bIsCrashDumpDetectorInitialized = FALSE;

// for every crash dump, a structure is needed that will contain the handle of the crash dump file
// and the address/range fixes between the real physical address and the address in the crash dump file
static NPAGED_LOOKASIDE_LIST AddressFixLookasideList;

// the head of the address fix list
static LIST_ENTRY AddressFixListHead;

// mutex which will protect accesses to handles list
static FAST_MUTEX AddressFixListMutex;

VOID CddhAddAddressFixes(const IN HANDLE, const IN PDUMP_HEADER);
PVOID CddhFixAddress(const IN PVOID, const IN PADDRESS_FIX_ENTRY);

BOOLEAN CddInit(VOID)
{
	KeEnterCriticalRegion();
	if(bIsCrashDumpDetectorInitialized == TRUE)
	{
		KeLeaveCriticalRegion();
		return TRUE;
	}

	ASSERTMSG("Fast mutex must be initialized at or below DISPATCH_LEVEL", KeGetCurrentIrql() <= DISPATCH_LEVEL);
	// initialize all required data structures
	ExInitializeNPagedLookasideList(&AddressFixLookasideList,			// list to initialize
									NULL,								// allocate function - OS supplied
									NULL,								// free function - OS supplied
									0,									// flags - always zero
									sizeof(ADDRESS_FIX_ENTRY),			// size of each entry to be allocated
									TAG_ADDRESS_FIX_LOOKASIDE,			// AfxL(ookaside) tag
									0									// depth - always zero
									);
	InitializeListHead(&AddressFixListHead);
	ExInitializeFastMutex(&AddressFixListMutex);

	bIsCrashDumpDetectorInitialized = TRUE;
	KeLeaveCriticalRegion();

	return STATUS_SUCCESS;
}

VOID CddUnInit(VOID)
{
	KeEnterCriticalRegion();

	// check if detector has been initialized
	if(bIsCrashDumpDetectorInitialized == FALSE)
	{
		KeLeaveCriticalRegion();
		return;
	}

	ASSERTMSG("Fast mutex acquire must occur at or below APC_LEVEL", KeGetCurrentIrql() <= APC_LEVEL);
	ExAcquireFastMutex(&AddressFixListMutex);
	
	// free all entries in lookaside list
	ExDeleteNPagedLookasideList(&AddressFixLookasideList);
	ASSERTMSG("Fast mutex release must occur at APC_LEVEL", KeGetCurrentIrql() == APC_LEVEL);
	ExReleaseFastMutex(&AddressFixListMutex);

	bIsCrashDumpDetectorInitialized = FALSE;
	KeLeaveCriticalRegion();
}

BOOLEAN CddIsCrashDumpFile(const IN HANDLE hDumpFile, const IN PVOID pPage, const IN ULONG uSize)
{
	// check arguments -- will not check the handle, because handle is really arbitrary and since it won't be opened by the detector, don't sanitize it
	if(pPage == NULL)
	{
		KdPrint(("[DEBUG] WARNING - Cannot determine dump type (raw/crash dump) because initial page is NULL\n"));
		return FALSE;
	}

	// page size must be 0x1000 (32-bit systems) or 0x2000 (64-bit)
#ifdef _WIN64
	ULONG uPageSize = 0x2000;
	ULONG uValidDump = 0x34365544;
#else // _WIN32
	ULONG uPageSize = 0x1000;
	ULONG uValidDump = 0x504d5544;
#endif // _WIN64

	if(uSize < uPageSize)
	{
		KdPrint(("[DEBUG] WARNING - Cannot determine dump type (raw/crash dump) page size is less than 0x1000/0x2000\n"));
		return FALSE;
	}

	BOOLEAN bIsCrashDump = FALSE;

	// cast the page into DUMP_HEADER
	PDUMP_HEADER pDumpHeader = (PDUMP_HEADER) pPage;

	// check if the page represents the crash dump
	// Signature must match "PAGE" (0x45474150) and ValidDump must match "DUMP" (0x504d5544) on 32-bit or "DU64" (0x34365544) on 64-bit machines
	if(pDumpHeader->Signature == 0x45474150 && pDumpHeader->ValidDump == uValidDump)
	{
		// add fixes to the internal list
		CddhAddAddressFixes(hDumpFile, pDumpHeader);

		KdPrint(("[DEBUG] Detected Windows crash dump file - found valid signature\n"));

		bIsCrashDump = TRUE;
	}

	return bIsCrashDump;
}

PVOID CddGetFixedAddress(const IN HANDLE hDumpFile, const IN PVOID pAddress)
{
	// don't check arguments - address and dump file won't be actually used for anything except retrieval and address fixup
	// address won't be resolved or accessed and handle won't be used for accessing the file

	BOOLEAN bRet = TRUE;
	PLIST_ENTRY addressFixListEntry = AddressFixListHead.Flink;

	// try to find the handle in the list
	ASSERTMSG("Fast mutex acquire must occur at or below APC_LEVEL", KeGetCurrentIrql() <= APC_LEVEL);
	ExAcquireFastMutex(&AddressFixListMutex);
	while(addressFixListEntry != &AddressFixListHead)
	{
		PADDRESS_FIX_ENTRY pAddressFixEntry = CONTAINING_RECORD(addressFixListEntry, ADDRESS_FIX_ENTRY, ListEntry);
		// if this is the wanted entry
		if(pAddressFixEntry->hDumpFile == hDumpFile)
		{
			// perform address fix-up
			PVOID pFixedAddress = CddhFixAddress(pAddress, pAddressFixEntry);

			ASSERTMSG("Fast mutex release must occur at APC_LEVEL", KeGetCurrentIrql() == APC_LEVEL);
			ExReleaseFastMutex(&AddressFixListMutex);
			return pFixedAddress;
		}

		addressFixListEntry = addressFixListEntry->Flink;
	}

	// current crash dump is not known and no address fixes/translations are available
	// this should never happen because the dump file should have been added to the list
	// just return NULL and warn the user
	ASSERTMSG("Fast mutex release must occur at APC_LEVEL", KeGetCurrentIrql() == APC_LEVEL);
	ExReleaseFastMutex(&AddressFixListMutex);

	KdPrint(("[DEBUG] WARNING - Current crash dump file unknown - no address fixes/translations are available...\n"));

	return NULL;
}

VOID CddhAddAddressFixes(const IN HANDLE hDumpFile, const IN PDUMP_HEADER pDumpHeader)
{
	ASSERTMSG("Dump file header cannot be NULL!", pDumpHeader != NULL);
	// don't check the handle, don't care about the value, since it won't be used directly

	ULONG uNumberOfRanges = pDumpHeader->PhysicalMemoryBlock.NumberOfRuns;

	// allocate memory for new "address-fix" entry
	PADDRESS_FIX_ENTRY pAddressFixEntry = (PADDRESS_FIX_ENTRY) ExAllocateFromNPagedLookasideList(&AddressFixLookasideList);
	if(pAddressFixEntry == NULL)
	{
		KdPrint(("[DEBUG] WARNING - Not enough memory in the lookaside list to allocate new address fix entry...\n"));
		return;
	}

	// add the crash dump handle and number of ranges to the address fix entry
	pAddressFixEntry->hDumpFile = hDumpFile;
	pAddressFixEntry->uNumberOfRanges = uNumberOfRanges;

#ifdef _WIN64
	ULONG_PTR uFileOffset = 0x2000;
#else // _WIN32
	ULONG_PTR uFileOffset = 0x1000;
#endif // _WIN64

	for(ULONG uRunIndex = 0; uRunIndex < uNumberOfRanges; uRunIndex++)
	{
		// multiply range start and size by 0x1000
		ULONG_PTR uRangeStartAddress = pDumpHeader->PhysicalMemoryBlock.Run[uRunIndex].BasePage << 12;
		ULONG_PTR uRangeSize = pDumpHeader->PhysicalMemoryBlock.Run[uRunIndex].PageCount << 12;

		// add to the list
		pAddressFixEntry->Ranges[uRunIndex].uFileOffset = uFileOffset;
		pAddressFixEntry->Ranges[uRunIndex].uRealStartAddress = uRangeStartAddress;
		pAddressFixEntry->Ranges[uRunIndex].uRangeSize = uRangeSize;

		uFileOffset += uRangeSize;
	}

	// add the new entry to the list (thread safely)
	ASSERTMSG("Fast mutex acquire/release must occur at or below APC_LEVEL", KeGetCurrentIrql() <= APC_LEVEL);
	ExAcquireFastMutex(&AddressFixListMutex);
	InsertHeadList(&AddressFixListHead, &pAddressFixEntry->ListEntry);
	ExReleaseFastMutex(&AddressFixListMutex);
}

PVOID CddhFixAddress(const IN PVOID pAddress, const IN PADDRESS_FIX_ENTRY pAddressFixEntry)
{
	ASSERTMSG("Address fix entry cannot be NULL!", pAddressFixEntry != NULL);
	// don't check pAddress, it can be anything, including NULL!

	ULONG uNumberOfRanges = pAddressFixEntry->uNumberOfRanges;
	ULONG_PTR uRealAddress = (ULONG_PTR) pAddress;

	for(ULONG uRunIndex = 0; uRunIndex < uNumberOfRanges; uRunIndex++)
	{
		// get the entries from the address fix entry
		ULONG_PTR uRangeStartAddress = pAddressFixEntry->Ranges[uRunIndex].uRealStartAddress;
		ULONG_PTR uRangeSize = pAddressFixEntry->Ranges[uRunIndex].uRangeSize;
		ULONG_PTR uFileOffset = pAddressFixEntry->Ranges[uRunIndex].uFileOffset;

		// check if the passed physical address belongs to the current run		
		if(uRealAddress >= uRangeStartAddress && uRealAddress <= (uRangeStartAddress + uRangeSize))
		{
			// get relative address offset inside the run
			ULONG_PTR uAddressOffset = uRealAddress - uRangeStartAddress;

			// get the file offset - this is the final offset that will be used for writing inside the file
			ULONG_PTR uAddressFileOffset = uFileOffset + uAddressOffset;
			return (PVOID) uAddressFileOffset;
		}
	}

	// address was not found in any run - this should never happen
	KdPrint(("[DEBUG] WARNING - Address 0x%p not found in any physical memory run!\n"));
	return NULL;
}