#include "AllocationHider.h"
#include "SymbolWrapper.h"
#include "GenericHider.h"

// boolean representing status of AllocationHider
static BOOLEAN bIsAllocationHiderInitialized = FALSE;

// flag which determines the state of symbols used in AllocationHider
static BOOLEAN bSymbolsInitialized = FALSE;

// offset of pool tag inside POOL_HEADER
static ULONG uPoolHeaderPoolTagOffset = -1;

// offset of pool size, bit position and bit length inside POOL_HEADER
static ULONG uPoolHeaderBlockSizeOffset = -1;
static ULONG uPoolHeaderBlockSizeBitPos = -1;
static ULONG uPoolHeaderBlockSizeBitLen = -1;

BOOLEAN AhpInitializeOffsetsAndAddresses(VOID);

NTSTATUS AhInit(VOID)
{
	KeEnterCriticalRegion();
	if(bIsAllocationHiderInitialized == TRUE)
	{
		KeLeaveCriticalRegion();
		return STATUS_SUCCESS;
	}

	if(!SymWAddSymbol("_POOL_HEADER.PoolTag", -1, -1, -1, -1) ||
		// apart from blocksize offset, we need bit position and length
		!SymWAddSymbol("_POOL_HEADER.BlockSize", -1, -1, 0, 0)
	  )
	{
		KdPrint(("[DEBUG] ERROR - Error while adding necessary symbols - allocation hider INACTIVE!\n"));
		KeLeaveCriticalRegion();
		return STATUS_NOT_FOUND;
	}

	bIsAllocationHiderInitialized = TRUE;
	KeLeaveCriticalRegion();

	return STATUS_SUCCESS;
}

BOOLEAN AhIsInitialized(VOID)
{
	return bIsAllocationHiderInitialized;
}

BOOLEAN AhInitSymbols(VOID)
{
	ASSERTMSG("Cannot hide allocation because the AllocationHider is not yet initialized", bIsAllocationHiderInitialized == TRUE);

	KeEnterCriticalRegion();
	// first check if symbols have already been initialized
	if(bSymbolsInitialized == TRUE)
	{
		KeLeaveCriticalRegion();
		return TRUE;
	}

	BOOLEAN bRet = TRUE;
	bRet =	SymWInitializeOffset(&uPoolHeaderPoolTagOffset, "_POOL_HEADER.PoolTag") &&
			SymWInitializeOffset(&uPoolHeaderBlockSizeOffset, "_POOL_HEADER.BlockSize") &&
			SymWInitializeBitPosAndLength(&uPoolHeaderBlockSizeBitPos, &uPoolHeaderBlockSizeBitLen, "_POOL_HEADER.BlockSize");

	if(!bRet)
	{
		KdPrint(("[DEBUG] ERROR - Error while initializing offsets or addresses - allocation hider INACTIVE!\n"));
	}

	bSymbolsInitialized = TRUE;
	KeLeaveCriticalRegion();

	return bRet;
}

VOID AhUnInit(VOID)
{
	KeEnterCriticalRegion();
	// check if engine has been initialized
	if(bIsAllocationHiderInitialized == FALSE)
	{
		KeLeaveCriticalRegion();
		return;
	}

	bIsAllocationHiderInitialized = FALSE;
	KeLeaveCriticalRegion();
}

BOOLEAN AhAddAllocation(IN OUT PSORTED_LIST pList, const IN PVOID pObject, const IN ULONG uAllocationTag, const IN ULONG uTagMask)
{
	ASSERTMSG("Cannot hide allocation because the AllocationHider is not yet initialized", bIsAllocationHiderInitialized == TRUE);
	ASSERTMSG("Internal symbols are not initialized!", bSymbolsInitialized == TRUE);

	if(pList == NULL)
	{
		KdPrint(("[DEBUG] ERROR - Hide list is not initialized -- address of the allocation cannot be added to the list\n"));
		return FALSE;
	}

	if(pObject == NULL)
	{
		KdPrint(("[DEBUG] ERROR - Allocation of the NULL object cannot be found\n"));
		return FALSE;
	}

#ifdef _WIN64
	// on 64-bit systems, block size is 32-bit long!
	ULONG tmpAllocationSize = 0;
	ULONG uMultiplier = 0x10;
#else
	USHORT tmpAllocationSize = 0;
	ULONG uMultiplier = 0x8;
#endif
	
	ULONG uAllocationSize = 0;
	PULONG puPossibleTag = NULL;
	PVOID pAlloc = NULL;
	BOOLEAN bRet = FALSE;

	// if mask is specified, only part of the tag should be matched
	// default value of the mask is 0xFFFFFFFF, so the entire tag is compared
	ULONG uTag = uAllocationTag & uTagMask;

	// allocations are always 8-byte aligned
	// for objects, allocations are  usually 0x10 - 0x70 bytes before the object block
	for(ULONG uOffset = 0x8; uOffset <= 0x70; uOffset += 0x8)
	{
		// pool tag offset is always > 0x8 (0x4 from Windows 2000 onwards)
		pAlloc = (PVOID) SYMW_MEMBER_FROM_OFFSET(pObject, 0, uOffset);
		puPossibleTag = (PULONG) SYMW_MEMBER_FROM_OFFSET(pAlloc, uPoolHeaderPoolTagOffset, 0);

		// the addresses can sometimes be paged - especially segment addresses on Windows 7
		// check whether the address is valid
		if(MmIsAddressValid(puPossibleTag))
		{
			// apply the mask
			ULONG uMatchTag = *puPossibleTag & uTagMask;
			if(uMatchTag == uTag)
			{
				// found object allocation - get it's size
#ifdef _WIN64
				tmpAllocationSize = *((PULONG) SYMW_MEMBER_FROM_OFFSET(pAlloc, uPoolHeaderBlockSizeOffset, 0));
#else
				tmpAllocationSize = *((USHORT *) SYMW_MEMBER_FROM_OFFSET(pAlloc, uPoolHeaderBlockSizeOffset, 0));
#endif // _WIN64
				
				uAllocationSize = ((tmpAllocationSize >> uPoolHeaderBlockSizeBitPos) & ((1 << uPoolHeaderBlockSizeBitLen) - 1)) * uMultiplier;
				bRet = TRUE;
				break;
			}
		}		
	}

	// if allocation found, add it to the hide list
	if(bRet)
	{
		// print out the tag and the allocation details
		char szTag[5];
		memset(szTag, 0, 5);

		if(MmIsAddressValid(puPossibleTag))
		{
			szTag[0] = *puPossibleTag & 0xFF;
			szTag[1] = (*puPossibleTag >> 8) & 0xFF;
			szTag[2] = (*puPossibleTag >> 16) & 0xFF;
			szTag[3] = (*puPossibleTag >> 24) & 0xFF;
			szTag[4] = '\0';
		}
		
		KdPrint(("[DEBUG] Adding address: Allocation @ 0x%p, size: 0x%x, tag: %s\n", pAlloc, uAllocationSize, szTag));
		GhAddRangeForHiding(pList, NULL, pAlloc, uAllocationSize);
	}
	else
	{
		KdPrint(("[DEBUG] WARNING - Cannot determine address of the object allocation and its size -- alocation won't be hidden!\n"));
	}

	return bRet;
}