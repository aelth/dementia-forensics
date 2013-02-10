#include "GenericHider.h"
#include "SymbolWrapper.h"

// "private" function, used as a generic method for adding items to the hide list
VOID GhpAddHideEntry(IN OUT PSORTED_LIST pList, 
					 const IN PVOID pPhysicalAddress, 
					 const IN PVOID pVirtualAddress,
					 const IN PVOID pOldMemContents, 
					 const IN PVOID pNewMemContents,
					 const IN ULONG uSize, 
					 const IN HIDE_TYPE type);

// this function is used for connecting the structures in linked list when "target" element has been removed (see GhModifyListFlinkBlinkPointers)
// function uses native pointers (4 bytes on 32-bit architecture and 8 bytes on 64-bit architecture)
VOID GhpAddAndLinkHideAddresses(	IN OUT PSORTED_LIST pList, 
									const IN PVOID pPhysicalAddress,
									const IN PVOID pVirtualAddress, 
									const IN ULONG_PTR uOldValue,
									const IN ULONG_PTR uNewValue);

PVOID GhpGetPhysicalAddress(const IN PVOID pVirtualAddress);

VOID GhAddReplacementValues(IN OUT PSORTED_LIST pList, const IN PVOID pPhysicalAddress, const IN PVOID pVirtualAddress, 
							const IN ULONG_PTR uOldValue, const IN ULONG_PTR uNewValue, const IN ULONG uReplaceSize)
{
	if(pList == NULL)
	{
		KdPrint(("[DEBUG] ERROR - Hide list is not initialized -- range to be hidden/erased cannot be added to the list\n"));
		return;
	}

	// allocate memory for old and new contents and add new entry to the list
	PVOID pOldMemory = ExAllocatePoolWithTag(NonPagedPool, uReplaceSize, TAG_HIDE_ARRAY);
	PVOID pNewMemory = ExAllocatePoolWithTag(NonPagedPool, uReplaceSize, TAG_HIDE_ARRAY); 
	if(pOldMemory == NULL || pNewMemory == NULL)
	{
		KdPrint(("[DEBUG] WARNING - Cannot allocate new HIDE_ARRAY -- VA 0x%x will not be added to the list...\n", pVirtualAddress));
		return;
	}

	RtlCopyMemory(pOldMemory, &uOldValue, uReplaceSize);
	RtlCopyMemory(pNewMemory, &uNewValue, uReplaceSize);
	GhpAddHideEntry(pList, pPhysicalAddress, pVirtualAddress, pOldMemory, pNewMemory, uReplaceSize, REPLACE);
}

VOID GhAddReplacementBuffers(IN OUT PSORTED_LIST pList, const IN PVOID pPhysicalAddress, const IN PVOID pVirtualAddress,
							 const IN PVOID pOldMemoryContents, const IN PVOID pNewMemoryContents, const IN ULONG uReplaceSize)
{
	GhpAddHideEntry(pList, pPhysicalAddress, pVirtualAddress, pOldMemoryContents, pNewMemoryContents, uReplaceSize, REPLACE);
}

VOID GhAddRangeForHiding(IN OUT PSORTED_LIST pList, const IN PVOID pPhysicalAddress, const IN PVOID pVirtualAddress, const IN ULONG uSize)
{
	if(pList == NULL)
	{
		KdPrint(("[DEBUG] ERROR - Hide list is not initialized -- range to be hidden/erased cannot be added to the list\n"));
		return;
	}

	// don't really care about physical address, virtual address or size - delegating the responsibility to other functions
	GhpAddHideEntry(pList, pPhysicalAddress, pVirtualAddress, NULL, NULL, uSize, DEL);
}

VOID GhAddGlobalHideAddress(IN OUT PSORTED_LIST pList, const IN PVOID pPhysicalAddress, const IN PVOID pVirtualAddress, 
							const IN ULONG_PTR uOldValue, const IN ULONG_PTR uNewValue, const IN ULONG uReplaceSize, const IN OP_TYPE operation)
{
	if(pList == NULL)
	{
		KdPrint(("[DEBUG] ERROR - Hide list is not initialized -- address to be hidden cannot be added to the list\n"));
		return;
	}

	PVOID pPhysAddress = pPhysicalAddress;
	if(pPhysAddress == NULL)
	{
		pPhysAddress = GhpGetPhysicalAddress(pVirtualAddress);
	}

	// acquire write lock, since we're going to write to the list afterwards
	ASSERTMSG("Sorted list lock acquire must occur at or below APC_LEVEL", KeGetCurrentIrql() <= APC_LEVEL);
	SortedListWriteLock(pList);

	PHIDE_ADDRESS_ENTRY pEntry = NULL;

	// go through the list and try to find the element with the wanted address
	while((pEntry = (PHIDE_ADDRESS_ENTRY) SortedListGetNext(pList, (PSORTED_LIST_ENTRY) pEntry)) != NULL)
	{
		// if the element is in the list
		if(pPhysAddress == pEntry->pPhysicalAddress)
		{
			// first check if the found element has "replace" operation - if not, exit
			if(pEntry->type != REPLACE)
			{
				KdPrint(("[DEBUG] ERROR - Hide element with address 0x%p found, but the operation is not REPLACE!\n", pEntry));
				SortedListWriteUnlock(pList);
				return;
			}

			// check the size of the memory being replaced - sizes of char (1 byte), SHORT (2 bytes), ULONG (4 bytes) and ULONG64 (8 bytes) are not supported
			if(pEntry->uSize != sizeof(char) && pEntry->uSize != sizeof(SHORT) && pEntry->uSize != sizeof(ULONG) && pEntry->uSize != sizeof(ULONG64))
			{
				KdPrint(("[DEBUG] ERROR - Hide element with address 0x%p found, but the size of the memory buffer for replacement (0x%x) is not 1, 2, 4 or 8!\n", pEntry, pEntry->uSize));
				SortedListWriteUnlock(pList);
				return;
			}

			// check if buffer containing new memory contents is present - exit if not
			if(pEntry->pNewMemContents == NULL)
			{
				KdPrint(("[DEBUG] ERROR - Buffer containing new memory contents has not been intiialized - cannot update hide element @0x%x!\n", pEntry));
				SortedListWriteUnlock(pList);
				return;
			}

			LONG lOpValue = 0;

			// perform operation on the element found
			switch(operation)
			{
			case DEC:
				// decrement the value
				lOpValue = -1;
				break;
			case INC:
				// increment the value
				lOpValue = 1;
				break;
			case REP:
				// replace the value in the list with the value passed as an argument
				// using brackets for variable declaration/definition
				{
					// check if replacement size is equal to the previous buffer size
					// this is treated as error, although we could continue and update the size member accordingly!
					if(uReplaceSize != pEntry->uSize)
					{
						KdPrint(("[DEBUG] ERROR - Cannot perform replacement of the old value because memory replacement buffer sizes differ!\n"));
						SortedListWriteUnlock(pList);
						return;
					}

					RtlCopyMemory(pEntry->pNewMemContents, &uNewValue, uReplaceSize);
				}
				break;
			default:
				KdPrint(("[DEBUG] WARNING - Invalid operation on the \"global hide element\" specified: %d\n", operation));
			}

			// update the value by using the appropriate data type
			switch(pEntry->uSize)
			{
			case sizeof(char):
				// update the byte
				*((char *) pEntry->pNewMemContents) += (char) lOpValue;
				break;
			case sizeof(SHORT):
				// update the short value
				*((PSHORT) pEntry->pNewMemContents) += (SHORT) lOpValue;
				break;
			case sizeof(ULONG):
				// update the int value
				*((PULONG) pEntry->pNewMemContents) += lOpValue;
				break;
			case sizeof(ULONG64):
				// update the int64 value
				*((PULONG64) pEntry->pNewMemContents) += (ULONG64) lOpValue;
				break;
			}

			// exit the loop, since there cannot be any duplicates
			break;
		}
	}

	ASSERTMSG("Sorted list lock release must occur at or below DISPATCH_LEVEL", KeGetCurrentIrql() <= DISPATCH_LEVEL);
	SortedListWriteUnlock(pList);

	// if the element has not been found, this element has to be added to the list
	if(pEntry == NULL)
	{
		// allocate memory for old and new contents and add new entry to the list
		PVOID pOldMemory = ExAllocatePoolWithTag(NonPagedPool, uReplaceSize, TAG_HIDE_ARRAY);
		PVOID pNewMemory = ExAllocatePoolWithTag(NonPagedPool, uReplaceSize, TAG_HIDE_ARRAY); 
		if(pOldMemory == NULL || pNewMemory == NULL)
		{
			KdPrint(("[DEBUG] WARNING - Cannot allocate new HIDE_ARRAY -- VA 0x%x will not be added to the list...\n", pVirtualAddress));
			return;
		}

		RtlCopyMemory(pOldMemory, &uOldValue, uReplaceSize);
		RtlCopyMemory(pNewMemory, &uNewValue, uReplaceSize);
		GhpAddHideEntry(pList, pPhysAddress, pVirtualAddress, pOldMemory, pNewMemory, uReplaceSize, REPLACE);
	}
}

VOID GhModifyListFlinkBlinkPointers(IN OUT PSORTED_LIST pList, const IN PVOID pObject, const IN ULONG uObjectListOffset)
{
	if(pList == NULL)
	{
		KdPrint(("[DEBUG] ERROR - Hide list is not initialized -- address to be hidden cannot be added to the list\n"));
		return;
	}

	if(pObject == NULL)
	{
		KdPrint(("[DEBUG] ERROR - Object which contains LIST_ENTRY member cannot be NULL\n"));
		return;
	}

	PLIST_ENTRY pTargetObjectListEntry = (PLIST_ENTRY) SYMW_MEMBER_FROM_OFFSET(pObject, uObjectListOffset, 0);

	// fix flink pointers
	// target->blink->flink = target->flink
	PVOID pFlinkAddress = pTargetObjectListEntry->Blink;
	PVOID pFlinkOldValue = pTargetObjectListEntry->Blink->Flink;
	PVOID pFlinkNewValue = pTargetObjectListEntry->Flink;
	KdPrint(("[DEBUG] Target object flink @ 0x%x -- old value = 0x%x, new value = 0x%x\n", pFlinkAddress, pFlinkOldValue, pFlinkNewValue));
	GhpAddAndLinkHideAddresses(pList, NULL, pFlinkAddress, (ULONG_PTR) pFlinkOldValue, (ULONG_PTR) pFlinkNewValue);

	// fix blink pointers
	// target->flink->blink = target->blink
	// watch out for Blink offset (+ sizeof(PLIST_ENTRY) )!
	PVOID pBlinkAddress = (PVOID) ((ULONG_PTR) pTargetObjectListEntry->Flink + sizeof(PLIST_ENTRY));
	PVOID pBlinkOldValue = pTargetObjectListEntry->Flink->Blink;
	PVOID pBlinkNewValue = pTargetObjectListEntry->Blink;
	KdPrint(("[DEBUG] Target object blink @ 0x%x -- old value = 0x%x, new value = 0x%x\n", pBlinkAddress, pBlinkOldValue, pBlinkNewValue));
	GhpAddAndLinkHideAddresses(pList, NULL, pBlinkAddress, (ULONG_PTR) pBlinkOldValue, (ULONG_PTR) pBlinkNewValue);
}

VOID GhAddUnicodeStringAddress(IN OUT PSORTED_LIST pList, const IN PVOID pUnicodeString)
{
	if(pList == NULL)
	{
		KdPrint(("[DEBUG] ERROR - Hide list is not initialized -- address to be hidden cannot be added to the list\n"));
		return;
	}

	if(pUnicodeString == NULL)
	{
		KdPrint(("[DEBUG] ERROR - Pointer to UNICODE_STRING cannot be NULL\n"));
		return;
	}

	PUNICODE_STRING pUString = (PUNICODE_STRING) pUnicodeString;

	KdPrint(("[DEBUG] Adding unicode string '%wZ' to hide list\n", pUString));

	// delete unicode string struct
	GhAddRangeForHiding(pList, NULL, pUnicodeString, sizeof(UNICODE_STRING));

	// delete the string itself
	GhAddRangeForHiding(pList, NULL, (PVOID) pUString->Buffer, pUString->Length);
}

VOID GhpAddHideEntry( IN OUT PSORTED_LIST pList, const IN PVOID pPhysicalAddress, const IN PVOID pVirtualAddress,
					 const IN PVOID pOldMemContents, const IN PVOID pNewMemContents, const IN ULONG uSize, const IN HIDE_TYPE type)
{
	ASSERTMSG("Hide list must not be NULL!", pList != NULL);

	PVOID pPhysAddress = pPhysicalAddress;
	if(pPhysAddress == NULL)
	{
		pPhysAddress = GhpGetPhysicalAddress(pVirtualAddress);
	}

	PHIDE_ADDRESS_ENTRY pHideEntry = (PHIDE_ADDRESS_ENTRY) ExAllocatePoolWithTag(NonPagedPool, sizeof(HIDE_ADDRESS_ENTRY), TAG_HIDE_ADDRESS_ENTRY);
	if(pHideEntry == NULL)
	{
		KdPrint(("[DEBUG] WARNING - Cannot allocate new HIDE_ADDRESS_ENTRY -- VA 0x%x will not be added to the list...\n", pVirtualAddress));
		return;
	}

	pHideEntry->pPhysicalAddress = pPhysAddress;
	pHideEntry->ListEntry.uSortValue = (ULONG_PTR) pPhysAddress;
	pHideEntry->pVirtualAddress = pVirtualAddress;
	pHideEntry->pOldMemContents = pOldMemContents;
	pHideEntry->pNewMemContents = pNewMemContents;
	pHideEntry->uSize = uSize;
	pHideEntry->type = type;

	ASSERTMSG("Sorted list lock acquire must occur at or below APC_LEVEL", KeGetCurrentIrql() <= APC_LEVEL);
	SortedListWriteLock(pList);
	BOOLEAN bEntryAdded = SortedListAddEntry(pList, (PSORTED_LIST_ENTRY) pHideEntry);
	ASSERTMSG("Sorted list lock release must occur at or below DISPATCH_LEVEL", KeGetCurrentIrql() <= DISPATCH_LEVEL);
	SortedListWriteUnlock(pList);

	// if entry is not added to the list (i.e. a duplicate exists) ERASE THE ENTRY IMMEDIATELY, since it is not handled by the list cleanup routines
	if(!bEntryAdded)
	{
		// check if old/new memory contents exist
		if(pHideEntry->pOldMemContents != NULL)
		{
			ExFreePoolWithTag(pHideEntry->pOldMemContents, TAG_HIDE_ARRAY);
		}

		if(pHideEntry->pNewMemContents != NULL)
		{
			ExFreePoolWithTag(pHideEntry->pNewMemContents, TAG_HIDE_ARRAY);
		}

		ExFreePoolWithTag(pHideEntry, TAG_HIDE_ADDRESS_ENTRY);
	}
}

VOID GhpAddAndLinkHideAddresses(IN OUT PSORTED_LIST pList, const IN PVOID pPhysicalAddress, const IN PVOID pVirtualAddress, 
								const IN ULONG_PTR uOldValue, const IN ULONG_PTR uNewValue)
{
	ASSERTMSG("Hide list must not be NULL!", pList != NULL);

	PHIDE_ADDRESS_ENTRY pEntry = NULL;

	// use write lock for synchronizing access, since elements will be deleted if they're found
	ASSERTMSG("Sorted list lock acquire must occur at or below APC_LEVEL", KeGetCurrentIrql() <= APC_LEVEL);
	SortedListWriteLock(pList);

	ULONG_PTR uOld = uOldValue;
	ULONG_PTR uNew = uNewValue;

	// go through the list of objects...
	while((pEntry = (PHIDE_ADDRESS_ENTRY) SortedListGetNext(pList, (PSORTED_LIST_ENTRY) pEntry)) != NULL)
	{
		// don't bother with DEL entries, since they indicate that entire range is being deleted
		if(pEntry->type == DEL)
		{
			continue;
		}

		// interested only in entries that have replacement buffers of the correct size (4 bytes for 32-bit architecture or 8 bytes for 64-bit architecture)
		if(pEntry->uSize != sizeof(ULONG_PTR))
		{
			continue;
		}

		// skip the entries that don't have old and new memory contents remembered (this should not happen!)
		if(pEntry->pOldMemContents == NULL && pEntry->pNewMemContents == NULL)
		{
			continue;
		}

		// get the old and new contents
		ULONG_PTR uOldMemContents = *((PULONG_PTR) pEntry->pOldMemContents);
		ULONG_PTR uNewMemContents = *((PULONG_PTR) pEntry->pNewMemContents);

		if((ULONG_PTR) pVirtualAddress == uOldMemContents || (ULONG_PTR) pEntry->pVirtualAddress == uOld)
		{
			// check if the new value (i.e. address) being added is equal to old address of already present entry
			if(uNew == uOldMemContents)
			{
				KdPrint(("[DEBUG] Found entry with virtual address 0x%x which has old value (0x%x) same to the new value being added\n", pEntry->pVirtualAddress, uNew));
				uNew = uNewMemContents;

				// the entry present is no longer needed, it can be safely deleted 
				SortedListRemoveEntry(pList, (PSORTED_LIST_ENTRY) pEntry);
				break;
			}

			if(uOld == uNewMemContents)
			{
				KdPrint(("[DEBUG] Found entry with virtual address 0x%x which has new value (0x%x) same to the old value being added\n", pEntry->pVirtualAddress, uOld));
				*((PULONG_PTR) pEntry->pNewMemContents) = uNew;
				// the correct entry is already present in the list, don't add the new one!
				ASSERTMSG("Sorted list lock release must occur at or below DISPATCH_LEVEL", KeGetCurrentIrql() <= DISPATCH_LEVEL);
				SortedListWriteUnlock(pList);
				return;
			}
		}
	}

	// allocate memory for old and new contents and add new entry to the list
	PULONG_PTR pOldMemory = (PULONG_PTR) ExAllocatePoolWithTag(NonPagedPool, sizeof(ULONG_PTR), TAG_HIDE_ARRAY);
	PULONG_PTR pNewMemory = (PULONG_PTR) ExAllocatePoolWithTag(NonPagedPool, sizeof(ULONG_PTR), TAG_HIDE_ARRAY); 
	if(pOldMemory == NULL || pNewMemory == NULL)
	{
		KdPrint(("[DEBUG] WARNING - Cannot allocate new HIDE_ARRAY -- VA 0x%x will not be added to the list...\n", pVirtualAddress));
		return;
	}

	// write new contents
	*pOldMemory = uOld;
	*pNewMemory = uNew;

	GhpAddHideEntry(pList, pPhysicalAddress, pVirtualAddress, pOldMemory, pNewMemory, sizeof(ULONG_PTR), REPLACE);

	ASSERTMSG("Sorted list lock release must occur at or below DISPATCH_LEVEL", KeGetCurrentIrql() <= DISPATCH_LEVEL);
	SortedListWriteUnlock(pList);
}

PVOID GhpGetPhysicalAddress(const IN PVOID pVirtualAddress)
{
	LARGE_INTEGER liPhysicalAddress;
	PVOID pPhysAddress = NULL;

	// physical address not calculated, perform virtual->physical translation
	liPhysicalAddress = (LARGE_INTEGER) MmGetPhysicalAddress(pVirtualAddress);
#ifdef _WIN64
	// take the complete 64-bit value on x64 architecture
	pPhysAddress = (PVOID) liPhysicalAddress.QuadPart;
#else // _WIN32
	// on 32-bit systems, take the low part
	pPhysAddress = (PVOID) liPhysicalAddress.LowPart;
#endif // _WIN64

	return pPhysAddress;
}