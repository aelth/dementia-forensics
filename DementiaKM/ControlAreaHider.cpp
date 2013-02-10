#include "ControlAreaHider.h"
#include "SymbolWrapper.h"
#include "AllocationHider.h"
#include "FileHider.h"
#include "GenericHider.h"
#include "SegmentHider.h"
#include "WinVerProvider.h"

// boolean representing status of ControlAreaHider
static BOOLEAN bIsControlAreaHiderInitialized = FALSE;

// flag which determines the state of the internal symbols
static BOOLEAN bSymbolsInitialized = FALSE;

// offset of Segment member within CONTROL_AREA structure
static ULONG uCtrlAreaSegmentOffset = -1;

// offset of NumberOfSectionReferences member within CONTROL_AREA structure
static ULONG uCtrlAreaNumberOfSectionReferencesOffset = -1;

// offset of NumberOfMappedViews member within CONTROL_AREA structure
static ULONG uCtrlAreaNumberOfMappedViewsOffset = -1;

// offset of NumberOfUserReferences member within CONTROL_AREA structure
static ULONG uCtrlAreaNumberOfUserReferencesOffset = -1;

// offset of FilePointer member within CONTROL_AREA structure
static ULONG uCtrlAreaFilePointerOffset = -1;

// offset of the member containing the CONTROL_AREA flags
static ULONG uCtrlAreaFlagsOffset = -1;

// bit flags that represent the important CONTROL_AREA flags
static ULONG uCtrlAreaFlagsHadUserReferenceBitPos = -1;
static ULONG uCtrlAreaFlagsHadUserReferenceBitLen = -1;

// bit flag which represents the number of reference counts inside EX_FAST_REF structure
// this structure is used only on Windows Vista and above for decoding pointers to file objects within CONTROL_AREA
static ULONG uExFastRefRefCntBitPos = -1;
static ULONG uExFastRefRefCntBitLen = -1;

BOOLEAN CAhpHasUserReference(const IN ULONG);
ULONG CAhpCheckFlag(const IN ULONG, const IN ULONG, const IN ULONG);

NTSTATUS CAhInit(VOID)
{
	KeEnterCriticalRegion();
	if(bIsControlAreaHiderInitialized == TRUE)
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

	if(!NT_SUCCESS(ShInit()))
	{
		KdPrint(("[DEBUG] WARNING - Segment hider not initialized - segments will not be hidden\n"));
	}

	if(!NT_SUCCESS(FhInit()))
	{
		KdPrint(("[DEBUG] WARNING - File hider not initialized - file objects will not be hidden\n"));
	}

	// add all necessary symbols
	if(	!SymWAddSymbol("_CONTROL_AREA.Segment", -1, -1, -1, -1) ||
		!SymWAddSymbol("_CONTROL_AREA.NumberOfSectionReferences", -1, -1, -1, -1) ||
		!SymWAddSymbol("_CONTROL_AREA.NumberOfMappedViews", -1, -1, -1, -1) ||
		!SymWAddSymbol("_CONTROL_AREA.NumberOfUserReferences", -1, -1, -1, -1) ||
		!SymWAddSymbol("_CONTROL_AREA.FilePointer", -1, -1, -1, -1) ||
		!SymWAddSymbol("_CONTROL_AREA.u", -1, -1, -1, -1)
		)
	{
		KdPrint(("[DEBUG] ERROR - Error while adding necessary symbols - Control Area hider INACTIVE!\n"));
		KeLeaveCriticalRegion();
		return STATUS_NOT_FOUND;
	}

	BOOLEAN bRet = TRUE;
	// above Vista
	if(WinGetMajorVersion() >= 6)
	{
		bRet &= SymWAddSymbol("_EX_FAST_REF.RefCnt", -1, -1, 0, 0);
	}
	else
	{
		bRet &= SymWAddSymbol("_MMSECTION_FLAGS.HadUserReference", -1, -1, 0, 0);
	}

	if(!bRet)
	{
		KdPrint(("[DEBUG] ERROR - Error while adding necessary symbols - Control Area hider INACTIVE!\n"));
		KeLeaveCriticalRegion();
		return STATUS_NOT_FOUND;
	}

	bIsControlAreaHiderInitialized = TRUE;
	KeLeaveCriticalRegion();

	return STATUS_SUCCESS;
}

BOOLEAN CAhInitSymbols(VOID)
{
	ASSERTMSG("Cannot initialize Control Area related symbols because ControlAreaHider is not yet initialized", bIsControlAreaHiderInitialized == TRUE);

	KeEnterCriticalRegion();
	if(bSymbolsInitialized == TRUE)
	{
		KeLeaveCriticalRegion();
		return TRUE;
	}

	if(!ShInitSymbols())
	{
		KdPrint(("[DEBUG] WARNING - Cannot initialize symbols for segment hider - segments won't be hidden!\n"));
	}

	if(!FhInitSymbols())
	{
		KdPrint(("[DEBUG] WARNING - Cannot initialize symbols for the file hider - file objects won't be hidden!\n"));
	}

	BOOLEAN bRet = TRUE;
	bRet =	SymWInitializeOffset(&uCtrlAreaSegmentOffset, "_CONTROL_AREA.Segment") &&
			SymWInitializeOffset(&uCtrlAreaNumberOfSectionReferencesOffset, "_CONTROL_AREA.NumberOfSectionReferences") &&
			SymWInitializeOffset(&uCtrlAreaNumberOfMappedViewsOffset, "_CONTROL_AREA.NumberOfMappedViews") &&
			SymWInitializeOffset(&uCtrlAreaNumberOfUserReferencesOffset, "_CONTROL_AREA.NumberOfUserReferences") &&
			SymWInitializeOffset(&uCtrlAreaFilePointerOffset, "_CONTROL_AREA.FilePointer") &&
			SymWInitializeOffset(&uCtrlAreaFlagsOffset, "_CONTROL_AREA.u");

	// above Vista
	if(WinGetMajorVersion() >= 6)
	{
		bRet &= SymWInitializeBitPosAndLength(&uExFastRefRefCntBitPos, &uExFastRefRefCntBitLen, "_EX_FAST_REF.RefCnt");
	}
	else
	{
		// TODO: probably has to be changed for 64-bit code!!!
		bRet &= SymWInitializeBitPosAndLength(&uCtrlAreaFlagsHadUserReferenceBitPos, &uCtrlAreaFlagsHadUserReferenceBitLen, "_MMSECTION_FLAGS.HadUserReference");
	}

	if(!bRet)
	{
		KdPrint(("[DEBUG] ERROR - Error while initializing offsets or addresses - Control Area hider INACTIVE!\n"));
	}

	bSymbolsInitialized = TRUE;
	KeLeaveCriticalRegion();
	return TRUE;
}

VOID CAhUnInit(VOID)
{
	KeEnterCriticalRegion();
	// check if the engine has been initialized
	if(bIsControlAreaHiderInitialized == FALSE)
	{
		KeLeaveCriticalRegion();
		return;
	}

	// un-initialize all initialized "hiding engines"
	AhUnInit();
	ShUnInit();
	FhUnInit();

	bIsControlAreaHiderInitialized = FALSE;
	KeLeaveCriticalRegion();
}

BOOLEAN CAhShouldHideRegion(IN OUT PSORTED_LIST pList, const IN PVOID pControlArea)
{
	ASSERTMSG("Cannot hide control area because the engine is not yet initialized", bIsControlAreaHiderInitialized == TRUE);
	ASSERTMSG("Internal symbols are not initialized!", bSymbolsInitialized == TRUE);

	if(pList == NULL)
	{
		KdPrint(("[DEBUG] ERROR - Hide list is not initialized -- cannot add control area data to the list\n"));
		return FALSE;
	}

	if(pControlArea == NULL)
	{
		KdPrint(("[DEBUG] WARNING - Control area is NULL -- area will not be hidden...\n"));
		return FALSE;
	}

	BOOLEAN bRet = FALSE;

	// perform additional checking of the control area address
	if(MmIsAddressValid(pControlArea))
	{
		// get number of section references, user-mode references and mapped views (every user-mode reference is a mapped view)
		ULONG uNumOfSectionRefs = *((PULONG) SYMW_MEMBER_FROM_OFFSET(pControlArea, uCtrlAreaNumberOfSectionReferencesOffset, 0));
		ULONG uNumOfMappedViews = *((PULONG) SYMW_MEMBER_FROM_OFFSET(pControlArea, uCtrlAreaNumberOfMappedViewsOffset, 0));
		ULONG uNumOfUserRefs = *((PULONG) SYMW_MEMBER_FROM_OFFSET(pControlArea, uCtrlAreaNumberOfUserReferencesOffset, 0));

		// region/area controlled by this block should be hidden if the region is currently used only by the target process
		// this can be deduced by the number of user references and mapped views
		// if the number of mapped views is equal to user references and equals 1, it is exclusively used by the target process
		// NOTE: this check definitely does not cover all possibilities - it is possible that a target process has multiple references
		// to the same memory region (i.e. EXE, DLL or other)
		// these possiblities are not covered!
		if(uNumOfUserRefs == 1 && uNumOfMappedViews == 1)
		{
			// user references and mapped views are 1 - this region is exclusively used by the target process
			bRet = TRUE;
		}	
	}

	return bRet;
}

BOOLEAN CAhHideControlArea(IN OUT PSORTED_LIST pList, const IN PVOID pControlArea)
{
	ASSERTMSG("Cannot hide control area because the engine is not yet initialized", bIsControlAreaHiderInitialized == TRUE);
	ASSERTMSG("Internal symbols are not initialized!", bSymbolsInitialized == TRUE);

	if(pList == NULL)
	{
		KdPrint(("[DEBUG] ERROR - Hide list is not initialized -- cannot add control area data to the list\n"));
		return FALSE;
	}

	if(pControlArea == NULL)
	{
		KdPrint(("[DEBUG] ERROR - Control area is NULL -- area will not be hidden...\n"));
		return FALSE;
	}

	BOOLEAN bRet = FALSE;

	// hide area only if the address is valid
	if(MmIsAddressValid(pControlArea))
	{
		KdPrint(("[DEBUG] Hiding control area @ 0x%p\n", pControlArea));

		// delete Control Area
		// there are two possible Control Area tags:
		//	MmCi = 0x69436d4d -- image control area
		//	MmCa = 0x61436d4d -- mapped file control area
		// use tag 0x??436d4d, where we don't care about the first byte (use appropriate mask)
		bRet = AhAddAllocation(pList, pControlArea, 0x00436d4d, 0x00FFFFFF);

		// delete the segment as well
		PVOID pSegment = (PVOID) *((PULONG_PTR) SYMW_MEMBER_FROM_OFFSET(pControlArea, uCtrlAreaSegmentOffset, 0));
		bRet &= ShFindHideAddreses(pList, pSegment);

		// hide the file object - this object will probably be hidden already by the ObjectHider (because of the open handles)
		PVOID pFileObject = (PVOID) *((PULONG_PTR) SYMW_MEMBER_FROM_OFFSET(pControlArea, uCtrlAreaFilePointerOffset, 0));

		// Windows Vista and above are using a different type (EX_FAST_REF), do a conversion to object
		if(WinGetMajorVersion() >= 6)
		{
			// the EX_FAST_REF struct is a union of reference count and the object pointer itself
			// the pointer can be obtained by using the formula below
			ULONG_PTR uMask = (1 << uExFastRefRefCntBitLen) - 1;
			pFileObject = (PVOID) ((ULONG_PTR) pFileObject & ~uMask);
		}

		if(pFileObject != NULL)
		{
			bRet &= FhFindHideAddreses(pList, (PFILE_OBJECT) pFileObject);
		}
	}

	return bRet;
}

BOOLEAN CAhpHasUserReference(const IN ULONG uFlags)
{
	if(WinGetMajorVersion() < 6)
	{
		return (BOOLEAN) CAhpCheckFlag(uFlags, uCtrlAreaFlagsHadUserReferenceBitPos, uCtrlAreaFlagsHadUserReferenceBitLen);
	}
	else
	{
		// no such member or equivalent on Vista and above
		return TRUE;
	}
}

ULONG CAhpCheckFlag(const IN ULONG uFlags, const IN ULONG uBitPosition, const IN ULONG uBitLength)
{
	// do some simple error checking
	ASSERTMSG("Bit length cannot be greater than the flags size", uBitLength <= (sizeof(ULONG) * 8));
	ASSERTMSG("Bit position cannot be greater than the flags size", uBitPosition <= (sizeof(ULONG) * 8));
	ASSERTMSG("Bit position or length invalid - the value lies outside of the range of flags \"register\"", (uBitLength + uBitPosition) <= (sizeof(ULONG) * 8));

	return ((uFlags >> uBitPosition) & ((1 << uBitLength) - 1));
}