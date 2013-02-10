#include "ProcessHider.h"
#include "ProcessHiderPrivateIncludes.h"
#include "SymbolWrapper.h"
#include "GenericHider.h"
#include "AllocationHider.h"
#include "ThreadHider.h"
#include "ObjectHider.h"
#include "HideEntry.h"
#include "FileHider.h"
#include "VADHider.h"
#include "WinVerProvider.h"

// boolean representing status of ProcessHider
static BOOLEAN bIsProcessHiderInitialized = FALSE;

// flag which determines the state of symbols used in ProcessHider
static BOOLEAN bSymbolsInitialized = FALSE;

// using lookaside list for allocating memory
static NPAGED_LOOKASIDE_LIST TargetProcessLookasideList;

// head of the list of the target processes to be hidden
static LIST_ENTRY TargetProcessListHead;

// mutex which will protect accesses to the list of target processes
static FAST_MUTEX TargetProcessListMutex;

// offset of rundown protect member inside EPROCESS block
static ULONG uEPROCRundownProtectOffset = -1;

// offset of process name inside EPROCESS block
static ULONG uEPROCImageFileNameOffset = -1;

// offset of process links inside EPROCESS block
static ULONG uEPROCActiveProcessLinksOffset = -1;

// offset of session process links (list of all processes in a session) inside EPROCESS block
static ULONG uEPROCSessionProcessLinksOffset = -1;

// offset of process links inside EPROCESS block
static ULONG uEPROCUniqueProcessIdOffset = -1;

// offset of process creation info struct inside EPROCESS block
static ULONG uEPROCSeAuditProcessCreationInfoOffset = -1;

// offset of Job member (pointer) inside EPROCESS block
static ULONG uEPROCJobOffset = -1;

// offset of SectionBaseAddress pointer inside EPROCESS block
// this address is needed for deleting the mapped memory region in VADHider
static ULONG uEPROCSectionBaseAddressOffset = -1;

// offset of VadRoot inside EPROCESS block
static ULONG uEPROCVadRootOffset = -1;

#ifdef _WIN64
// offset of ObjectTable inside EPROCESS block - this member is used only on x64 builds in order to (UNSAFELY) reference process handle table
static ULONG uEPROCObjectTableOffset = -1;
#endif // _WIN64

// address of mutex used for synchronizing the access to ActiveProcessLinks list
static PVOID pPspActiveProcessMutex = NULL;

// head of ActiveProcessLinks list
static PLIST_ENTRY pPsActiveProcessHead = NULL;

// address of PsLookupProcessByProcessId function
static PSLOOKUPPROCESSBYPROCESSID pPsLookupProcessByProcessId = NULL;

// address of PsGetNextProcessThread function
static PSGETNEXTPROCESSTHREAD pPsGetNextProcessThread = NULL;

// address of PsReferenceProcessFilePointer function
static PSREFPROCESSFILEPOINTER pPsReferenceProcessFilePointer = NULL;

// address of ExAcquireRundownProtection (below Vista)
static EXACQUIRERUNDOWNPROTECTION pExAcquireRundownProtection = NULL;

// address of ExAcquireRundownProtectionEx (below Vista)
static EXACQUIRERUNDOWNPROTECTIONEX pExAcquireRundownProtectionEx = NULL;

// address of ExReleaseRundownProtection (below Vista)
static EXRELEASERUNDOWNPROTECTION pExReleaseRundownProtection = NULL;

// address of ExReleaseRundownProtectionEx (below Vista)
static EXRELEASERUNDOWNPROTECTIONEX pExReleaseRundownProtectionEx = NULL;

VOID PhpAcquireProcessListProtection(VOID);
VOID PhpReleaseProcessListProtection(VOID);
PETHREAD PhpPsGetNextProcessThread(IN PEPROCESS, IN PETHREAD);

// returns the array of pointers to target process EPROCESS blocks
// size of this array is returned in the second argument
// caller is responsible for memory deallocation!
PEPROCESS * PhpFindTargetProcesses(const IN PCHAR, OUT PULONG);
BOOLEAN PhpAddTargetProcesses(const IN PEPROCESS *, const IN ULONG, const IN PTARGET_OBJECT);
BOOLEAN PhpAddTargetProcessToList(const IN PEPROCESS, const IN PTARGET_OBJECT);

BOOLEAN PhpHideThreads(IN OUT PSORTED_LIST, const IN PEPROCESS, const IN BOOLEAN);
BOOLEAN PhpHideHandles(IN OUT PSORTED_LIST, const IN PEPROCESS);
BOOLEAN PhpHideImageFile(IN OUT PSORTED_LIST, const IN PEPROCESS);
BOOLEAN PhpHideProcessJob(IN OUT PSORTED_LIST, const IN PEPROCESS);
BOOLEAN PhpHideProcessVads(IN OUT PSORTED_LIST, const IN PEPROCESS);
BOOLEAN PhpHideProcessAuditingInfo(IN OUT PSORTED_LIST, const IN PEPROCESS);

// this function is actually a simple strncmp function with n = 16 (length of the ImageFileName member in EPROCESS)
// RtlEqualMemory was used before, but it failed on Windows 7 (the whole 16-byte buffer was different on Windows 7)
// on Windows XP it worked fine
BOOLEAN PhpProcessNamesEqual(IN PCHAR, IN PCHAR);

// this function is simple (read: stupid) implementation of realloc
// be careful when using this function, since it could lead to all sorts of problems
PVOID PhpRealloc(IN PVOID, const IN ULONG, const IN ULONG, const IN POOL_TYPE);

NTSTATUS PhInit(VOID)
{
	KeEnterCriticalRegion();
	if(bIsProcessHiderInitialized == TRUE)
	{
		KeLeaveCriticalRegion();
		return STATUS_SUCCESS;
	}

	// initialize allocation hider if it has not been initialized
	if(!AhIsInitialized())
	{
		AhInit();
	}

	if(!NT_SUCCESS(ThInit()))
	{
		KdPrint(("[DEBUG] WARNING - Thread engine not initialized -- threads will not be hidden...\n"));
	}

	if(!NT_SUCCESS(OhInit()))
	{
		KdPrint(("[DEBUG] WARNING - Object hider not initialized -- handles and objects will not be hidden...\n"));
	}

	if(!NT_SUCCESS(FhInit()))
	{
		KdPrint(("[DEBUG] WARNING - File hider not initialized -- process image file object will not be hidden...\n"));
	}

	if(!NT_SUCCESS(VADhInit()))
	{
		KdPrint(("[DEBUG] WARNING - VAD hider not initialized -- private memory ranges of the target process will not be hidden...\n"));
	}

	BOOLEAN bRet = TRUE;

	if(WinGetMajorVersion() >= 6)
	{
		bRet = SymWAddSymbol("PspActiveProcessLock", -1, -1, -1, -1) &&
			   SymWAddSymbol("ExAcquireRundownProtectionEx", -1, -1, -1, -1) &&
			   SymWAddSymbol("ExReleaseRundownProtectionEx", -1, -1, -1, -1);
	}
	else
	{
		bRet =	SymWAddSymbol("PspActiveProcessMutex", -1, -1, -1, -1) &&
				SymWAddSymbol("ExAcquireRundownProtection", -1, -1, -1, -1) &&
				SymWAddSymbol("ExReleaseRundownProtection", -1, -1, -1, -1);
	}

	// add all symbols that are necessary for proper functioning of the driver and the hiding algorithms
	if(!bRet ||
		!SymWAddSymbol("PsLookupProcessByProcessId", -1, -1, -1, -1) ||
		!SymWAddSymbol("PsGetNextProcessThread", -1, -1, -1, -1) ||
		!SymWAddSymbol("PsReferenceProcessFilePointer", -1, -1, -1, -1) ||
		!SymWAddSymbol("PsActiveProcessHead", -1, -1, -1, -1) ||
		!SymWAddSymbol("_EPROCESS.RundownProtect", -1, -1, -1, -1) ||
		!SymWAddSymbol("_EPROCESS.ActiveProcessLinks", -1, -1, -1, -1) ||
		!SymWAddSymbol("_EPROCESS.SessionProcessLinks", -1, -1, -1, -1) ||
		!SymWAddSymbol("_EPROCESS.UniqueProcessId", -1, -1, -1, -1) ||
		!SymWAddSymbol("_EPROCESS.ImageFileName", -1, -1, -1, -1) ||
		!SymWAddSymbol("_EPROCESS.SeAuditProcessCreationInfo", -1, -1, -1, -1) ||
		!SymWAddSymbol("_EPROCESS.Job", -1, -1, -1, -1) ||
#ifdef _WIN64
		!SymWAddSymbol("_EPROCESS.ObjectTable", -1, -1, -1, -1) ||
#endif // _WIN64
		!SymWAddSymbol("_EPROCESS.SectionBaseAddress", -1, -1, -1, -1) ||
		!SymWAddSymbol("_EPROCESS.VadRoot", -1, -1, -1, -1)
	   )
	{
		KdPrint(("[DEBUG] ERROR - Error while adding necessary symbols - process engine INACTIVE!\n"));
		KeLeaveCriticalRegion();
		return STATUS_NOT_FOUND;
	}

	ASSERTMSG("Fast mutex must be initialized at or below DISPATCH_LEVEL", KeGetCurrentIrql() <= DISPATCH_LEVEL);

	// initialize all required data structures
	ExInitializeNPagedLookasideList(&TargetProcessLookasideList,		// list to initialize
									NULL,								// allocate function - OS supplied
									NULL,								// free function - OS supplied
									0,									// flags - always zero
									sizeof(PROC_HIDE),					// size of each entry to be allocated
									TAG_TARGET_PROCESS_LOOKASIDE,		// Tprl(ookaside) tag
									0									// depth - always zero
									);
	InitializeListHead(&TargetProcessListHead);
	ExInitializeFastMutex(&TargetProcessListMutex);

	bIsProcessHiderInitialized = TRUE;
	KeLeaveCriticalRegion();

	return STATUS_SUCCESS;
}

BOOLEAN PhInitSymbols(VOID)
{
	ASSERTMSG("Critical region entering and mutex acquiring must occur at or below APC_LEVEL", KeGetCurrentIrql() <= APC_LEVEL);
	ASSERTMSG("Cannot get process-related symbols if engine is not yet initialized", bIsProcessHiderInitialized == TRUE);

	KeEnterCriticalRegion();
	if(bSymbolsInitialized == TRUE)
	{
		KeLeaveCriticalRegion();
		return TRUE;
	}

	// initialize symbols for all "subclasses" (for example, allocation hider, thread hider, etc.)
	BOOLEAN bRet =	AhInitSymbols() &&
					ThInitSymbols() &&
					OhInitSymbols() &&
					FhInitSymbols() &&
					VADhInitSymbols();

	// on Vista and Windows 7, process mutex is actually lock
	if(WinGetMajorVersion() >= 6)
	{
		bRet &= SymWInitializeAddress(&pPspActiveProcessMutex, "PspActiveProcessLock", FALSE) &&
				SymWInitializeAddress((PVOID *) &pExAcquireRundownProtectionEx, "ExAcquireRundownProtectionEx", TRUE) &&
				SymWInitializeAddress((PVOID *) &pExReleaseRundownProtectionEx, "ExReleaseRundownProtectionEx", TRUE);
	}
	else
	{
		bRet &= SymWInitializeAddress(&pPspActiveProcessMutex, "PspActiveProcessMutex", FALSE) &&
				SymWInitializeAddress((PVOID *) &pExAcquireRundownProtection, "ExAcquireRundownProtection", TRUE) &&
				SymWInitializeAddress((PVOID *) &pExReleaseRundownProtection, "ExReleaseRundownProtection", TRUE);
	}

	bRet &= SymWInitializeAddress((PVOID *) &pPsActiveProcessHead, "PsActiveProcessHead", FALSE) &&
			SymWInitializeAddress((PVOID *) &pPsLookupProcessByProcessId, "PsLookupProcessByProcessId", TRUE) &&
			SymWInitializeAddress((PVOID *) &pPsGetNextProcessThread, "PsGetNextProcessThread", TRUE) &&
			SymWInitializeAddress((PVOID *) &pPsReferenceProcessFilePointer, "PsReferenceProcessFilePointer", TRUE) &&
			SymWInitializeOffset(&uEPROCRundownProtectOffset, "_EPROCESS.RundownProtect") &&
			SymWInitializeOffset(&uEPROCImageFileNameOffset, "_EPROCESS.ImageFileName") &&
			SymWInitializeOffset(&uEPROCUniqueProcessIdOffset, "_EPROCESS.UniqueProcessId") &&
			SymWInitializeOffset(&uEPROCActiveProcessLinksOffset, "_EPROCESS.ActiveProcessLinks") &&
			SymWInitializeOffset(&uEPROCSessionProcessLinksOffset, "_EPROCESS.SessionProcessLinks") &&
			SymWInitializeOffset(&uEPROCSeAuditProcessCreationInfoOffset, "_EPROCESS.SeAuditProcessCreationInfo") &&
			SymWInitializeOffset(&uEPROCJobOffset, "_EPROCESS.Job") &&
#ifdef _WIN64
			SymWInitializeOffset(&uEPROCObjectTableOffset, "_EPROCESS.ObjectTable") &&
#endif // _WIN64
			SymWInitializeOffset(&uEPROCSectionBaseAddressOffset, "_EPROCESS.SectionBaseAddress") &&
			SymWInitializeOffset(&uEPROCVadRootOffset, "_EPROCESS.VadRoot");

	if(!bRet)
	{
		KdPrint(("[DEBUG] ERROR - Error while initializing offsets or addresses - process engine INACTIVE!\n"));
		KeLeaveCriticalRegion();
		return bRet;
	}

	bSymbolsInitialized = TRUE;
	KeLeaveCriticalRegion();

	return bRet;
}

VOID PhUnInit(VOID)
{
	ASSERTMSG("Critical region and lookaside list deletion must occur at or below APC_LEVEL", KeGetCurrentIrql() <= APC_LEVEL);

	KeEnterCriticalRegion();
	// check if engine has been initialized
	if(bIsProcessHiderInitialized == FALSE)
	{
		KeLeaveCriticalRegion();
		return;
	}

	// uninitialize all engines -- if particular engine is already uninitialized, it won't be
	// uninitialized twice
	AhUnInit();
	ThUnInit();
	OhUnInit();
	FhUnInit();
	VADhUnInit();

	// free all entries in lookaside list
	ExDeleteNPagedLookasideList(&TargetProcessLookasideList);

	bIsProcessHiderInitialized = FALSE;
	KeLeaveCriticalRegion();
}

BOOLEAN PhAddTargetProcess(const IN PTARGET_OBJECT pTargetObject)
{
	ASSERTMSG("Cannot add process if hider is not yet initialized", bIsProcessHiderInitialized == TRUE);
	ASSERTMSG("Internal symbols are not initialized!", bSymbolsInitialized == TRUE);

	if(pTargetObject == NULL)
	{
		KdPrint(("[DEBUG] ERROR - Invalid hide object passed - pointer to hide object cannot be NULL\n"));
		return FALSE;
	}
	BOOLEAN bRet = TRUE;

	PCHAR szProcessName = pTargetObject->szObjectName;
	ULONG_PTR uPID = pTargetObject->uPID;

	// find target process EPROCESS block
	PEPROCESS pTargetProc = NULL;
	ULONG uTargetProcessCount = 0;
	PEPROCESS *pTargetProcessArray = NULL;

	if(uPID != -1)
	{
		if(!NT_SUCCESS(pPsLookupProcessByProcessId((HANDLE) uPID, &pTargetProc)))
		{
			KdPrint(("[DEBUG] WARNING - Process lookup for process with PID %d failed... Trying name lookup\n", uPID));
		
			// get array of pointers to EPROCESS blocks of target processes
			pTargetProcessArray = PhpFindTargetProcesses(szProcessName, &uTargetProcessCount);
			
			// if the array retrieval failed, exit
			if(pTargetProcessArray == NULL)
			{
				KdPrint(("[DEBUG] ERROR - Target processes could not be obtained - exiting...\n"));
				return FALSE;
			}

			// add all processes to the internal list
			bRet = PhpAddTargetProcesses(pTargetProcessArray, uTargetProcessCount, pTargetObject);
		}
		else
		{
			// process found by PID -- add directly to the list
			bRet = PhpAddTargetProcessToList(pTargetProc, pTargetObject);
			KdPrint(("[DEBUG] Found process %s with PID = %d. EPROCESS @ 0x%x\n", szProcessName, uPID, pTargetProc));
			ObDereferenceObject(pTargetProc);
		}
	}
	else
	{
		// PID not specified, first get array of EPROCESS pointers
		pTargetProcessArray = PhpFindTargetProcesses(szProcessName, &uTargetProcessCount);

		// if the array retrieval failed, exit
		if(pTargetProcessArray == NULL)
		{
			KdPrint(("[DEBUG] ERROR - Target processes could not be obtained - exiting...\n"));
			return FALSE;
		}

		// add all processes to the internal list
		bRet = PhpAddTargetProcesses(pTargetProcessArray, uTargetProcessCount, pTargetObject);
	}

	// delete the memory allocated for the internal process list!
	if(pTargetProcessArray != NULL)
	{
		ExFreePoolWithTag(pTargetProcessArray, TAG_TARGET_PEPROCESS_ARRAY);
	}

	return bRet;
}

BOOLEAN PhFindHideAddreses(IN OUT PSORTED_LIST pList)
{
	ASSERTMSG("Cannot add process if hider is not yet initialized", bIsProcessHiderInitialized == TRUE);
	ASSERTMSG("Internal symbols are not initialized!", bSymbolsInitialized == TRUE);

	if(pList == NULL)
	{
		KdPrint(("[DEBUG] ERROR - Invalid hide list - pointer to hide list cannot be NULL\n"));
		return FALSE;
	}

	BOOLEAN bRet = TRUE;

	PLIST_ENTRY targetProcessEntry = TargetProcessListHead.Flink;

	// iterate through the target process list
	ASSERTMSG("Fast mutex acquire must occur at or below APC_LEVEL", KeGetCurrentIrql() <= APC_LEVEL);
	ExAcquireFastMutex(&TargetProcessListMutex);
	while(targetProcessEntry != &TargetProcessListHead)
	{
		PPROC_HIDE pTargetProcessEntry = CONTAINING_RECORD(targetProcessEntry, PROC_HIDE, ListEntry);
		PEPROCESS pTargetProc = (PEPROCESS) pTargetProcessEntry->pEPROCESS;
		
		// find allocation first - PROCESS allocation has "Proc" tag (0xe36f7250 hex)
		bRet &= AhAddAllocation(pList, (PVOID) pTargetProc, 0xe36f7250);

		// find process list flink/blink pointers
		PhpAcquireProcessListProtection();
		
		// modify process list links
		GhModifyListFlinkBlinkPointers(pList, (PVOID) pTargetProc, uEPROCActiveProcessLinksOffset);

		KdPrint(("[DEBUG] Deleting session process list links...\n"));
		// modify session process list links
		GhModifyListFlinkBlinkPointers(pList, (PVOID) pTargetProc, uEPROCSessionProcessLinksOffset);

		PhpReleaseProcessListProtection();		

		// check if process threads need to be hidden (default: yes)
		if(pTargetProcessEntry->targetObject.bHideThreads)
		{
			bRet &= PhpHideThreads(pList, pTargetProc, pTargetProcessEntry->targetObject.bHideHandles);
		}

		// check if process handle table should be hidden, along with all opened handles (default: yes)
		if(pTargetProcessEntry->targetObject.bHideHandles)
		{
			bRet &= PhpHideHandles(pList, pTargetProc);
		}

		// check if process image file object should be hidden (default: yes)
		if(pTargetProcessEntry->targetObject.bHideImageFileObj)
		{
			bRet &= PhpHideImageFile(pList, pTargetProc);
		}

		// check if process job should be modified (default: yes)
		// applicable only if process belongs to a job
		if(pTargetProcessEntry->targetObject.bHideJob)
		{
			bRet &= PhpHideProcessJob(pList, pTargetProc);
		}

		// check if process private memory ranges should be hidden (default: yes)
		if(pTargetProcessEntry->targetObject.bHideVad)
		{
			bRet &= PhpHideProcessVads(pList, pTargetProc);
		}
		
		// delete process auditing information
		bRet &= PhpHideProcessAuditingInfo(pList, pTargetProc);

		// all addresses have been collected - it's safe to unlock the process
		// even if the process is killed afterwards, the addresses are only modified inside the write buffer
		// there is no danger of crash, only a slightly corrupted buffer in the worst case
		//PhpUnlockProcess(pTargetProc);

		// move to next target process
		targetProcessEntry = targetProcessEntry->Flink;
	}

	// wanted symbol entry is not present in the list, return NULL
	ASSERTMSG("Fast mutex release must occur at APC_LEVEL", KeGetCurrentIrql() == APC_LEVEL);
	ExReleaseFastMutex(&TargetProcessListMutex);
	return bRet;
}

BOOLEAN PhLockProcess(IN PEPROCESS pProcess)
{
	ASSERTMSG("Cannot add process if hider is not yet initialized", bIsProcessHiderInitialized == TRUE);
	ASSERTMSG("Internal symbols are not initialized!", bSymbolsInitialized == TRUE);

	if(pProcess == NULL)
	{
		KdPrint(("[DEBUG] ERROR - Cannot lock NULL process\n"));
		return FALSE;
	}

	BOOLEAN bRet = TRUE;
	PVOID pProcessRundownProtect = (PVOID) *((PULONG_PTR)SYMW_MEMBER_FROM_OFFSET(pProcess, uEPROCRundownProtectOffset, 0));

	ASSERTMSG("Fast mutex/pushlock acquire must occur at or below APC_LEVEL", KeGetCurrentIrql() <= APC_LEVEL);
	// if OS is Vista, 2008, Windows 7 or above
	if(WinGetMajorVersion() >= 6)
	{
		bRet = pExAcquireRundownProtectionEx(pProcessRundownProtect);
	}
	else
	{
		bRet = pExAcquireRundownProtection(pProcessRundownProtect);
	}

	return bRet;
}

VOID PhUnlockProcess(IN PEPROCESS pProcess)
{
	ASSERTMSG("Cannot add process if hider is not yet initialized", bIsProcessHiderInitialized == TRUE);
	ASSERTMSG("Internal symbols are not initialized!", bSymbolsInitialized == TRUE);

	if(pProcess == NULL)
	{
		KdPrint(("[DEBUG] ERROR - Cannot lock NULL process\n"));
		return;
	}

	PVOID pProcessRundownProtect = (PVOID) *((PULONG_PTR)SYMW_MEMBER_FROM_OFFSET(pProcess, uEPROCRundownProtectOffset, 0));

	ASSERTMSG("Fast mutex/pushlock acquire must occur at or below APC_LEVEL", KeGetCurrentIrql() <= APC_LEVEL);
	// if OS is Vista, 2008, Windows 7 or above
	if(WinGetMajorVersion() >= 6)
	{
		pExReleaseRundownProtectionEx(pProcessRundownProtect);
	}
	else
	{
		pExReleaseRundownProtection(pProcessRundownProtect);
	}
}

VOID PhpAcquireProcessListProtection(VOID)
{
	ASSERTMSG("Fast mutex/pushlock acquire must occur at or below APC_LEVEL", KeGetCurrentIrql() <= APC_LEVEL);

	// if we're on Vista or Windows 7
	if(WinGetMajorVersion() >= 6)
	{
		KeEnterCriticalRegion();
		ExfAcquirePushLockShared((PEX_PUSH_LOCK) pPspActiveProcessMutex);
	}
	else
	{
		ExAcquireFastMutex((PFAST_MUTEX) pPspActiveProcessMutex);
	}
}

VOID PhpReleaseProcessListProtection(VOID)
{
	// fast mutex or pushlock release
	// if we're on Vista or Windows 7
	if(WinGetMajorVersion() >= 6)
	{
		ASSERTMSG("Pushlock release must occur at or below APC_LEVEL", KeGetCurrentIrql() <= APC_LEVEL);
		ExfReleasePushLock((PEX_PUSH_LOCK) pPspActiveProcessMutex);
		KeLeaveCriticalRegion();
	}	
	else
	{
		ASSERTMSG("Fast mutex release must occur at APC_LEVEL", KeGetCurrentIrql() == APC_LEVEL);
		ExReleaseFastMutex((PFAST_MUTEX) pPspActiveProcessMutex);
	}
}

PETHREAD PhpPsGetNextProcessThread(IN PEPROCESS pTargetProcess, IN PETHREAD pThread)
{
	ASSERTMSG("Pointer to target process cannot be NULL!", pTargetProcess != NULL);

	PETHREAD pNewETHREAD = NULL;

	// if OS is Vista, 2008, Windows 7 or above
	if(WinGetMajorVersion() >= 6)
	{
#ifdef _WIN64
		// on x64, only one calling convention is used so we can just call the function (argument will be passed in ECX register)
		pNewETHREAD = pPsGetNextProcessThread(pTargetProcess, pThread);
#else // _WIN32
		PSGETNEXTPROCESSTHREADVISTA pPsGetNextProcessThreadVista = (PSGETNEXTPROCESSTHREADVISTA) pPsGetNextProcessThread;

		// Microsoft changed calling convention for PsGetNextProcessThread in Vista and greater versions.
		// it looks like they're using some kind of mixed calling convention, because EPROCESS parameter is being passed 
		// through the eax register -- we must manually prepare arguments for this function in Vista and greater.
		__asm
		{
				push eax
				mov eax, pTargetProcess
				push pThread
				call pPsGetNextProcessThreadVista
				mov pNewETHREAD, eax
				pop eax
		}
#endif // _WIN64
	}
	// if OS is XP or lower, we can just call PsGetNextProcessThread method
	else
	{
		pNewETHREAD = pPsGetNextProcessThread(pTargetProcess, pThread);
	}

	return pNewETHREAD;
}

PEPROCESS * PhpFindTargetProcesses(const IN PCHAR szTargetProcName, OUT PULONG puTargetProcessCount)
{
	ASSERTMSG("Fast mutex or pushlock acquire/release must occur at or below APC_LEVEL", KeGetCurrentIrql() <= APC_LEVEL);
	ASSERTMSG("Pointer to target process name cannot be NULL", szTargetProcName != NULL);
	ASSERTMSG("Pointer to size of the target process array cannot be NULL", puTargetProcessCount != NULL);

	// traverse the list of processes
	BOOLEAN bProcessExists = FALSE;

	// allocate memory for the target process pointer array
	// initial allocation is 100*sizeof(PEPROCESS), but it can be deallocated during the iteration!
	// non-paged pool can be used, since the operations on the array will be performed with IRQL <= APC_LEVEL
	ULONG uArraySize = 100;
	ULONG uTargetProcessCount = 0;
	PVOID pTargetProcessesPointerArray = ExAllocatePoolWithTag(NonPagedPool, uArraySize * sizeof(PEPROCESS), TAG_TARGET_PEPROCESS_ARRAY);

	if(pTargetProcessesPointerArray == NULL)
	{
		KdPrint(("[DEBUG] ERROR - Fatal error -- could not allocate memory for the target processes array!\n"));
		return NULL;
	}

	// zero-out the buffer
	RtlZeroMemory(pTargetProcessesPointerArray, uArraySize * sizeof(PEPROCESS));

	// acquire process list mutex/pushlock and traverse it
	PhpAcquireProcessListProtection();
	PLIST_ENTRY pProcListStart = pPsActiveProcessHead;
	PLIST_ENTRY pProcListEntry = pProcListStart->Flink;
	PCHAR szProcessName = NULL;
	PULONG_PTR pPID = NULL;

	do 
	{
		// obtain ImageFileName and PID members from the current EPROCESS structure
		szProcessName = (PCHAR) SYMW_MEMBER_FROM_OFFSET(SYMW_MEMBER_FROM_OFFSET(pProcListEntry, 0, uEPROCActiveProcessLinksOffset), uEPROCImageFileNameOffset, 0);
		pPID = (PULONG_PTR) SYMW_MEMBER_FROM_OFFSET(SYMW_MEMBER_FROM_OFFSET(pProcListEntry, 0, uEPROCActiveProcessLinksOffset), uEPROCUniqueProcessIdOffset, 0);

		// compare only 16 bytes -- this is the length of ImageFileName field inside EPROCESS structure
		if(PhpProcessNamesEqual(szTargetProcName, szProcessName))
		{
			// if target process has been found, add it to the target array
			PEPROCESS pTargetProc = (PEPROCESS) SYMW_MEMBER_FROM_OFFSET(pProcListEntry, 0, uEPROCActiveProcessLinksOffset);

			// lock the process and unlock it after all addresses to be hidden have been collected
			//PhpLockProcess(pTargetProc);

			KdPrint(("[DEBUG] Found process %s with PID = %d. EPROCESS @ 0x%p\n", szProcessName, *pPID, pTargetProc));
			
			*((PEPROCESS *) pTargetProcessesPointerArray + uTargetProcessCount) = pTargetProc;
			uTargetProcessCount++;

			// check if there is no space left in the buffer and reallocate it if needed
			if(uTargetProcessCount == uArraySize)
			{
				// increase array size for 100 more processes
				pTargetProcessesPointerArray = PhpRealloc(	pTargetProcessesPointerArray, 
															uArraySize * sizeof(PEPROCESS), 
															uArraySize * sizeof(PEPROCESS) + 100, 
															NonPagedPool);
				uArraySize += 100;
			}
		}

		// move to the next entry in the process table list
		pProcListEntry = pProcListEntry->Flink;
	} while(pProcListEntry != pProcListStart);

	PhpReleaseProcessListProtection();

	// if no target processes have been found, notify the caller
	if(uTargetProcessCount == 0)
	{
		*puTargetProcessCount = 0;

		// release the memory
		ExFreePoolWithTag(pTargetProcessesPointerArray, TAG_TARGET_PEPROCESS_ARRAY);
		return NULL;
	}

	*puTargetProcessCount = uTargetProcessCount;
	return (PEPROCESS *) pTargetProcessesPointerArray;
}

BOOLEAN PhpAddTargetProcesses(const IN PEPROCESS *pTargetProcessArray, const IN ULONG uTargetProcessCount, const IN PTARGET_OBJECT pTargetObject)
{
	ASSERTMSG("Passed pointer to PEPROCESS array is NULL", pTargetProcessArray != NULL);
	ASSERTMSG("Number of target processes is zero - are you sure this is OK?", uTargetProcessCount != 0);
	ASSERTMSG("Passed pointer to target object is NULL", pTargetObject != NULL);

	BOOLEAN bRet = TRUE;

	// traverse the entire list and add processes to the internal list
	for(ULONG i = 0; i < uTargetProcessCount; i++)
	{
		bRet &= PhpAddTargetProcessToList(pTargetProcessArray[i], pTargetObject);
	}

	return bRet;
}

BOOLEAN PhpAddTargetProcessToList(const IN PEPROCESS pTargetProcess, const IN PTARGET_OBJECT pTargetObject)
{
	ASSERTMSG("Passed pointer to EPROCESS block is NULL", pTargetProcess != NULL);
	ASSERTMSG("Passed pointer to target object is NULL", pTargetObject != NULL);

	PPROC_HIDE pTargetProcessEntry = (PPROC_HIDE) ExAllocateFromNPagedLookasideList(&TargetProcessLookasideList);
	if(pTargetProcessEntry == NULL)
	{
		KdPrint(("[DEBUG] ERROR - Not enough memory in lookaside list to allocate new target process entry...\n"));
		return FALSE;
	}

	// add new entry to the list (thread-safely)
	pTargetProcessEntry->pEPROCESS = pTargetProcess;
	// copy entire target object - this ensures that all members will be copied, even if they are changed inside header files
	RtlCopyMemory(&pTargetProcessEntry->targetObject, pTargetObject, sizeof(TARGET_OBJECT));

	ASSERTMSG("Fast mutex acquire/release must occur at or below APC_LEVEL", KeGetCurrentIrql() <= APC_LEVEL);
	ExAcquireFastMutex(&TargetProcessListMutex);
	InsertHeadList(&TargetProcessListHead, &pTargetProcessEntry->ListEntry);
	ExReleaseFastMutex(&TargetProcessListMutex);

	return TRUE;	
}

BOOLEAN PhpHideThreads(IN OUT PSORTED_LIST pList, const IN PEPROCESS pTargetProcess, const IN BOOLEAN bDeleteThreadHandle)
{
	ASSERTMSG("Passed pointer to hide list is NULL", pList != NULL);
	ASSERTMSG("Passed pointer to target process is NULL", pTargetProcess != NULL);
	ASSERTMSG("PsGetNextProcessThread must occur at or below APC_LEVEL", KeGetCurrentIrql() <= APC_LEVEL);

	BOOLEAN bRet = TRUE;

	// get first process thread
	PETHREAD pThread = PhpPsGetNextProcessThread(pTargetProcess, NULL);
	while (pThread)
	{
		KdPrint(("[DEBUG] Found thread @ 0x%x -- hiding data...\n", pThread));
		
		// hide thread allocation and other data (thread handles inside PspCidTable if specified by the user-mode program (default: yes))
		bRet = ThFindHideAddreses(pList, pThread, bDeleteThreadHandle);
		pThread = PhpPsGetNextProcessThread(pTargetProcess, pThread);
	}

	return bRet;
}

BOOLEAN PhpHideHandles(IN OUT PSORTED_LIST pList, const IN PEPROCESS pTargetProcess)
{
	ASSERTMSG("Passed pointer to hide list is NULL", pList != NULL);
	ASSERTMSG("Passed pointer to target process is NULL", pTargetProcess != NULL);

	BOOLEAN bRet = TRUE;

	HANDLE hPID = *((PHANDLE) SYMW_MEMBER_FROM_OFFSET(pTargetProcess, uEPROCUniqueProcessIdOffset, 0));

	// hide process handle table, modify all open handles and fix the entries in the PspCidTable
	bRet =	OhHideTargetProcessHandleTable(pList, pTargetProcess) &&
			OhHideProcessHandles(pList, pTargetProcess) &&
			OhHidePspCidTableHandle(pList, hPID, pTargetProcess);

	// additionally, remove the entry from the csrss handle table that points to our target process
	ULONG uTargetProcessCount = 0;
	PEPROCESS *pTargetProcessArray = PhpFindTargetProcesses("csrss.exe", &uTargetProcessCount);
	
	// if the call failed, exit
	if(pTargetProcessArray == NULL)
	{
		KdPrint(("[DEBUG] WARNING - Could not obtain pointer to csrss.exe process - handles inside the csrss handle table won't be hidden...\n"));
		return FALSE;
	}

	for(ULONG i = 0; i < uTargetProcessCount; i++)
	{
		// hide csrss handle table entries
		bRet &= OhHideCsrssProcessHandles(pList, pTargetProcessArray[i], pTargetProcess);
	}

	// delete memory allocated for the process array
	ExFreePoolWithTag(pTargetProcessArray, TAG_TARGET_PEPROCESS_ARRAY);

	return bRet;
}

BOOLEAN PhpHideImageFile(IN OUT PSORTED_LIST pList, const IN PEPROCESS pTargetProcess)
{
	ASSERTMSG("Passed pointer to hide list is NULL", pList != NULL);
	ASSERTMSG("Passed pointer to target process is NULL", pTargetProcess != NULL);

	PFILE_OBJECT pFileObject = NULL;
	if(pPsReferenceProcessFilePointer == NULL)
	{
		KdPrint(("[DEBUG] WARNING - PsReferenceProcessFilePointer function is not initialized - cannot hide process image file object...\n"));
		return FALSE;
	}

	if(!NT_SUCCESS(pPsReferenceProcessFilePointer(pTargetProcess, &pFileObject)))
	{
		KdPrint(("[DEBUG] WARNING - PsReferenceProcessFilePointer failed - cannot hide process image file object...\n"));
		return FALSE;
	}

	KdPrint(("[DEBUG] Found image file object @ %p -- hiding data...\n", pFileObject));

	BOOLEAN bRet = FhFindHideAddreses(pList, pFileObject);

	// must dereference the file object - otherwise a pointer to the object would still be visible, but from Dementia process:)
	ObDereferenceObject(pFileObject);

	return bRet;
}

BOOLEAN PhpHideProcessJob(IN OUT PSORTED_LIST pList, const IN PEPROCESS pTargetProcess)
{
	ASSERTMSG("Passed pointer to hide list is NULL", pList != NULL);
	ASSERTMSG("Passed pointer to target process is NULL", pTargetProcess != NULL);

	BOOLEAN bRet = TRUE;
	PVOID pJob = (PVOID) *((PULONG_PTR) SYMW_MEMBER_FROM_OFFSET(pTargetProcess, uEPROCJobOffset, 0));

	// job hiding will be performed only if job exists
	if(pJob != NULL)
	{
		// TODO!!!!!
	}

	return bRet;
}

BOOLEAN PhpHideProcessVads(IN OUT PSORTED_LIST pList, const IN PEPROCESS pTargetProcess)
{
	ASSERTMSG("Passed pointer to hide list is NULL", pList != NULL);
	ASSERTMSG("Passed pointer to target process is NULL", pTargetProcess != NULL);

	BOOLEAN bRet = TRUE;
	PVOID pVadRoot = NULL;
	
	// on Vista and above VadRoot is actually a MM_AVL_TABLE structure, NOT a pointer!
	if(WinGetMajorVersion() >= 6)
	{
		pVadRoot = (PVOID) SYMW_MEMBER_FROM_OFFSET(pTargetProcess, uEPROCVadRootOffset, 0);
	}
	else
	{
		// on Windows XP and below, VadRoot is pointer to MM_VAD
		pVadRoot = (PVOID) *((PULONG_PTR) SYMW_MEMBER_FROM_OFFSET(pTargetProcess, uEPROCVadRootOffset, 0));
	}

	PVOID pProcessSectionBase = (PVOID) *((PULONG_PTR) SYMW_MEMBER_FROM_OFFSET(pTargetProcess, uEPROCSectionBaseAddressOffset, 0));

	// check if VAD root pointer is valid, and perform hiding of VADs
	if(pVadRoot != NULL)
	{
		bRet = VADhFindHideAddreses(pList, pVadRoot, pTargetProcess, pProcessSectionBase);
	}

	return bRet;
}

BOOLEAN PhpHideProcessAuditingInfo(IN OUT PSORTED_LIST pList, const IN PEPROCESS pTargetProcess)
{
	ASSERTMSG("Passed pointer to hide list is NULL", pList != NULL);
	ASSERTMSG("Passed pointer to target process is NULL", pTargetProcess != NULL);

	PVOID pImageFileName = (PVOID) *((PULONG_PTR) SYMW_MEMBER_FROM_OFFSET(pTargetProcess, uEPROCSeAuditProcessCreationInfoOffset, 0));

	BOOLEAN bRet = TRUE;

	// check if process auditing is enabled
	if(pImageFileName != NULL)
	{
		KdPrint(("[DEBUG] Process auditing info found @ 0x%x\n", (ULONG) pImageFileName));
		GhAddUnicodeStringAddress(pList, pImageFileName);

		// delete the entire allocation - this deletion also wipes-out the UNICODE_STRING - previous call is technically unnecessary

		ULONG uAllocationTag = 0;

		if(WinGetMajorVersion() >= 6)
		{
			// on Vista and above allocation tag is 'SeOn' - 0x6e4f6553 (hex)
			uAllocationTag = 0x6e4f6553;
		}
		else
		{
			// below Vista, allocation tag is 'SePa' - 0x61506553 (hex)
			uAllocationTag = 0x61506553;
		}

		bRet = AhAddAllocation(pList, pImageFileName, uAllocationTag);
	}

	return bRet;
}

BOOLEAN PhpProcessNamesEqual(IN PCHAR szProcName1, IN PCHAR szProcName2)
{
	// max size is 16 - this is the length of ImageFileName field inside EPROCESS structure
	ULONG uMaxSize = 16;

	while(uMaxSize--)
	{
		// if characters differ, return false
		if(*szProcName1 != *szProcName2)
		{
			return FALSE;
		}

		// if both characters are NUL, end of the string occurred
		if(*szProcName1 == '\0')
		{
			return TRUE;
		}

		++szProcName1;
		++szProcName2;
	}

	// all 16 bytes are equal but none is NUL - return TRUE anyway
	return TRUE;
}

PVOID PhpRealloc(IN PVOID pInputBuffer, const IN ULONG uOldSize, const IN ULONG uNewSize, const IN POOL_TYPE poolType)
{
	ASSERTMSG("Input buffer must not be NULL!", pInputBuffer != NULL);
	
	PVOID pOutputBuffer = ExAllocatePoolWithTag(poolType, uNewSize, TAG_TARGET_PEPROCESS_ARRAY);
	if(pOutputBuffer == NULL)
	{
		KdPrint(("[DEBUG] ERROR - Error while trying to reallocate buffer and allocate memory for the new (copy) buffer\n"));
		return pOutputBuffer;
	}

	// zero-out the buffer
	RtlZeroMemory(pOutputBuffer, uNewSize);

	// copy bytes from the input buffer, but take care not to overflow any of the specified buffers!
	ULONG uCopySize = (uNewSize > uOldSize) ? uOldSize : uNewSize;
	RtlCopyMemory(pOutputBuffer, pInputBuffer, uOldSize);
	ExFreePoolWithTag(pInputBuffer, TAG_TARGET_PEPROCESS_ARRAY);

	return pOutputBuffer;
}