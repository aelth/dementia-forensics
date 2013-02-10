#include "SynchronizationProvider.h"

#define TAG_DPC_ARRAY ('aCPD')

VOID SyncRaiseIrqlAndWaitDPC(IN		PKDPC	pDPC,
							 IN		PVOID	pDefferedContext,
							 IN		PVOID	pSystemArg1,
							 IN		PVOID	pSystemArg2
							 );

ULONG SyncpGetNumberOfCPUs(KAFFINITY affinityMask);

VOID SyncInitializeCPULock(OUT PCPU_LOCK pCPULock)
{
	ASSERTMSG("Cannot initialize NULL CPU lock", pCPULock != NULL);
	
	// initialize all fields in CPU_LOCK structure
	ASSERTMSG("Fast mutex must be initialized at or below DISPATCH_LEVEL", KeGetCurrentIrql() <= DISPATCH_LEVEL);
	ExInitializeFastMutex(&pCPULock->LockMutex);
	pCPULock->pDPCs = NULL;
	pCPULock->lNumberOfRaisedCPUs = 0;
	pCPULock->lReleaseSignal = 0;
	pCPULock->oldIrql = PASSIVE_LEVEL;
	pCPULock->bIsAcquired = FALSE;
}

BOOLEAN SyncAcquireCPULock(__inout PCPU_LOCK pCPULock)
{
	ASSERTMSG("Cannot acquire NULL CPU lock", pCPULock != NULL);

	// first we must acquire mutex - SyncAcquire method is thus thread safe when called with the same lock as parameter
	ASSERTMSG("Fast mutex acquire must occur at or below APC_LEVEL", KeGetCurrentIrql() <= APC_LEVEL);
	ExAcquireFastMutex(&pCPULock->LockMutex);

	// reset variables
	pCPULock->lNumberOfRaisedCPUs = 0;
	pCPULock->lReleaseSignal = 0;

	// get number of processors - we could obtain this number during lock initialization, but in hot-add environments, CPUs could be added during run-time
	KAFFINITY affinityMask = KeQueryActiveProcessors();
	ULONG uNumberOfCPUs = SyncpGetNumberOfCPUs(affinityMask);

	// if there is just one processor
	if(uNumberOfCPUs == 1)
	{
		// we can raise its IRQL and exit
		KdPrint(("[DEBUG] Detected only one CPU in the system, raising its IRQL to DISPATCH_LEVEL\n"));
		KeRaiseIrql(DISPATCH_LEVEL, &pCPULock->oldIrql);
		pCPULock->bIsAcquired = TRUE;

		return TRUE;
	}

	// else, we must raise every CPU to DISPATCH_LEVEL - we will do it by queuing the DPCs

	// first, allocate memory for DPCs - they MUST BE in non-paged pool, we're running at DISPATCH_LEVEL after all
	pCPULock->pDPCs = (PKDPC) ExAllocatePoolWithTag(	NonPagedPool,					// allocate memory in non-paged pool, we can access it on any IRQL
														uNumberOfCPUs * sizeof(KDPC),	// for every CPU we need one KDPC structure
														TAG_DPC_ARRAY					// use 'DPCa' tag (reverse)
												   );
	if(pCPULock->pDPCs == NULL)
	{
		KdPrint(("[DEBUG] ERROR - Allocation of DPC array failed, returning error status...\n"));
		return FALSE;
	}

	// we must acquire current CPU index - if we obtain it on any level lower than DISPATCH and above
	// context switch might occur, and we won't get correct results
	// we thus raise the IRQL first...
	KeRaiseIrql(DISPATCH_LEVEL, &pCPULock->oldIrql);

	// and then get current CPU number
	ULONG uCurrentCPUid = KeGetCurrentProcessorNumber();

	for(ULONG i = 0; i < uNumberOfCPUs; i++)
	{
		// scheduling DPC on the current processor would cause deadlock, and we must avoid it
		if(i != uCurrentCPUid)
		{
			// first initialize DPC - determine which method will be called and with which context
			KeInitializeDpc(&pCPULock->pDPCs[i], SyncRaiseIrqlAndWaitDPC, NULL);

			// DPC will be run on the i-th processor
			KeSetTargetProcessorDpc(&pCPULock->pDPCs[i], (CCHAR) i);

			// we want our acquire routine to finish ASAP, so we give it HighImportance
			KeSetImportanceDpc(&pCPULock->pDPCs[i], HighImportance);

			// finally, queue the DPC
			KeInsertQueueDpc(&pCPULock->pDPCs[i], (PVOID) pCPULock, NULL);
		}
	}

	// wait (busy waiting) until all processors have been raised to DISPATCH_LEVEL
	while(InterlockedCompareExchange(	&pCPULock->lNumberOfRaisedCPUs,				// number of currently raised CPUs (first operand to compare)
										uNumberOfCPUs - 1,							// value which will be returned as a result if comparison succeeds
										uNumberOfCPUs - 1							// value to compare with the first operand
									 ) != uNumberOfCPUs - 1
		  )
	{
		// do nothing, busy wait
		__asm
		{
			nop
		}
	}

	KdPrint(("[DEBUG] All CPUs raised to DISPATCH_LEVEL\n"));
	pCPULock->bIsAcquired = TRUE;

	return TRUE;
}

VOID SyncReleaseCPULock(__inout PCPU_LOCK pCPULock)
{
	ASSERTMSG("Cannot release NULL CPU lock", pCPULock != NULL);

	// first check if this passed lock is acquired at all
	if(pCPULock->bIsAcquired == FALSE)
	{
		return;
	}

	// "send" signal to every processor that IRQL can be lowered
	InterlockedExchange(&pCPULock->lReleaseSignal, 1);

	// wait until all processors have been released
	while(InterlockedCompareExchange(	&pCPULock->lNumberOfRaisedCPUs,				// number of currently raised CPUs (first operand to compare)
										0,											// value which will be returned as a result if comparison succeeds
										0											// value to compare with the first operand			
									 ) != 0
		 )
	{
		// do nothing
		__asm
		{
			nop
		}
	}

	// restore old IRQL - probably APC_LEVEL because of the mutex
	KeLowerIrql(pCPULock->oldIrql);

	KdPrint(("[DEBUG] All CPUs are now beneath the DISPATCH_LEVEL\n"));

	// we must free memory allocated for the DPCs
	if(pCPULock->pDPCs != NULL)
	{
		ExFreePoolWithTag(pCPULock->pDPCs, TAG_DPC_ARRAY);
		pCPULock->pDPCs = NULL;
	}

	// reset the flag
	pCPULock->bIsAcquired = FALSE;

	// release the mutex and thus lower the IRQL
	ASSERTMSG("Fast mutex release must occur at APC_LEVEL", KeGetCurrentIrql() == APC_LEVEL);
	ExReleaseFastMutex(&pCPULock->LockMutex);
}

VOID SyncRaiseIrqlAndWaitDPC(IN PKDPC pDPC, IN PVOID pDefferedContext, IN PVOID pSystemArg1, IN PVOID pSystemArg2)
{
	ASSERTMSG("SystemArgument1 cannot be NULL - we must pass CPU_LOCK structure", pSystemArg1 != NULL);

	// get CPU_LOCK structure passed as an argument
	PCPU_LOCK pCPULock = (PCPU_LOCK) pSystemArg1;

	// first increase number of raised CPUs - we are indeed on DISPATCH_LEVEL, because DPCs run on DISPATCH_LEVEL
	InterlockedIncrement(&pCPULock->lNumberOfRaisedCPUs);

	// until we get signal to lower down the IRQL, we must busy wait - THIS IS THE MAIN REASON WHY CODE BETWEEN CPU_LOCK ACQUIRE AND RELEASE MUST BE MINIMAL AND FAST
	while(InterlockedCompareExchange(	&pCPULock->lReleaseSignal,	// variable which will be used as a signal to lower down IRQL (when 1)
										1,							// value which will be returned as a result if comparison succeeds
										1							// value to compare with the first operand
									) == 0
		  )
	{
		// do nothing
		__asm
		{
			nop
		}
	}

	// we will lower down IRQL upon exit of this routine, so decrement number of raised CPUs
	// there is a small time window between routine exit and this call while CPU is still raised
	// and flag indicates otherwise - still, this behavior should not pose any danger or problems
	InterlockedDecrement(&pCPULock->lNumberOfRaisedCPUs);

	KdPrint(("[DEBUG] Processor %d going below DISPATCH_LEVEL\n", KeGetCurrentProcessorNumber()));
}

ULONG SyncpGetNumberOfCPUs(KAFFINITY affinityMask)
{
	ULONG_PTR mask = (ULONG_PTR) affinityMask;
	ULONG counter;

	for(counter = 0; mask; counter++)
	{
		mask &= mask - 1; // clear the least significant bit set
	}

	return counter;
}