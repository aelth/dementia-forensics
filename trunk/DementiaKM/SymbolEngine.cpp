#include "SymbolEngine.h"
#include <ntstrsafe.h>

#define TAG_SYMBOL_LOOKASIDE ('LmyS')

// boolean representing status of SymbolEngine
static BOOLEAN bIsSymEngineInitialized = FALSE;

// since we are always going to allocate fixed-sized memory blocks (INTERNAL_SYMBOL structures),
// it is very convenient and efficient to use lookaside lists
static NPAGED_LOOKASIDE_LIST SymbolsLookasideList;

// the head of the symbols linked list - list of all internal symbols we want to hook or
// have under our supervision
static LIST_ENTRY SymbolsListHead;

// mutex which will protect access to symbols list
static FAST_MUTEX SymbolsListMutex;

// symbol count
static ULONG uSymbolCount = 0;

BOOLEAN SympAddSymbol(IN PCHAR pszSymbolName, IN ULONG64 uSymbolAddress, IN ULONG uOffset, IN ULONG uBitPosition, IN ULONG uBitLength);
PSYMBOL_ENTRY SympFindSymbol(IN PCHAR pszSymbolName);

VOID SymInit(void)
{
	KeEnterCriticalRegion();
	if(bIsSymEngineInitialized == TRUE)
	{
		KeLeaveCriticalRegion();
		return;
	}

	// initialize all required data structures
	ExInitializeNPagedLookasideList(&SymbolsLookasideList,				// list to initialize
									NULL,								// allocate function - OS supplied
									NULL,								// free function - OS supplied
									0,									// flags - always zero
									sizeof(SYMBOL_ENTRY),				// size of each entry to be allocated
									TAG_SYMBOL_LOOKASIDE,				// SymL(ookaside) tag
									0									// depth - always zero
									);
	InitializeListHead(&SymbolsListHead);

	ASSERTMSG("Fast mutex must be initialized at or below DISPATCH_LEVEL", KeGetCurrentIrql() <= DISPATCH_LEVEL);
	ExInitializeFastMutex(&SymbolsListMutex);

	bIsSymEngineInitialized = TRUE;

	KeLeaveCriticalRegion();
}

VOID SymUnInit(void)
{
	ASSERTMSG("SymbolEngine must be initialized prior to this call", bIsSymEngineInitialized == TRUE);
	ASSERTMSG("Critical region and lookaside list deletion must occur at or below APC_LEVEL", KeGetCurrentIrql() <= APC_LEVEL);

	KeEnterCriticalRegion();
	// free all entries in lookaside list
	ExDeleteNPagedLookasideList(&SymbolsLookasideList);

	// set flag so someone can initialize SymbolHelper again if needed
	bIsSymEngineInitialized = FALSE;

	// set symbol count to "initial" value
	uSymbolCount = 0;
	KeLeaveCriticalRegion();
}

BOOLEAN SymIsInitialized(void)
{
	return bIsSymEngineInitialized;
}

PINTERNAL_SYMBOL SymGetSymbols(OUT PULONG puSymbolCount)
{
	ASSERTMSG("SymbolEngine must be initialized prior to this call", bIsSymEngineInitialized == TRUE);
	ASSERTMSG("Pointer to number of currently defined symbols must not be NULL", puSymbolCount != NULL);

	PLIST_ENTRY symbolListEntry = SymbolsListHead.Flink;

	*puSymbolCount = SymGetSymbolCount();

	// check if there are symbols in the list
	if(*puSymbolCount == 0)
	{
		KdPrint(("[DEBUG] No symbols in the list!\n"));
		return NULL;
	}

	// allocate memory for the INTERNAL_SYMBOL array
	PINTERNAL_SYMBOL pInternalSymbolArray = (PINTERNAL_SYMBOL) ExAllocatePoolWithTag(	NonPagedPool,								// must be NonPaged
																						*puSymbolCount * sizeof(INTERNAL_SYMBOL),
																						TAG_INTERNAL_SYMBOL_ARRAY					// Syma(rray)
																					  );

	if(pInternalSymbolArray == NULL)
	{
		KdPrint(("[DEBUG] ERROR - Unable to allocate memory for internal symbol array!\n"));
		return NULL;
	}

	// iterate through symbols list
	ULONG uSymIndex = 0;
	ASSERTMSG("Fast mutex acquire must occur at or below APC_LEVEL", KeGetCurrentIrql() <= APC_LEVEL);
	ExAcquireFastMutex(&SymbolsListMutex);
	while(symbolListEntry != &SymbolsListHead)
	{
		PSYMBOL_ENTRY pSymbolEntry = CONTAINING_RECORD(symbolListEntry, SYMBOL_ENTRY, ListEntry);
		
		// copy name of the symbol
		if(RtlStringCbCopyA(pInternalSymbolArray[uSymIndex].name, MAX_SYM_NAME, pSymbolEntry->Symbol.name) == STATUS_INVALID_PARAMETER)
		{
			KdPrint(("[DEBUG] ERROR - Error while copying symbol name to INTERNAL_SYMBOL structure\n"));
			return NULL;
		}

		// copy address from the symbol in the list
		pInternalSymbolArray[uSymIndex].u64address = pSymbolEntry->Symbol.u64address;

		// copy offset from the symbol in the list
		pInternalSymbolArray[uSymIndex].uOffset = pSymbolEntry->Symbol.uOffset;

		// copy bit position from the symbol in the list
		pInternalSymbolArray[uSymIndex].uBitPosition = pSymbolEntry->Symbol.uBitPosition;

		// copy bit length from the symbol in the list
		pInternalSymbolArray[uSymIndex].uBitLength = pSymbolEntry->Symbol.uBitLength;
		
		uSymIndex++;
		symbolListEntry = symbolListEntry->Flink;
	}

	// release mutex before returning the list to the main program
	ASSERTMSG("Fast mutex release must occur at APC_LEVEL", KeGetCurrentIrql() == APC_LEVEL);
	ExReleaseFastMutex(&SymbolsListMutex);
	return pInternalSymbolArray;
}

ULONG SymGetSymbolCount(void)
{
	ASSERTMSG("SymbolEngine must be initialized prior to this call", bIsSymEngineInitialized == TRUE);

	// immediately acquire mutex, because of current count query
	ASSERTMSG("Fast mutex acquire must occur at or below APC_LEVEL", KeGetCurrentIrql() <= APC_LEVEL);
	ExAcquireFastMutex(&SymbolsListMutex);
	if(uSymbolCount != 0)
	{
		ASSERTMSG("Fast mutex release must occur at APC_LEVEL", KeGetCurrentIrql() == APC_LEVEL);
		ExReleaseFastMutex(&SymbolsListMutex);
		return uSymbolCount;
	}

	PLIST_ENTRY symbolListEntry = SymbolsListHead.Flink;
	ULONG uTmpSymbolCount = 0;

	// iterate through symbols list
	while(symbolListEntry != &SymbolsListHead)
	{
		++uTmpSymbolCount;
		symbolListEntry = symbolListEntry->Flink;
	}

	// release the spinlock and return the count
	uSymbolCount = uTmpSymbolCount;
	ASSERTMSG("Fast mutex release must occur at APC_LEVEL", KeGetCurrentIrql() == APC_LEVEL);
	ExReleaseFastMutex(&SymbolsListMutex);
	return uTmpSymbolCount;
}

BOOLEAN SymAddSymbol( IN PCHAR pszSymbolName, IN ULONG64 uSymbolAddress, IN ULONG uOffset, IN ULONG uBitPosition, IN ULONG uBitLength )
{
	return SympAddSymbol(pszSymbolName, uSymbolAddress, uOffset, uBitPosition, uBitLength);
}

NTSTATUS SymAddSymbols(IN PINTERNAL_SYMBOL pSymbolsArray, IN ULONG uArraySize)
{
	ASSERTMSG("Passed symbols array cannot be NULL", pSymbolsArray != NULL);
	ASSERTMSG("SymbolEngine must be initialized prior to this call", bIsSymEngineInitialized == TRUE);

	// first check passed parameters - array size must not be 0
	if(uArraySize == 0)
	{
		KdPrint(("[DEBUG] ERROR - Size of symbols array cannot be 0!\n"));
		return STATUS_INFO_LENGTH_MISMATCH;
	}

	ULONG uSymCount = uArraySize / sizeof(INTERNAL_SYMBOL);

	// for all symbols in array
	for(ULONG i = 0; i < uSymCount; i++)
	{
		// add it to internal list
		if(!SymUpdateSymbol(&pSymbolsArray[i]))
		{
			// if error occurred, assume parameter is invalid - symbol does not exist!
			return STATUS_INVALID_PARAMETER;
		}
	}

	return STATUS_SUCCESS;
}

BOOLEAN SymUpdateSymbol(IN PINTERNAL_SYMBOL pSymbol)
{
	ASSERTMSG("Passed symbol cannot be NULL", pSymbol != NULL);
	ASSERTMSG("SymbolEngine must be initialized prior to this call", bIsSymEngineInitialized == TRUE);

	PSYMBOL_ENTRY pWantedSymbol = SympFindSymbol(pSymbol->name);
	ASSERTMSG("Fast mutex acquire must occur at or below APC_LEVEL", KeGetCurrentIrql() <= APC_LEVEL);
	ExAcquireFastMutex(&SymbolsListMutex);
	if(pWantedSymbol == NULL)
	{
		KdPrint(("[DEBUG] ERROR - %s private symbol does not exist!\n", pSymbol->name));
		ASSERTMSG("Fast mutex release must occur at APC_LEVEL", KeGetCurrentIrql() == APC_LEVEL);
		ExReleaseFastMutex(&SymbolsListMutex);
		return FALSE;
	}

	// update all symbol fields
	pWantedSymbol->Symbol.u64address = pSymbol->u64address;
	pWantedSymbol->Symbol.uOffset = pSymbol->uOffset;
	pWantedSymbol->Symbol.uBitPosition = pSymbol->uBitPosition;
	pWantedSymbol->Symbol.uBitLength = pSymbol->uBitLength;

	ASSERTMSG("Fast mutex release must occur at APC_LEVEL", KeGetCurrentIrql() == APC_LEVEL);
	ExReleaseFastMutex(&SymbolsListMutex);
	return TRUE;
}

ULONG64 SymGetAddress(IN PCHAR pszSymbolName)
{
	ASSERTMSG("Passed symbol name is NULL", pszSymbolName != NULL);
	ASSERTMSG("SymbolEngine must be initialized prior to this call", bIsSymEngineInitialized == TRUE);

	ULONG64 u64Address = -1;

	PSYMBOL_ENTRY pWantedSymbol = SympFindSymbol(pszSymbolName);
	ASSERTMSG("Fast mutex acquire must occur at or below APC_LEVEL", KeGetCurrentIrql() <= APC_LEVEL);
	ExAcquireFastMutex(&SymbolsListMutex);
	if(pWantedSymbol == NULL || pWantedSymbol->Symbol.u64address == -1)
	{
		KdPrint(("[DEBUG] ERROR - %s private symbol does not exist, or its address is -1\n", pszSymbolName));
		ASSERTMSG("Fast mutex release must occur at APC_LEVEL", KeGetCurrentIrql() == APC_LEVEL);
		ExReleaseFastMutex(&SymbolsListMutex);
		return u64Address;
	}

	u64Address = pWantedSymbol->Symbol.u64address;
	ASSERTMSG("Fast mutex release must occur at APC_LEVEL", KeGetCurrentIrql() == APC_LEVEL);
	ExReleaseFastMutex(&SymbolsListMutex);
	return u64Address;
}

ULONG64 SymGetExportedAddress(IN PCHAR pszSymbolName)
{
	ASSERTMSG("Passed symbol name is NULL", pszSymbolName != NULL);
	ASSERTMSG("SymbolEngine must be initialized prior to this call", bIsSymEngineInitialized == TRUE);

	ULONG64 u64Func = NULL;
	ULONG64 u64FuncBackup = NULL;
	ANSI_STRING szFunctionName;
	UNICODE_STRING uszFunctionName;

	u64Func = SymGetAddress(pszSymbolName);
	RtlInitAnsiString(&szFunctionName, pszSymbolName);
	
	// use UNICODE_STRING for MmGetSystemRoutineAddress
	memset(&uszFunctionName, 0, sizeof(UNICODE_STRING));
	if(!NT_SUCCESS(RtlAnsiStringToUnicodeString(&uszFunctionName, &szFunctionName, TRUE)))
	{
		// if initialization failed, use obtained address, no matter what
		return u64Func;
	}

	u64FuncBackup = (ULONG64) MmGetSystemRoutineAddress(&uszFunctionName);

	if(u64Func == -1)
	{
		KdPrint(("[DEBUG] WARNING - Address of %s function not found - trying with the address retrieved from MmGetSystemRoutineAddress\n", szFunctionName.Buffer));
		if(u64FuncBackup == NULL)
		{
			KdPrint(("[DEBUG] ERROR - MmGetSystemRoutineAddress for %s failed!\n", szFunctionName.Buffer));
			RtlFreeUnicodeString(&uszFunctionName);
			return NULL;
		}
		else
		{
			// use address obtained from the MmGet... function as the relevant one
			RtlFreeUnicodeString(&uszFunctionName);
			return u64FuncBackup;
		}
	}
	// check if it differs from the address obtained from the kernel-mode
	else if(u64Func != u64FuncBackup)
	{
		// prefer kernel-mode (if not NULL)
		u64Func = (u64FuncBackup == NULL) ? u64Func : u64FuncBackup;
		RtlFreeUnicodeString(&uszFunctionName);
		return u64Func;
	}

	RtlFreeUnicodeString(&uszFunctionName);
	return u64Func;
}

ULONG SymGetOffset(IN PCHAR pszSymbolName)
{
	ASSERTMSG("Passed symbol name is NULL", pszSymbolName != NULL);
	ASSERTMSG("SymbolEngine must be initialized prior to this call", bIsSymEngineInitialized == TRUE);

	ULONG uOffset = -1;

	PSYMBOL_ENTRY pWantedSymbol = SympFindSymbol(pszSymbolName);
	ASSERTMSG("Fast mutex acquire must occur at or below APC_LEVEL", KeGetCurrentIrql() <= APC_LEVEL);
	ExAcquireFastMutex(&SymbolsListMutex);
	if(pWantedSymbol == NULL || pWantedSymbol->Symbol.uOffset == -1)
	{
		KdPrint(("[DEBUG] ERROR - %s private symbol does not exist, or its offset is -1\n", pszSymbolName));
		ASSERTMSG("Fast mutex release must occur at APC_LEVEL", KeGetCurrentIrql() == APC_LEVEL);
		ExReleaseFastMutex(&SymbolsListMutex);
		return uOffset;
	}

	uOffset = pWantedSymbol->Symbol.uOffset;
	ASSERTMSG("Fast mutex release must occur at APC_LEVEL", KeGetCurrentIrql() == APC_LEVEL);
	ExReleaseFastMutex(&SymbolsListMutex);
	return uOffset;
}

ULONG SymGetBitPosition(IN PCHAR pszSymbolName)
{
	ASSERTMSG("Passed symbol name is NULL", pszSymbolName != NULL);
	ASSERTMSG("SymbolEngine must be initialized prior to this call", bIsSymEngineInitialized == TRUE);

	ULONG uBitPosition = -1;

	PSYMBOL_ENTRY pWantedSymbol = SympFindSymbol(pszSymbolName);
	ASSERTMSG("Fast mutex acquire must occur at or below APC_LEVEL", KeGetCurrentIrql() <= APC_LEVEL);
	ExAcquireFastMutex(&SymbolsListMutex);
	if(pWantedSymbol == NULL || pWantedSymbol->Symbol.uBitPosition == -1)
	{
		KdPrint(("[DEBUG] ERROR - %s private symbol does not exist, or its bit position is -1\n", pszSymbolName));
		ASSERTMSG("Fast mutex release must occur at APC_LEVEL", KeGetCurrentIrql() == APC_LEVEL);
		ExReleaseFastMutex(&SymbolsListMutex);
		return uBitPosition;
	}

	uBitPosition = pWantedSymbol->Symbol.uBitPosition;
	ASSERTMSG("Fast mutex release must occur at APC_LEVEL", KeGetCurrentIrql() == APC_LEVEL);
	ExReleaseFastMutex(&SymbolsListMutex);
	return uBitPosition;
}

ULONG SymGetBitLength(IN PCHAR pszSymbolName)
{
	ASSERTMSG("Passed symbol name is NULL", pszSymbolName != NULL);
	ASSERTMSG("SymbolEngine must be initialized prior to this call", bIsSymEngineInitialized == TRUE);

	ULONG uBitLength = -1;

	PSYMBOL_ENTRY pWantedSymbol = SympFindSymbol(pszSymbolName);
	ASSERTMSG("Fast mutex acquire must occur at or below APC_LEVEL", KeGetCurrentIrql() <= APC_LEVEL);
	ExAcquireFastMutex(&SymbolsListMutex);
	if(pWantedSymbol == NULL || pWantedSymbol->Symbol.uBitLength == -1)
	{
		KdPrint(("[DEBUG] ERROR - %s private symbol does not exist, or its bit length is -1\n", pszSymbolName));
		ASSERTMSG("Fast mutex release must occur at APC_LEVEL", KeGetCurrentIrql() == APC_LEVEL);
		ExReleaseFastMutex(&SymbolsListMutex);
		return uBitLength;
	}

	uBitLength = pWantedSymbol->Symbol.uBitLength;
	ASSERTMSG("Fast mutex release must occur at APC_LEVEL", KeGetCurrentIrql() == APC_LEVEL);
	ExReleaseFastMutex(&SymbolsListMutex);
	return uBitLength;
}

BOOLEAN SymRemoveSymbol(IN PCHAR pszSymbolName)
{
	ASSERTMSG("Passed symbol name is NULL", pszSymbolName != NULL);
	ASSERTMSG("SymbolEngine must be initialized prior to this call", bIsSymEngineInitialized == TRUE);

	// try to find the symbol to remove
	PSYMBOL_ENTRY pSymbolEntry = SympFindSymbol(pszSymbolName);
	// if symbol not found, return NULL
	if(pSymbolEntry == NULL)
	{
		return FALSE;
	}

	// symbol found, so we remove it from the list...
	ASSERTMSG("Fast mutex acquire must occur at or below APC_LEVEL", KeGetCurrentIrql() <= APC_LEVEL);
	ExAcquireFastMutex(&SymbolsListMutex);
	RemoveEntryList(&pSymbolEntry->ListEntry);

	// decrease symbol count (do it inside guarded block!)
	--uSymbolCount;

	// and free allocated memory
	ExFreeToNPagedLookasideList(&SymbolsLookasideList, pSymbolEntry);
	ASSERTMSG("Fast mutex release must occur at APC_LEVEL", KeGetCurrentIrql() == APC_LEVEL);
	ExReleaseFastMutex(&SymbolsListMutex);

	return TRUE;
}

BOOLEAN SympAddSymbol(IN PCHAR pszSymbolName, IN ULONG64 uSymbolAddress, IN ULONG uOffset, IN ULONG uBitPosition, IN ULONG uBitLength)
{
	ASSERTMSG("Cannot add symbol with NULL name", pszSymbolName != NULL);
	ASSERTMSG("SymbolEngine must be initialized prior to this call", bIsSymEngineInitialized == TRUE);

	// it is possible that we got symbol with zero address, offset -1, bit position -1, and length -1 if the symbol was not found during
	// enumeration in user mode. In that case, and only in that case, we return error!
	// NOTE: uSymbolAddress of -1 is used when symbols are initialized in order to send the array of wanted symbols to the user mode
	if(uSymbolAddress == 0 && uOffset == -1 && uBitPosition == -1 && uBitLength == -1)
	{
		KdPrint(("[DEBUG] WARNING - Symbol was probably not found in user mode, cannot add symbol with unknown address and unknown offset\n"));
		return FALSE;
	}

	// if symbol with this name already exists
	if(SympFindSymbol(pszSymbolName) != NULL)
	{
		// don't want to "update" the address -- use SymUpdateSymbol function instead
		KdPrint(("[DEBUG] WARNING - Symbol %s with address 0x%x already exists -- use SymUpdateFunction() to update the address\n", pszSymbolName, uSymbolAddress));
		return TRUE;
	}

	// get memory from lookaside list
	PSYMBOL_ENTRY pSymbolEntry = (PSYMBOL_ENTRY) ExAllocateFromNPagedLookasideList(&SymbolsLookasideList);
	if(pSymbolEntry == NULL)
	{
		KdPrint(("[DEBUG] ERROR - Not enough memory in lookaside list to allocate new symbol entry\n"));
		return FALSE;
	}

	// copy string from passed parameter
	if(RtlStringCbCopyA(pSymbolEntry->Symbol.name, MAX_SYM_NAME, pszSymbolName) == STATUS_INVALID_PARAMETER)
	{
		KdPrint(("[DEBUG] ERROR - Error while copying symbol name to SYMBOL_ENTRY structure\n"));
		return FALSE;
	}

	// copy address from the passed parameter
	pSymbolEntry->Symbol.u64address = uSymbolAddress;

	// copy offset from the passed parameter
	pSymbolEntry->Symbol.uOffset = uOffset;

	// copy bit position from the passed parameter
	pSymbolEntry->Symbol.uBitPosition = uBitPosition;

	// copy bit length from the passed parameter
	pSymbolEntry->Symbol.uBitLength = uBitLength;

	// insert it to list (thread safe)
	ASSERTMSG("Fast mutex acquire must occur at or below APC_LEVEL", KeGetCurrentIrql() <= APC_LEVEL);
	ExAcquireFastMutex(&SymbolsListMutex);
	InsertHeadList(&SymbolsListHead, &pSymbolEntry->ListEntry);
	++uSymbolCount;
	ASSERTMSG("Fast mutex release must occur at APC_LEVEL", KeGetCurrentIrql() == APC_LEVEL);
	ExReleaseFastMutex(&SymbolsListMutex);

	return TRUE;
}

PSYMBOL_ENTRY SympFindSymbol(IN PCHAR pszSymbolName)
{
	ASSERTMSG("Passed symbol name is NULL", pszSymbolName != NULL);
	ASSERTMSG("SymbolEngine must be initialized prior to this call", bIsSymEngineInitialized == TRUE);
	PLIST_ENTRY symbolListEntry = SymbolsListHead.Flink;

	// we are not at dispatch level yet, so getting length of the input string is allowed
	size_t nSymbolNameLength = 0;
	if(RtlStringCbLengthA(pszSymbolName, MAX_SYM_NAME, &nSymbolNameLength) == STATUS_INVALID_PARAMETER)
	{
		KdPrint(("[DEBUG] ERROR - Could not obtain the length of the symbol name\n"));
		return NULL;
	}

	// iterate through symbols list
	ASSERTMSG("Fast mutex acquire must occur at or below APC_LEVEL", KeGetCurrentIrql() <= APC_LEVEL);
	ExAcquireFastMutex(&SymbolsListMutex);
	while(symbolListEntry != &SymbolsListHead)
	{
		PSYMBOL_ENTRY pSymbolEntry = CONTAINING_RECORD(symbolListEntry, SYMBOL_ENTRY, ListEntry);
		// if this is the wanted entry
		if(RtlCompareMemory((PVOID) pszSymbolName, (PVOID)pSymbolEntry->Symbol.name, nSymbolNameLength) == nSymbolNameLength)
		{
			// return it to caller
			ASSERTMSG("Fast mutex release must occur at APC_LEVEL", KeGetCurrentIrql() == APC_LEVEL);
			ExReleaseFastMutex(&SymbolsListMutex);
			return pSymbolEntry;
		}

		symbolListEntry = symbolListEntry->Flink;
	}

	// wanted symbol entry is not present in the list, return NULL
	ASSERTMSG("Fast mutex release must occur at APC_LEVEL", KeGetCurrentIrql() == APC_LEVEL);
	ExReleaseFastMutex(&SymbolsListMutex);
	return NULL;
}