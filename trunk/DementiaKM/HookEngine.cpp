#include "HookEngine.h"
#include "HookPrivateIncludes.h"
#include "SynchronizationProvider.h"
#include "XDE.h"
#include <ntstrsafe.h>

// boolean representing status of HookEngine
static BOOLEAN bIsHkEngineInitialized = FALSE;

// since we are always going to allocate fixed-sized memory blocks (HOOK structures),
// it is very convenient and efficient to use lookaside lists
static NPAGED_LOOKASIDE_LIST HooksLookasideList;

// the head of the hook linked list - list of all hooks which are done by the driver
static LIST_ENTRY HooksListHead;

// mutex which will protect accesses to hook list
static FAST_MUTEX HooksListMutex;

// CPU lock which is used to ensure that the running thread cannot be preempted during the hook
static CPU_LOCK CPULock;

// pointer to memory descriptor list structure
PMDL g_MDL = NULL;

ULONG HkpGetMinimalDetourLengthInBytes(IN PVOID pFunctionAddress);
PUCHAR HkpCreateCallableBuffer(IN PVOID pFunctionAddress, IN PUCHAR pSavedBytes, IN	ULONG uDetourLength);
NTSTATUS HkpUnhookNoRemoveNoLock(IN PVOID pOriginalFunction);
BOOLEAN HkpAddHookEntry(IN PVOID pOriginalFunction, IN PVOID pNewFunction, IN PUCHAR pOriginalBytes, IN	ULONG uOriginalBytesLength);
BOOLEAN HkpRemoveHookEntry(IN PVOID pOriginalFunction);
PHOOK_ENTRY HkpFindHookEntry(IN PVOID pOriginalFunction, IN BOOLEAN bUseLock);

VOID HkInit(VOID)
{
	KeEnterCriticalRegion();
	if(bIsHkEngineInitialized == TRUE)
	{
		KeLeaveCriticalRegion();
		return;
	}

	ASSERTMSG("Fast mutex must be initialized at or below DISPATCH_LEVEL", KeGetCurrentIrql() <= DISPATCH_LEVEL);
	// initialize all required data structures
	ExInitializeNPagedLookasideList(&HooksLookasideList,				// list to initialize
									NULL,								// allocate function - OS supplied
									NULL,								// free function - OS supplied
									0,									// flags - always zero
									sizeof(HOOK_ENTRY),					// size of each entry to be allocated
									'oLkH',								// HkLo(okaside) tag
									0									// depth - always zero
									);
	InitializeListHead(&HooksListHead);
	ExInitializeFastMutex(&HooksListMutex);

	// lock necessary for hooking
	SyncInitializeCPULock(&CPULock);

	bIsHkEngineInitialized = TRUE;
	KeLeaveCriticalRegion();
}

VOID HkUnInit(VOID)
{
	if(bIsHkEngineInitialized == FALSE)
	{
		return;
	}

	PLIST_ENTRY hooksListEntry = HooksListHead.Flink;

	// iterate through the hooks list
	ASSERTMSG("Fast mutex acquire must occur at or below APC_LEVEL", KeGetCurrentIrql() <= APC_LEVEL);
	ExAcquireFastMutex(&HooksListMutex);
	while(hooksListEntry != &HooksListHead)
	{
		PHOOK_ENTRY pHookEntry = CONTAINING_RECORD(hooksListEntry, HOOK_ENTRY, ListEntry);

		// if failure occurs, we cannot do anything:(
		HkpUnhookNoRemoveNoLock(pHookEntry->Hook.pOriginalFunction);

		// release memory allocated for saved bytes array
		ExFreePoolWithTag(pHookEntry->Hook.pSavedBytes, TAG_SAVED_BYTES);
		pHookEntry->Hook.pSavedBytes = NULL;

		hooksListEntry = hooksListEntry->Flink;
	}
	ASSERTMSG("Fast mutex release and lookaside list deletion must occur at or below APC_LEVEL", KeGetCurrentIrql() <= APC_LEVEL);
	ExReleaseFastMutex(&HooksListMutex);

	// free all entries in lookaside list
	ExDeleteNPagedLookasideList(&HooksLookasideList);

	// set flag so someone can initialize HookEngine again if needed
	bIsHkEngineInitialized = FALSE;
}

BOOLEAN HkIsInitialized(void)
{
	return bIsHkEngineInitialized;
}

NTSTATUS HkHook(IN PVOID pOriginalFunction, IN PVOID pNewFunction, OUT PUCHAR *ppHookedFunctionCallBuffer)
{
	ASSERTMSG("HookEngine must be initialized prior to this call", bIsHkEngineInitialized == TRUE);
	ASSERTMSG("Cannot hook NULL function, or use NULL function as a detour", pOriginalFunction != NULL && pNewFunction != NULL);

	PMDL pMDL = NULL;
	PVOID pMappedAddress = NULL;
	NTSTATUS status = STATUS_SUCCESS;

	status = HkMapMDL(	pOriginalFunction,						// create MDL beginning at original function address
						METHOD_ENTRY_DETOUR_LENGTH,				// MDL is spanning through first 20 bytes of the function - we will never need to overwrite more than that
						KernelMode,								// access mode
						&pMDL,									// pointer to created MDL
						&pMappedAddress							// pointer to mapped MDL address
					   );

	if(!NT_SUCCESS(status))
	{
		KdPrint(("[DEBUG] ERROR - Creating of MDL over the first %d bytes of the function failed\n", METHOD_ENTRY_DETOUR_LENGTH));
		return status;
	}

	PUCHAR pSavedBytes = NULL;
	ULONG uDetourLength = 0;

	ASSERTMSG("CPU lock acquire must occur at or below APC_LEVEL", KeGetCurrentIrql() <= APC_LEVEL);
	if(SyncAcquireCPULock(&CPULock))
	{
		PCHAR origFunction = (PCHAR) pMappedAddress;
		uDetourLength = HkpGetMinimalDetourLengthInBytes(origFunction);

		pSavedBytes = (PUCHAR) ExAllocatePoolWithTag(NonPagedPool, uDetourLength, TAG_SAVED_BYTES);
		if(pSavedBytes == NULL)
		{
			KdPrint(("[DEBUG] ERROR - Allocation of saved bytes buffer failed, must exit\n"));
			ASSERTMSG("CPU lock release must occur at DISPATCH_LEVEL", KeGetCurrentIrql() == DISPATCH_LEVEL);
			SyncReleaseCPULock(&CPULock);
			return STATUS_INSUFFICIENT_RESOURCES;
		}

		// save original bytes
		RtlCopyMemory(pSavedBytes, origFunction, uDetourLength);
		// write jmp opcode
		*origFunction = (CHAR) JMP;
		// write jmp address (subtract 5 because this is the length of the jmp instr)
		*(PULONG_PTR)(origFunction + 1) = (ULONG_PTR) pNewFunction - (ULONG_PTR) pOriginalFunction - 5;

		// create callable buffer for calling the hooked function
		*ppHookedFunctionCallBuffer = HkpCreateCallableBuffer(pOriginalFunction, pSavedBytes, uDetourLength);

		ASSERTMSG("CPU lock release must occur at DISPATCH_LEVEL", KeGetCurrentIrql() == DISPATCH_LEVEL);
		SyncReleaseCPULock(&CPULock);
	}
	else
	{
		KdPrint(("[DEBUG] ERROR - Cannot acquire CPU lock\n"));
		status = STATUS_INSUFFICIENT_RESOURCES;
	}

	HkpAddHookEntry(pOriginalFunction, pNewFunction, pSavedBytes, uDetourLength);

	ExFreePoolWithTag(pSavedBytes, TAG_SAVED_BYTES);
	MmUnmapLockedPages(pMappedAddress, pMDL);
	IoFreeMdl(pMDL);

	return status;
}

NTSTATUS HkUnhook(IN PVOID pOriginalFunction)
{
	ASSERTMSG("HookEngine must be initialized prior to this call", bIsHkEngineInitialized == TRUE);
	ASSERTMSG("Original function must not be NULL", pOriginalFunction != NULL);

	NTSTATUS status = HkpUnhookNoRemoveNoLock(pOriginalFunction);

	if(!NT_SUCCESS(status))
	{
		KdPrint(("[DEBUG] ERROR - HkpUnhookNoRemoveNoLock failed\n"));
		return status;
	}

	if(HkpRemoveHookEntry(pOriginalFunction) == FALSE)
	{
		KdPrint(("[DEBUG] ERROR - Hook not removed from the structure, inconsistent state!!! It would be best if we could crash the system down right away.\n"));
		status = STATUS_INTERNAL_ERROR;
	}

	return status;
}

ULONG HkpGetMinimalDetourLengthInBytes(PVOID pFunctionAddress)
{
	ASSERTMSG("Function address must not be NULL", pFunctionAddress != NULL);

	// we don't want to split instruction in chunks when hooking - our hook must
	// replace DISCRETE instructions, not split them in half
	ULONG uDetourLength = 0;
	PUCHAR pFunctionPointer = (PUCHAR) pFunctionAddress;

	// we need 5 bytes for our jump, so determine the minimum number of bytes
	// greater than 5 which is obtained by calculating each instruction length and
	// adding this length to cumulative detour length
	while(uDetourLength < 5)
	{
		xde_instr currInstr;
		xde_disasm(pFunctionPointer, &currInstr);
		uDetourLength += currInstr.len;
		pFunctionPointer += currInstr.len;
	}

	return uDetourLength;
}

PUCHAR HkpCreateCallableBuffer(IN PVOID pFunctionAddress, IN PUCHAR pSavedBytes, IN ULONG uDetourLength)
{
	ASSERTMSG("Function address must not be NULL", pFunctionAddress != NULL);
	ASSERTMSG("Saved bytes pointer must be initialized", pSavedBytes != NULL);

	/* executable buffer will have following format:

	b8 XX XX XX XX	mov eax, function_address
	83 c0 XX		add eax, detour_length
	...
	detourLength	saved_bytes
	...
	ff e0			jmp eax

	buffer size is thus 10 + detourLength 
	*/
	PUCHAR pExecBuffer = (PUCHAR) ExAllocatePoolWithTag(NonPagedPool, 10 + uDetourLength, TAG_EXEC_BUFFER);
	if(pExecBuffer == NULL)
	{
		KdPrint(("[DEBUG] ERROR - Could not allocate memory for executable buffer, reporting failure\n"));
		return NULL;
	}

	ULONG uOffset = 0;

	pExecBuffer[uOffset++] = 0xb8;
	*((PULONG) (pExecBuffer + uOffset)) = (ULONG_PTR) pFunctionAddress;
	uOffset += sizeof(ULONG_PTR);
	pExecBuffer[uOffset++] = 0x83;
	pExecBuffer[uOffset++] = 0xc0;
	pExecBuffer[uOffset++] = (UCHAR) uDetourLength;

	RtlCopyMemory(pExecBuffer + uOffset, pSavedBytes, uDetourLength);
	uOffset += uDetourLength;

	pExecBuffer[uOffset++] = 0xff;
	pExecBuffer[uOffset++] = 0xe0;

	return pExecBuffer;
}

NTSTATUS HkpUnhookNoRemoveNoLock(IN PVOID pOriginalFunction)
{
	ASSERTMSG("HookEngine must be initialized prior to this call", bIsHkEngineInitialized == TRUE);
	ASSERTMSG("Original function must not be NULL", pOriginalFunction != NULL);

	// try to find given entry in the hook list
	PHOOK_ENTRY pHookEntry = HkpFindHookEntry(pOriginalFunction, FALSE);
	// if given entry does not exists, function is not hooked
	if(pHookEntry == NULL)
	{
		KdPrint(("[DEBUG] ERROR - Cannot find function with the address 0x%x in the hook list, make sure the function is hooked\n", pOriginalFunction));
		return STATUS_NOT_FOUND;
	}

	PMDL pMDL = NULL;
	PVOID pMappedAddress = NULL;
	NTSTATUS status = STATUS_SUCCESS;

	// create MDL over function
	status = HkMapMDL(	pHookEntry->Hook.pOriginalFunction,		// create MDL beginning at original function address
						pHookEntry->Hook.uSavedBytesLength,		// MDL is spanning through first x bytes of the function (determined by the XDE)
						KernelMode,								// access mode
						&pMDL,									// pointer to created MDL
						&pMappedAddress							// pointer to mapped MDL address
					  );

	if(!NT_SUCCESS(status))
	{
		KdPrint(("[DEBUG] ERROR - Creating of MDL over first %d bytes of the function failed\n", pHookEntry->Hook.uSavedBytesLength));
		return status;
	}

	// raise IRQL on all CPUs before performing unhooking
	ASSERTMSG("CPU lock acquire must occur at or below APC_LEVEL", KeGetCurrentIrql() <= APC_LEVEL);
	if(SyncAcquireCPULock(&CPULock))
	{
		// first restore old bytes
		RtlCopyMemory(pMappedAddress, pHookEntry->Hook.pSavedBytes, pHookEntry->Hook.uSavedBytesLength);

		// release the lock
		ASSERTMSG("CPU lock release must occur at DISPATCH_LEVEL", KeGetCurrentIrql() == DISPATCH_LEVEL);
		SyncReleaseCPULock(&CPULock);
	}
	else
	{
		KdPrint(("[DEBUG] ERROR - Cannot acquire CPU lock\n"));
		status = STATUS_INSUFFICIENT_RESOURCES;
	}

	MmUnmapLockedPages(pMappedAddress, pMDL);
	IoFreeMdl(pMDL);

	return status;
}

NTSTATUS HkMapMDL(IN PVOID pAddressToMap, IN ULONG uBufferLength, IN KPROCESSOR_MODE mode, OUT PMDL *ppMDL, OUT PVOID *ppMappedAddress)
{
	ASSERTMSG("Creation and mapping of MDLs must occur at or below DISPATCH_LEVEL", KeGetCurrentIrql() <= DISPATCH_LEVEL);

	*ppMDL = IoAllocateMdl(	pAddressToMap,				// pointer to virtual address over which MDL is being created
							uBufferLength,				// length of the MDL buffer (in bytes)
							FALSE,						// flag which indicates whether the buffer is primary or secondary - we have no IRP and it must be FALSE
							FALSE,						// don't charge quota
							NULL						// no IRP is associated with the MDL
							);
	if(*ppMDL == NULL)
	{
		KdPrint(("[DEBUG] ERROR - IoAllocateMdl call failed, could not create MDL\n"));
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	__try 
	{
		MmProbeAndLockPages(*ppMDL, mode, IoModifyAccess);

		// modify the MDL to describe non-paged pool
		MmBuildMdlForNonPagedPool(*ppMDL);

		// modify the flags to make the underlying pages writable
		(*ppMDL)->MdlFlags |= MDL_MAPPED_TO_SYSTEM_VA;

		// lock those pages and return the pointer to the created MDL
		*ppMappedAddress = MmMapLockedPagesSpecifyCache(	*ppMDL,							// pointer to MDL to be mapped	
															KernelMode,						// access mode - drivers use KernelMode
															MmNonCached,					// caching type - do not cache requested memory
															NULL,							// base address - for kernel mode, this is NULL	
															FALSE,							// do not bug check on failure
															NormalPagePriority				// page priority, use normal page priority
															);
	}
	__except(EXCEPTION_EXECUTE_HANDLER)
	{
		KdPrint(("[DEBUG] ERROR - Pages could not be locked or MDL could not be built...\n"));
		IoFreeMdl(*ppMDL);
		return STATUS_INSUFFICIENT_RESOURCES;
	}
	

	if(*ppMappedAddress == NULL)
	{
		KdPrint(("[DEBUG] ERROR - MmMapLockedPagesSpecifyCache call failed, could not map locked pages\n"));
		IoFreeMdl(*ppMDL);
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	return STATUS_SUCCESS;
}

BOOLEAN HkpAddHookEntry(IN PVOID pOriginalFunction, IN PVOID pNewFunction, IN PUCHAR pOriginalBytes, IN ULONG uOriginalBytesLength)
{
	ASSERTMSG("HookEngine must be initialized prior to this call", bIsHkEngineInitialized == TRUE);
	ASSERTMSG("Addresses of the original and new function cannot be NULL", pOriginalFunction != NULL && pNewFunction != NULL);
	ASSERTMSG("Original 5 bytes of the function to be hooked cannot be NULL", pOriginalBytes != NULL);

	// if hook already exists
	if(HkpFindHookEntry(pOriginalFunction, TRUE) != NULL)
	{
		// we can only return success - don't want to "update" the hook
		KdPrint(("[DEBUG] WARNING - Function on address 0x%x is already hooked and detoured to function 0x%x", (ULONG_PTR) pOriginalFunction, (ULONG_PTR) pNewFunction));
		return TRUE;
	}

	// get memory for new hook structure from lookaside list
	PHOOK_ENTRY pHookEntry = (PHOOK_ENTRY) ExAllocateFromNPagedLookasideList(&HooksLookasideList);
	if(pHookEntry == NULL)
	{
		KdPrint(("[DEBUG] ERROR - Not enough memory in lookaside list to allocate new hook entry"));
		return FALSE;
	}

	// save original and hook method, and copy original bytes at the beginning of the method to be hooked
	pHookEntry->Hook.pOriginalFunction = pOriginalFunction;
	pHookEntry->Hook.pNewFunction = pNewFunction;
	pHookEntry->Hook.pSavedBytes = (PUCHAR) ExAllocatePoolWithTag(NonPagedPool, uOriginalBytesLength, TAG_SAVED_BYTES);
	if(pHookEntry->Hook.pSavedBytes == NULL)
	{
		KdPrint(("[DEBUG] ERROR - Not enough memory to allocate saved bytes buffer"));
		return FALSE;
	}
	RtlCopyMemory(pHookEntry->Hook.pSavedBytes, pOriginalBytes, uOriginalBytesLength);
	pHookEntry->Hook.uSavedBytesLength = uOriginalBytesLength;

	// insert it to list (thread safe)
	ASSERTMSG("Fast mutex acquire/release must occur at or below APC_LEVEL", KeGetCurrentIrql() <= APC_LEVEL);
	ExAcquireFastMutex(&HooksListMutex);
	InsertHeadList(&HooksListHead, &pHookEntry->ListEntry);
	ExReleaseFastMutex(&HooksListMutex);

	return TRUE;
}

BOOLEAN HkpRemoveHookEntry(IN PVOID pOriginalFunction)
{
	ASSERTMSG("HookEngine must be initialized prior to this call", bIsHkEngineInitialized == TRUE);
	ASSERTMSG("Addresses of the original and new function cannot be NULL", pOriginalFunction != NULL);

	// try to find the hook entry which is going to be removed
	PHOOK_ENTRY pHookEntry = HkpFindHookEntry(pOriginalFunction, TRUE);

	// if hook entry was not found, return FALSE
	if(pHookEntry == NULL)
	{
		return FALSE;
	}

	ASSERTMSG("Fast mutex acquire must occur at or below APC_LEVEL", KeGetCurrentIrql() <= APC_LEVEL);
	ExAcquireFastMutex(&HooksListMutex);
	// release memory allocated for saved bytes array
	ExFreePoolWithTag(pHookEntry->Hook.pSavedBytes, TAG_SAVED_BYTES);
	pHookEntry->Hook.pSavedBytes = NULL;

	// hook entry found, so we remove it from the list...
	RemoveEntryList(&pHookEntry->ListEntry);

	// and free allocated memory
	ASSERTMSG("Fast mutex release must occur at or below APC_LEVEL", KeGetCurrentIrql() <= APC_LEVEL);
	ExFreeToNPagedLookasideList(&HooksLookasideList, pHookEntry);
	ExReleaseFastMutex(&HooksListMutex);

	return TRUE;
}

PHOOK_ENTRY HkpFindHookEntry(IN PVOID pOriginalFunction, IN BOOLEAN bUseLock)
{
	ASSERTMSG("HookEngine must be initialized prior to this call", bIsHkEngineInitialized == TRUE);
	ASSERTMSG("Addresses of the original and new function which are being searched for cannot be NULL", pOriginalFunction != NULL);

	PLIST_ENTRY hooksListEntry = HooksListHead.Flink;

	// iterate through the hooks list
	if(bUseLock)
	{
		ASSERTMSG("Fast mutex acquire must occur at or below APC_LEVEL", KeGetCurrentIrql() <= APC_LEVEL);
		ExAcquireFastMutex(&HooksListMutex);
	}
	while(hooksListEntry != &HooksListHead)
	{
		PHOOK_ENTRY pHookEntry = CONTAINING_RECORD(hooksListEntry, HOOK_ENTRY, ListEntry);
		// if this is the wanted entry - we assume that the function can only be hooked once, so if original function addresses
		// are the same, then hooks entries are the same
		if(pHookEntry->Hook.pOriginalFunction == pOriginalFunction)
		{
			if(bUseLock)
			{
				ASSERTMSG("Fast mutex release must occur at APC_LEVEL", KeGetCurrentIrql() == APC_LEVEL);
				ExReleaseFastMutex(&HooksListMutex);
			}
			return pHookEntry;
		}

		hooksListEntry = hooksListEntry->Flink;
	}

	// wanted hook entry is not present in the list, return NULL
	if(bUseLock)
	{
		ASSERTMSG("Fast mutex release must occur at APC_LEVEL", KeGetCurrentIrql() == APC_LEVEL);
		ExReleaseFastMutex(&HooksListMutex);
	}

	return NULL;
}