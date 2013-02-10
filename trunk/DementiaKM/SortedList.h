#ifndef __SORTEDLIST_H_VERSION__
#define __SORTEDLIST_H_VERSION__ 100

#if _MSC_VER > 1000 
#pragma once  
#endif // _MSC_VER > 1000

// list implementation is somewhat based on Jozef Bekes work: https://sites.google.com/site/jozsefbekes/Home/windows-programming/ddk-list-implementation

#ifdef __cplusplus
extern "C"{
#endif // __cplusplus
#include <ntddk.h>
#ifdef __cplusplus
};
#endif // __cplusplus

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

	// minimal list entry - sort value member will be used for proper insertion of elements to the list
	typedef struct _SORTED_LIST_ENTRY
	{
		ULONG_PTR uSortValue;
		struct _SORTED_LIST_ENTRY *next;
		struct _SORTED_LIST_ENTRY *previous;
	} SORTED_LIST_ENTRY, *PSORTED_LIST_ENTRY;

	// pointer to function used for cleanup/deallocation of list element
	typedef VOID (*SORTEDLISTENTRYCLEANUP)(PSORTED_LIST_ENTRY pEntry);

	// these structures should not be modified directly!
	typedef struct _SORTED_LIST
	{
		POOL_TYPE poolType;
		ULONG uPoolTag;
		ULONG uSizeOfEntry;
		ULONG uElementCount;
		PERESOURCE pLock;
		LONG lReaderCount;
		BOOLEAN bIsMultiThread;
		BOOLEAN bIsWriteLocked;

		PSORTED_LIST_ENTRY head;
		PSORTED_LIST_ENTRY tail;

		SORTEDLISTENTRYCLEANUP pfnEntryCleanup;

	} SORTED_LIST, *PSORTED_LIST;

	NTSTATUS SortedListCreate(	OUT PSORTED_LIST pSortedList, 
								IN ULONG uSizeOfEntry, 
								IN POOL_TYPE poolType,
								IN ULONG uPoolTag,
								IN BOOLEAN bMultiThreaded, 
								IN SORTEDLISTENTRYCLEANUP pfnCleanupFunc
							 );

	// add new element at the proper position in the list
	// if element is already in the list, it won't be added, and FALSE is returned to the caller
	BOOLEAN SortedListAddEntry(IN PSORTED_LIST pSortedList, IN PSORTED_LIST_ENTRY pNewEntry);

	// get next element of the list based on the current element
	// if current element is NULL, return first element of the list
	PSORTED_LIST_ENTRY SortedListGetNext(IN PSORTED_LIST pSortedList, IN PSORTED_LIST_ENTRY pCurrentElem);

	// remove specific list element
	VOID SortedListRemoveEntry(IN PSORTED_LIST pSortedList, IN PSORTED_LIST_ENTRY pEntryToRemove);

	// deletes all elements of the sorted list and the list structures (locks, etc)
	NTSTATUS SortedListDestroy(IN PSORTED_LIST pSortedList);

	// deletes all elements of the sorted list, but without destroying the list structures (locks, etc)
	VOID SortedListClear(IN PSORTED_LIST pSortedList);

	// get list length (i.e. number of elements)
	ULONG SortedListLength(PSORTED_LIST pSortedList);

	// lock-unlock functions if the list is multithreaded
	// since read operations are much more frequent than the write operations, both read and write locks are available
	// these functions should be used before performing operations on the list (if the list is multithreaded)
	VOID SortedListWriteLock(IN PSORTED_LIST pSortedList);
	VOID SortedListReadLock(IN PSORTED_LIST pSortedList);
	VOID SortedListWriteUnlock(IN PSORTED_LIST pSortedList);
	VOID SortedListReadUnlock(IN PSORTED_LIST pSortedList);

#ifdef __cplusplus
}; // extern "C"
#endif // __cplusplus

#endif // __SORTEDLIST_H_VERSION__