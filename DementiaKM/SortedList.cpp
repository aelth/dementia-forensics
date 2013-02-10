#include "SortedList.h"

NTSTATUS SortedListCreate(	OUT PSORTED_LIST pSortedList,IN ULONG uSizeOfEntry, IN POOL_TYPE poolType, 
							IN ULONG uPoolTag, IN BOOLEAN bMultiThread, IN SORTEDLISTENTRYCLEANUP pfnCleanupFunc
						 )
{
	ASSERTMSG("Pointer to sorted list must not be NULL", pSortedList != NULL);
	ASSERTMSG("Cleanup function must not be NULL", pfnCleanupFunc != NULL);

	NTSTATUS status = STATUS_SUCCESS;
	// initialize all variables of the sorted list
	RtlZeroMemory(pSortedList, sizeof(SORTED_LIST));
	pSortedList->poolType = poolType;
	pSortedList->uPoolTag = uPoolTag;
	pSortedList->uSizeOfEntry = uSizeOfEntry;
	pSortedList->uElementCount = 0;
	pSortedList->head = pSortedList->tail = NULL;
	pSortedList->bIsMultiThread = bMultiThread;
	pSortedList->bIsWriteLocked = FALSE;
	pSortedList->lReaderCount = 0;
	pSortedList->pLock = NULL;

	// if multithreaded list
	if(bMultiThread)
	{
		pSortedList->pLock = (PERESOURCE) ExAllocatePoolWithTag(NonPagedPool, sizeof(ERESOURCE), uPoolTag);
		status = ExInitializeResourceLite(pSortedList->pLock);
	}

	pSortedList->pfnEntryCleanup = pfnCleanupFunc;

	return status;
}

NTSTATUS SortedListDestroy(IN PSORTED_LIST pSortedList)
{
	ASSERTMSG("Pointer to sorted list must not be NULL", pSortedList != NULL);

	NTSTATUS status = STATUS_SUCCESS;

	// first acquire the lock (if multithreaded)
	if(pSortedList->bIsMultiThread)
	{
		ASSERTMSG("Sorted list lock acquire must occur at or below APC_LEVEL", KeGetCurrentIrql() <= APC_LEVEL);
		SortedListWriteLock(pSortedList);
	}

	// delete all elements
	SortedListClear(pSortedList);

	// delete (un-initialize) the synchronization objects
	if(pSortedList->bIsMultiThread)
	{
		ASSERTMSG("Sorted list lock release must occur at or below DISPATCH_LEVEL", KeGetCurrentIrql() <= DISPATCH_LEVEL);
		SortedListWriteUnlock(pSortedList);
		status = ExDeleteResourceLite(pSortedList->pLock);
		ExFreePoolWithTag(pSortedList->pLock, pSortedList->uPoolTag);
	}

	pSortedList->uElementCount = 0;

	return status;
}

BOOLEAN SortedListAddEntry(IN PSORTED_LIST pSortedList, IN PSORTED_LIST_ENTRY pNewEntry)
{
	ASSERTMSG("Pointer to sorted list must not be NULL", pSortedList != NULL);
	ASSERTMSG("Pointer to the entry being added to the sorted list must not be NULL", pNewEntry != NULL);

	if(pSortedList->bIsMultiThread)
	{
		ASSERTMSG("Trying to add new entry to the multithreaded list, without acquiring the lock", pSortedList->bIsWriteLocked == TRUE);
	}

	pSortedList->uElementCount++;

	// if the list is empty, add new element to the beginning of the list
	if(pSortedList->head == NULL)
	{
		pNewEntry->next = NULL;
		pNewEntry->previous = NULL;
		pSortedList->head = pNewEntry;
		pSortedList->tail = pNewEntry;
		return TRUE;
	}

	// if the list is not empty, start from the beginning of the list and find the right place to insert the new element
	PSORTED_LIST_ENTRY pEntry = SortedListGetNext(pSortedList, NULL);
	while(pEntry != NULL) 
	{
		// duplicates are not allowed
		if(pNewEntry->uSortValue == pEntry->uSortValue)
		{
			KdPrint(("[DEBUG] WARNING - Found duplicate element 0x%x in the list -- will not add new element\n", pEntry->uSortValue));
			return FALSE;
		}

		// check if new element should be inserted here (sorted values are the key here)
		if(pNewEntry->uSortValue < pEntry->uSortValue)
		{
			pNewEntry->next = pEntry;
			pNewEntry->previous = pEntry->previous;

			// if inserting before current first element, update head pointer
			if(pEntry->previous == NULL)
			{
				pSortedList->head = pNewEntry;
			}
			else
			{
				pEntry->previous->next = pNewEntry;
			}
			pEntry->previous = pNewEntry;

			return TRUE;
		}

		pEntry = SortedListGetNext(pSortedList, pEntry);
	}

	// no such element -- adding new element at the end of the list
	pNewEntry->next = NULL;
	pNewEntry->previous = pSortedList->tail;
	pSortedList->tail->next = pNewEntry;
	pSortedList->tail= pNewEntry;

	return TRUE;
}

VOID SortedListClear(IN PSORTED_LIST pSortedList)
{
	ASSERTMSG("Pointer to sorted list must not be NULL", pSortedList != NULL);
	if(pSortedList->bIsMultiThread)
	{
		ASSERTMSG("Trying to clear the multithreaded list, without acquiring the lock", pSortedList->bIsWriteLocked == TRUE);
	}

	PSORTED_LIST_ENTRY pEntry = NULL;

	do 
	{
		pEntry = SortedListGetNext(pSortedList, NULL);
		SortedListRemoveEntry(pSortedList, pEntry);
	} while (pEntry != NULL);

	pSortedList->head = NULL;
	pSortedList->tail = NULL;
}

VOID SortedListRemoveEntry(IN PSORTED_LIST pSortedList, IN PSORTED_LIST_ENTRY pEntryToRemove)
{
	ASSERTMSG("Pointer to sorted list must not be NULL", pSortedList != NULL);
	if(pSortedList->bIsMultiThread)
	{
		ASSERTMSG("Trying to remove the entry from the multithreaded list, without acquiring the lock", pSortedList->bIsWriteLocked == TRUE);
	}

	// if NULL entry is to be removed, do nothing
	if(pEntryToRemove == NULL)
	{
		return;
	}

	// check if current element is the first element in the list
	if (pEntryToRemove->previous == NULL)
	{
		// list head must point to my successor
		pSortedList->head = pEntryToRemove->next;
	}
	else
	{
		// the successor of my predecessor is my successor
		pEntryToRemove->previous->next = pEntryToRemove->next;
	}

	// check if the element to be removed is the last element in the list
	if (pEntryToRemove->next == NULL)
	{
		// tail should point to my predecessor
		pSortedList->tail = pEntryToRemove->previous;
	}
	else
	{
		// the predecessor of my successor is my predecessor
		pEntryToRemove->next->previous = pEntryToRemove->previous;
	}

	pSortedList->uElementCount--;

	pEntryToRemove->next = NULL;
	pEntryToRemove->previous = NULL;

	pSortedList->pfnEntryCleanup(pEntryToRemove);
}

PSORTED_LIST_ENTRY SortedListGetNext(IN PSORTED_LIST pSortedList, IN PSORTED_LIST_ENTRY pCurrentElem)
{
	ASSERTMSG("Pointer to sorted list must not be NULL", pSortedList != NULL);
	if(pSortedList->bIsMultiThread)
	{
		ASSERTMSG("Trying to access multithreaded list, without acquiring the read/write lock", pSortedList->bIsWriteLocked == TRUE || pSortedList->lReaderCount > 0);
	}

	// if getting the neighbor of the current element (i.e. current element is known)
	if(pCurrentElem != NULL)
	{
		// just return the neighbor
		return pCurrentElem->next;
	}
	
	// no current element, start from the list head
	return pSortedList->head;
}

ULONG SortedListLength(PSORTED_LIST pSortedList)
{
	ASSERTMSG("Pointer to sorted list must not be NULL", pSortedList != NULL);
	if(pSortedList->bIsMultiThread)
	{
		ASSERTMSG("Trying to access multithreaded list, without acquiring the read/write lock", pSortedList->bIsWriteLocked == TRUE || pSortedList->lReaderCount > 0);
	}

	return pSortedList->uElementCount;
}

VOID SortedListWriteLock(IN PSORTED_LIST pSortedList)
{
	ASSERTMSG("Pointer to sorted list must not be NULL", pSortedList != NULL);
	ASSERTMSG("Calling lock routine for single threaded list!", pSortedList->bIsMultiThread == TRUE);
	ASSERTMSG("Sorted list lock acquire must occur at or below APC_LEVEL", KeGetCurrentIrql() <= APC_LEVEL);

	// acquire lock exclusively for the write lock -- wait if another thread is holding the resource
	KeEnterCriticalRegion();
	ExAcquireResourceExclusiveLite(pSortedList->pLock, TRUE);
	pSortedList->bIsWriteLocked = TRUE;
}

VOID SortedListReadLock(IN PSORTED_LIST pSortedList)
{
	ASSERTMSG("Pointer to sorted list must not be NULL", pSortedList != NULL);
	ASSERTMSG("Calling lock routine for single threaded list!", pSortedList->bIsMultiThread == TRUE);
	ASSERTMSG("Sorted list lock acquire must occur at or below APC_LEVEL", KeGetCurrentIrql() <= APC_LEVEL);

	// acquire shared lock -- multiple readers can access the list at the same time
	KeEnterCriticalRegion();
	ExAcquireResourceSharedLite(pSortedList->pLock, TRUE);
	InterlockedIncrement(&pSortedList->lReaderCount);
}

VOID SortedListWriteUnlock(IN PSORTED_LIST pSortedList)
{
	ASSERTMSG("Pointer to sorted list must not be NULL", pSortedList != NULL);
	ASSERTMSG("Calling unlock routine for single threaded list!", pSortedList->bIsMultiThread == TRUE);
	ASSERTMSG("Sorted list lock release must occur at or below DISPATCH_LEVEL", KeGetCurrentIrql() <= DISPATCH_LEVEL);

	pSortedList->bIsWriteLocked = FALSE;
	ExReleaseResourceLite(pSortedList->pLock);
	KeLeaveCriticalRegion();
}

VOID SortedListReadUnlock(IN PSORTED_LIST pSortedList)
{
	ASSERTMSG("Pointer to sorted list must not be NULL", pSortedList != NULL);
	ASSERTMSG("Calling unlock routine for single threaded list!", pSortedList->bIsMultiThread == TRUE);
	ASSERTMSG("Calling read unlock routine for a list with no read-locks acquired", pSortedList->lReaderCount > 0);
	ASSERTMSG("Sorted list lock release must occur at or below DISPATCH_LEVEL", KeGetCurrentIrql() <= DISPATCH_LEVEL);

	InterlockedDecrement(&pSortedList->lReaderCount);
	ExReleaseResourceLite(pSortedList->pLock);
	KeLeaveCriticalRegion();
}