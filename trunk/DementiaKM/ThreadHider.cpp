#include "ThreadHider.h"
#include "SymbolWrapper.h"
#include "AllocationHider.h"
#include "ObjectHider.h"

// boolean representing status of ThreadHider
static BOOLEAN bIsThreadHiderInitialized = FALSE;

// flag which determines the state of the internal symbols
static BOOLEAN bSymbolsInitialized = FALSE;

// offset of CLIENT_ID member inside ETHREAD structure
static ULONG uETHREADCidOffset = -1;

// offset of UniqueThread (Thread ID) inside CLIENT_ID structure
static ULONG uCIDUniqueThreadOffset = -1;

NTSTATUS ThInit(VOID)
{
	KeEnterCriticalRegion();
	if(bIsThreadHiderInitialized == TRUE)
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

	// initialize object hider -- should be initialized by the process hider
	if(!NT_SUCCESS(OhInit()))
	{
		KdPrint(("[DEBUG] WARNING - Object hider not initialized -- handles and objects will not be hidden...\n"));
	}

	// add all necessary symbols
	if(	!SymWAddSymbol("_ETHREAD.Cid", -1, -1, -1, -1) ||
		!SymWAddSymbol("_CLIENT_ID.UniqueThread", -1, -1, -1, -1))
	{
		KdPrint(("[DEBUG] ERROR - Error while adding necessary symbols - thread hider engine INACTIVE!\n"));
		KeLeaveCriticalRegion();
		return STATUS_NOT_FOUND;
	}

	bIsThreadHiderInitialized = TRUE;
	KeLeaveCriticalRegion();

	return STATUS_SUCCESS;
}

BOOLEAN ThInitSymbols(VOID)
{
	ASSERTMSG("Cannot get thread-related symbols because ThreadHider is not yet initialized", bIsThreadHiderInitialized == TRUE);

	KeEnterCriticalRegion();
	if(bSymbolsInitialized == TRUE)
	{
		KeLeaveCriticalRegion();
		return TRUE;
	}

	// initialize symbols for all "subclasses" (for example, allocation hider, thread hider, etc.)
	// SHOULD ALREADY BEEN DONE BY THE PROCESS HIDER
	BOOLEAN bRet =	AhInitSymbols() &&
					OhInitSymbols();

	bRet &= SymWInitializeOffset(&uETHREADCidOffset, "_ETHREAD.Cid") &&
			SymWInitializeOffset(&uCIDUniqueThreadOffset, "_CLIENT_ID.UniqueThread");

	if(!bRet)
	{
		KdPrint(("[DEBUG] ERROR - Error while initializing offsets or addresses - thread hide engine INACTIVE!\n"));
		KeLeaveCriticalRegion();
		return bRet;
	}

	bSymbolsInitialized = TRUE;
	KeLeaveCriticalRegion();
	return bRet;
}

VOID ThUnInit(VOID)
{
	KeEnterCriticalRegion();
	// check if engine has been initialized
	if(bIsThreadHiderInitialized == FALSE)
	{
		KeLeaveCriticalRegion();
		return;
	}

	AhUnInit();
	OhUnInit();

	bIsThreadHiderInitialized = FALSE;
	KeLeaveCriticalRegion();
}

BOOLEAN ThFindHideAddreses(IN OUT PSORTED_LIST pList, const IN PETHREAD pTargetThread, const IN BOOLEAN bDeleteThreadHandle)
{
	ASSERTMSG("Cannot hide thread because ThreadHider is not yet initialized", bIsThreadHiderInitialized == TRUE);
	ASSERTMSG("Internal symbols are not initialized!", bSymbolsInitialized == TRUE);

	if(pList == NULL)
	{
		KdPrint(("[DEBUG] ERROR - Invalid hide list - pointer to hide list cannot be NULL\n"));
		return FALSE;
	}

	if(pTargetThread == NULL)
	{
		KdPrint(("[DEBUG] ERROR - Invalid thread -- pointer cannot be NULL -- cannot hide thread\n"));
		return FALSE;
	}

	// find and delete Thread allocation -- allocations have "Thre" tag (0xe5726854 hex)
	BOOLEAN bRet = AhAddAllocation(pList, (PVOID) pTargetThread, 0xe5726854);

	// hide thread handles inside PspCidTable if specified by the user-mode program (default: yes)
	if(bDeleteThreadHandle)
	{
		HANDLE hTID = *((PHANDLE) SYMW_MEMBER_FROM_OFFSET(SYMW_MEMBER_FROM_OFFSET(pTargetThread, uETHREADCidOffset, 0), uCIDUniqueThreadOffset, 0));
		bRet &= OhHidePspCidTableHandle(pList, hTID, pTargetThread);
	}

	return bRet;
}