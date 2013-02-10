#include "SegmentHider.h"
#include "SymbolWrapper.h"
#include "AllocationHider.h"

// boolean representing status of SegmentHider
static BOOLEAN bIsSegmentHiderInitialized = FALSE;

// flag which determines the state of the internal symbols
static BOOLEAN bSymbolsInitialized = FALSE;

NTSTATUS ShInit(VOID)
{
	KeEnterCriticalRegion();
	if(bIsSegmentHiderInitialized == TRUE)
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

	// empty for now

	bIsSegmentHiderInitialized = TRUE;
	KeLeaveCriticalRegion();

	return STATUS_SUCCESS;
}

BOOLEAN ShInitSymbols(VOID)
{
	ASSERTMSG("Cannot initialize segment-related symbols because SegmentHider is not yet initialized", bIsSegmentHiderInitialized == TRUE);

	KeEnterCriticalRegion();
	if(bSymbolsInitialized == TRUE)
	{
		KeLeaveCriticalRegion();
		return TRUE;
	}

	// empty for now

	bSymbolsInitialized = TRUE;
	KeLeaveCriticalRegion();
	return TRUE;
}

VOID ShUnInit(VOID)
{
	KeEnterCriticalRegion();
	// check if the engine has been initialized
	if(bIsSegmentHiderInitialized == FALSE)
	{
		KeLeaveCriticalRegion();
		return;
	}

	// un-initialize all initialized "hiding engines"
	AhUnInit();

	bIsSegmentHiderInitialized = FALSE;
	KeLeaveCriticalRegion();
}

BOOLEAN ShFindHideAddreses(IN OUT PSORTED_LIST pList, const IN PVOID pSegment)
{
	ASSERTMSG("Cannot hide segment because the engine is not yet initialized", bIsSegmentHiderInitialized == TRUE);
	ASSERTMSG("Internal symbols are not initialized!", bSymbolsInitialized == TRUE);

	if(pList == NULL)
	{
		KdPrint(("[DEBUG] ERROR - Hide list is not initialized -- cannot add segment data to the list\n"));
		return FALSE;
	}

	if(pSegment == NULL)
	{
		KdPrint(("[DEBUG] ERROR - Segment is NULL, cannot hide the segment!\n"));
		return FALSE;
	}

	BOOLEAN bRet = FALSE;

	KdPrint(("[DEBUG] Hiding segment @ 0x%p\n", pSegment));

	// delete the segment
	// segment has tag MmSt - 0x74536d4d
	// segment can also have MmSm (image map) tag - 0x6d536d4d
	bRet = AhAddAllocation(pList, pSegment, 0x00536d4d, 0x00FFFFFF);

	return bRet;
}