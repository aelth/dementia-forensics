#include "VADHider.h"
#include "VADHiderPrivateIncludes.h"
#include "SymbolWrapper.h"
#include "AllocationHider.h"
#include "ControlAreaHider.h"
#include "GenericHider.h"
#include "WinVerProvider.h"

// boolean representing status of VADHider
static BOOLEAN bIsVADHiderInitialized = FALSE;

// flag which determines the state of the internal symbols
static BOOLEAN bSymbolsInitialized = FALSE;

// offsets of right and left child inside MMVAD structure which represents AVL tree
static ULONG uMMVADLeftChildOffset = -1;
static ULONG uMMVADRightChildOffset = -1;

// offsets of starting and ending address of the memory "region" that a particular VAD represents
static ULONG uMMVADStartingVpnOffset = -1;
static ULONG uMMVADEndingVpnOffset = -1;

// offset of ControlArea pointer - this member exists for MMVAD/MMVAD_LONG structure only and only on OS below Vista
static ULONG uMMVADControlAreaOffset = -1;

// offset of Subsection pointer - this member exists for MMVAD/MMVAD_LONG structure only and only on Windows Vista and above
static ULONG uMMVADSubsectionOffset = -1;

// offset of BalancedRoot member inside MM_AVL_TABLE structure - VadRoot from EPROCESS points to this structure on Vista and above
static ULONG uMMAVLTableBalancedRootOffset = -1;

// offset of ControlArea pointer inside SUBSECTION structure - this is needed only on Windows Vista and above
static ULONG uSubsectionControlAreaOffset = -1;

// offset of the member containing the VAD flags
static ULONG uMMVADFlagsOffset = -1;

// bit flags that represent the important VAD flags
static ULONG uMMVADFlagsPrivateMemoryBitPos = -1;
static ULONG uMMVADFlagsPrivateMemoryBitLen = -1;

static ULONG uMMVADFlagsCommitChargeBitPos = -1;
static ULONG uMMVADFlagsCommitChargeBitLen = -1;

// addresses of KeStackAttachProcess/KeUnstackDetachProcess functions
static KESTACKATTACHPROCESS pKeStackAttachProcess = NULL;
static KEUNSTACKDETACHPROCESS pKeUnstackDetachProcess = NULL;

// preorder traversal of VAD AVL tree
BOOLEAN VADhpTraverseAVLTree(IN OUT PSORTED_LIST, IN PVOID, const IN PEPROCESS, const IN PVOID);
BOOLEAN VADhpHideVadPoolAllocation(IN OUT PSORTED_LIST, IN PVOID);
VOID VADhpHideVadRegion(IN OUT PSORTED_LIST, const IN PEPROCESS, const IN ULONG_PTR, const IN ULONG_PTR, const IN ULONG_PTR);
PVOID VADhpGetControlArea(const IN PVOID);
BOOLEAN VADhpIsPrivateMemory(const IN ULONG_PTR);
ULONG VADhpGetNumOfCommitedPages(const IN ULONG_PTR);
ULONG_PTR VADhpCheckFlag(const IN ULONG_PTR, const IN ULONG, const IN ULONG);

NTSTATUS VADhInit(VOID)
{
	KeEnterCriticalRegion();
	if(bIsVADHiderInitialized == TRUE)
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

	// initialize ControlAreaHider
	if(!NT_SUCCESS(CAhInit()))
	{
		KdPrint(("[DEBUG] WARNING - Cannot initialize ControlAreaHider - Control Areas of shared objects won't be hidden!\n"));
	}

	// add all necessary symbols
	if(	!SymWAddSymbol("KeStackAttachProcess", -1, -1, -1, -1) ||
		!SymWAddSymbol("KeUnstackDetachProcess", -1, -1, -1, -1) ||
		!SymWAddSymbol("_MMVAD.LeftChild", -1, -1, -1, -1) ||
		!SymWAddSymbol("_MMVAD.RightChild", -1, -1, -1, -1) ||
		!SymWAddSymbol("_MMVAD.StartingVpn", -1, -1, -1, -1) ||
		!SymWAddSymbol("_MMVAD.EndingVpn", -1, -1, -1, -1) ||
		!SymWAddSymbol("_MMVAD.u", -1, -1, -1, -1) ||
		// get bit position and length
		!SymWAddSymbol("_MMVAD_FLAGS.PrivateMemory", -1, -1, 0, 0) ||
		!SymWAddSymbol("_MMVAD_FLAGS.CommitCharge", -1, -1, 0, 0)
		)
	{
		KdPrint(("[DEBUG] ERROR - Error while adding necessary symbols - VAD hider INACTIVE!\n"));
		KeLeaveCriticalRegion();
		return STATUS_NOT_FOUND;
	}

	BOOLEAN bRet = TRUE;
	// Windows Vista and above
	if(WinGetMajorVersion() >= 6)
	{
		bRet &= SymWAddSymbol("_MMVAD.Subsection", -1, -1, -1, -1) &&
				SymWAddSymbol("_MM_AVL_TABLE.BalancedRoot", -1, -1, -1, -1) &&
				SymWAddSymbol("_SUBSECTION.ControlArea", -1, -1, -1, -1);
	}
	// below Vista
	else
	{
		bRet &= SymWAddSymbol("_MMVAD.ControlArea", -1, -1, -1, -1);
	}

	if(!bRet)
	{
		KdPrint(("[DEBUG] ERROR - Error while adding necessary symbols - VAD hider INACTIVE!\n"));
		KeLeaveCriticalRegion();
		return STATUS_NOT_FOUND;
	}

	bIsVADHiderInitialized = TRUE;
	KeLeaveCriticalRegion();

	return STATUS_SUCCESS;
}

BOOLEAN VADhInitSymbols(VOID)
{
	ASSERTMSG("Cannot initialize VAD related symbols because VADHider is not yet initialized", bIsVADHiderInitialized == TRUE);

	KeEnterCriticalRegion();
	if(bSymbolsInitialized == TRUE)
	{
		KeLeaveCriticalRegion();
		return TRUE;
	}

	if(!CAhInitSymbols())
	{
		KdPrint(("[DEBUG] WARNING - Cannot initialize symbols for ControlAreaHider - Control Areas of shared objects won't be hidden!\n"));
	}

	BOOLEAN bRet = TRUE;
	bRet =	SymWInitializeAddress((PVOID *) &pKeStackAttachProcess, "KeStackAttachProcess", TRUE) &&
			SymWInitializeAddress((PVOID *) &pKeUnstackDetachProcess, "KeUnstackDetachProcess", TRUE) &&
			SymWInitializeOffset(&uMMVADLeftChildOffset, "_MMVAD.LeftChild") &&
			SymWInitializeOffset(&uMMVADRightChildOffset, "_MMVAD.RightChild") &&
			SymWInitializeOffset(&uMMVADStartingVpnOffset, "_MMVAD.StartingVpn") &&
			SymWInitializeOffset(&uMMVADEndingVpnOffset, "_MMVAD.EndingVpn") &&
			SymWInitializeOffset(&uMMVADFlagsOffset, "_MMVAD.u") &&
			SymWInitializeBitPosAndLength(&uMMVADFlagsPrivateMemoryBitPos, &uMMVADFlagsPrivateMemoryBitLen, "_MMVAD_FLAGS.PrivateMemory") &&
			SymWInitializeBitPosAndLength(&uMMVADFlagsCommitChargeBitPos, &uMMVADFlagsCommitChargeBitLen, "_MMVAD_FLAGS.CommitCharge");

	// Windows Vista and above
	if(WinGetMajorVersion() >= 6)
	{
		bRet &= SymWInitializeOffset(&uMMVADSubsectionOffset, "_MMVAD.Subsection") &&
				SymWInitializeOffset(&uMMAVLTableBalancedRootOffset, "_MM_AVL_TABLE.BalancedRoot") &&
				SymWInitializeOffset(&uSubsectionControlAreaOffset, "_SUBSECTION.ControlArea");
	}
	else
	{
		bRet &= SymWInitializeOffset(&uMMVADControlAreaOffset, "_MMVAD.ControlArea");
	}

	if(!bRet)
	{
		KdPrint(("[DEBUG] ERROR - Error while initializing offsets or addresses - VAD hider INACTIVE!\n"));
	}

	bSymbolsInitialized = TRUE;
	KeLeaveCriticalRegion();
	return TRUE;
}

VOID VADhUnInit(VOID)
{
	KeEnterCriticalRegion();
	// check if VAD engine has been initialized
	if(bIsVADHiderInitialized == FALSE)
	{
		KeLeaveCriticalRegion();
		return;
	}

	// un-initialize all initialized "hiding engines"
	AhUnInit();
	CAhUnInit();

	bIsVADHiderInitialized = FALSE;
	KeLeaveCriticalRegion();
}

BOOLEAN VADhFindHideAddreses(IN OUT PSORTED_LIST pList, const IN PVOID pVadRoot, const IN PEPROCESS pTargetProcess, const IN PVOID pProcessSectionBase)
{
	ASSERTMSG("Cannot hide VADs because the VADHider is not yet initialized", bIsVADHiderInitialized == TRUE);
	ASSERTMSG("Internal symbols are not initialized!", bSymbolsInitialized == TRUE);

	if(pList == NULL)
	{
		KdPrint(("[DEBUG] ERROR - Hide list is not initialized -- cannot add VAD data to the list\n"));
		return FALSE;
	}

	if(pVadRoot == NULL)
	{
		KdPrint(("[DEBUG] ERROR - VAD root cannot be NULL\n"));
		return FALSE;
	}

	if(pTargetProcess == NULL)
	{
		KdPrint(("[DEBUG] ERROR - Target process cannot be NULL - cannot attach to the target process\n"));
		return FALSE;
	}

	if(pProcessSectionBase == NULL)
	{
		KdPrint(("[DEBUG] WARNING - Base address of the process section in memory cannot be NULL. Will not hide this section...\n"));
	}

	KdPrint(("[DEBUG] Deleting process VADs and private memory regions pointed to by the VADs...\n"));

	BOOLEAN bRet = TRUE;

	// on Vista and above VadRoot does not point directly to the VAD root node, but to MM_AVL_TABLE structure
	if(WinGetMajorVersion() >= 6)
	{
		PVOID pBalancedRoot = (PVOID) SYMW_MEMBER_FROM_OFFSET(pVadRoot, uMMAVLTableBalancedRootOffset, 0);
		PVOID pRealVadRoot = (PVOID) *((PULONG_PTR) SYMW_MEMBER_FROM_OFFSET(pBalancedRoot, uMMVADRightChildOffset, 0));
		
		bRet = VADhpTraverseAVLTree(pList, pRealVadRoot, pTargetProcess, pProcessSectionBase);
	}
	else
	{
		bRet = VADhpTraverseAVLTree(pList, pVadRoot, pTargetProcess, pProcessSectionBase);
	}

	return bRet;
}

BOOLEAN VADhpTraverseAVLTree(IN OUT PSORTED_LIST pList, IN PVOID pVADNode, const IN PEPROCESS pTargetProcess, const IN PVOID pProcessSectionBase)
{
	ASSERTMSG("Passed pointer to hide list is NULL", pList != NULL);

	// first check whether there are no further subnodes (children) of this VAD node
	if(pVADNode == NULL)
	{
		return TRUE;
	}

	// get node parameters
	ULONG_PTR uStartVpn = *((PULONG_PTR) SYMW_MEMBER_FROM_OFFSET(pVADNode, uMMVADStartingVpnOffset, 0));
	ULONG_PTR uEndVpn = *((PULONG_PTR) SYMW_MEMBER_FROM_OFFSET(pVADNode, uMMVADEndingVpnOffset, 0));
	ULONG_PTR uFlags = *((PULONG_PTR) SYMW_MEMBER_FROM_OFFSET(pVADNode, uMMVADFlagsOffset, 0));

	// get real starting and ending address for represented by this VAD
	// this calculation is valid both on 32 and 64 bit systems - upper 20 bits (32-bit system) or upper 52 bits (64-bit system)
	// are represented by the members in the VAD
	uStartVpn = uStartVpn << PAGE_SHIFT;
	uEndVpn = ((uEndVpn + 1) << PAGE_SHIFT) - 1;

	// check if the VAD represents the private memory of the target process
	if(VADhpIsPrivateMemory(uFlags))
	{
		KdPrint(("[DEBUG] VAD @ 0x%p describing the range 0x%p - 0x%p represents the private memory of the target process\n", (ULONG_PTR) pVADNode, uStartVpn, uEndVpn));

		// hide this region
		VADhpHideVadRegion(pList, pTargetProcess, uStartVpn, uEndVpn, uFlags);
	}
	else if((ULONG_PTR) pProcessSectionBase >= uStartVpn && (ULONG_PTR) pProcessSectionBase < uEndVpn)
	{
		// this section represents the process image file (i.e. the process EXE mapped in memory)
		KdPrint(("[DEBUG] VAD @ 0x%p describing the range 0x%p - 0x%p represents the target process image file in memory\n", (ULONG_PTR) pVADNode, uStartVpn, uEndVpn));

		// hide the region
		VADhpHideVadRegion(pList, pTargetProcess, uStartVpn, uEndVpn, uFlags);

		// hide the control area
		PVOID pControlArea = VADhpGetControlArea(pVADNode);
		if(MmIsAddressValid(pControlArea) && pControlArea != NULL)
		{
			CAhHideControlArea(pList, pControlArea);
		}
	}
	else 
	{
		// not private memory of the target process - shared section (or image file/mapping)
		// first check whether CONTROL_AREA pointer is valid
		PVOID pControlArea = VADhpGetControlArea(pVADNode);
		if(MmIsAddressValid(pControlArea) && pControlArea != NULL)
		{
			// check if the control block of this region should be hidden
			if(CAhShouldHideRegion(pList, pControlArea))
			{
				// hide the VAD region
				KdPrint(("[DEBUG] Hiding shared memory range 0x%p - 0x%p because it's used exclusively by the target process\n", uStartVpn, uEndVpn));
				VADhpHideVadRegion(pList, pTargetProcess, uStartVpn, uEndVpn, uFlags);

				// and delete the control area
				CAhHideControlArea(pList, pControlArea);
			}
		}
	}

	// delete the Vad allocation, regardless of the type and attributes
	VADhpHideVadPoolAllocation(pList, pVADNode);

	// obtain node children
	ULONG_PTR uLeftChild = *((PULONG_PTR) SYMW_MEMBER_FROM_OFFSET(pVADNode, uMMVADLeftChildOffset, 0));
	ULONG_PTR uRightChild = *((PULONG_PTR) SYMW_MEMBER_FROM_OFFSET(pVADNode, uMMVADRightChildOffset, 0));

	// do a preorder traversal of the tree
	VADhpTraverseAVLTree(pList, (PVOID) uLeftChild, pTargetProcess, pProcessSectionBase);
	VADhpTraverseAVLTree(pList, (PVOID) uRightChild, pTargetProcess, pProcessSectionBase);

	return TRUE;
}

BOOLEAN VADhpHideVadPoolAllocation(IN OUT PSORTED_LIST pList, IN PVOID pVadNode)
{
	ASSERTMSG("Passed pointer to hide list is NULL", pList != NULL);
	ASSERTMSG("Pointer to VAD node cannot be NULL", pVadNode != NULL);

	// delete VAD allocation
	// since it is not known in the advance whether this is short VAD (VadS), "normal" VAD (Vad) or long VAD (Vadl),
	// ignore the MSB of the tag and delete the allocation without regard to the VAD type
	// Pool allocation tags:
	//	VadS = 0x53646156
	//	Vad = 0x20646156	
	//	Vadl = 0x6c646156
	// use tag 0x??646156, where we don't care about the first byte (use appropriate mask)
	return AhAddAllocation(pList, pVadNode, 0x00646156, 0x00FFFFFF);
}

VOID VADhpHideVadRegion(IN OUT PSORTED_LIST pList, const IN PEPROCESS pTargetProcess, const IN ULONG_PTR uStartAddress, const IN ULONG_PTR uEndAddress, const IN ULONG_PTR uFlags)
{
	ASSERTMSG("Passed pointer to hide list is NULL", pList != NULL);
	ASSERTMSG("Passed pointer to target process is NULL", pTargetProcess != NULL);
	ASSERTMSG("Start address cannot be bigger than ending address", uStartAddress < uEndAddress);

	// calculate the number of *commited* pages this region covers
	ULONG uNumOfCommitedPages = VADhpGetNumOfCommitedPages(uFlags);

	// get total number of pages 
	ULONG uRangeSize = uEndAddress - uStartAddress + 1;
	ULONG uTotalNumOfPages = ADDRESS_AND_SIZE_TO_SPAN_PAGES(uStartAddress, uRangeSize);

	KAPC_STATE ApcState;
	// attach to the target process
	ASSERTMSG("Process attach with KeStackAttachProcess must occur below DISPATCH_LEVEL", KeGetCurrentIrql() < DISPATCH_LEVEL);
	pKeStackAttachProcess(pTargetProcess, &ApcState);

	// delete all pages which belong to the region described by the VAD
	// V2P translation will be performed using the current process (i.e. target process) page directory table
	ULONG_PTR uPageStartAddress = uStartAddress;
	ULONG uPagesHidden = 0;
	for(ULONG uPage = 0; uPage < uTotalNumOfPages; uPage++)
	{
		if(uPagesHidden == uNumOfCommitedPages)
		{
			// all pages have been hidden, exit
			break;
		}

		// check if this page represents committed and "usable" memory
		if(MmIsAddressValid((PVOID) uPageStartAddress))
		{
			// hide this page
			GhAddRangeForHiding(pList, NULL, (PVOID) uPageStartAddress, PAGE_SIZE);
			uPagesHidden++;
		}

		// move to the next page
		uPageStartAddress += PAGE_SIZE;
	}

	// detach from the process
	ASSERTMSG("Process detach with KeUnstackDetachProcess must occur below DISPATCH_LEVEL", KeGetCurrentIrql() < DISPATCH_LEVEL);
	pKeUnstackDetachProcess(&ApcState);
}

PVOID VADhpGetControlArea(const IN PVOID pVadNode)
{
	ASSERTMSG("Pointer to VAD node cannot be NULL", pVadNode != NULL);

	PVOID pControlArea = NULL;

	// if Windows Vista and above
	if(WinGetMajorVersion() >= 6)
	{
		// MMVAD has no direct CONTROL_AREA pointer - instead, it has a pointer to SUBSECTION
		PVOID pSubsection = (PVOID) *((PULONG_PTR) SYMW_MEMBER_FROM_OFFSET(pVadNode, uMMVADSubsectionOffset, 0));
		
		// subsection has pointer back to CONTROL_AREA
		pControlArea = (PVOID) *((PULONG_PTR) SYMW_MEMBER_FROM_OFFSET(pSubsection, uSubsectionControlAreaOffset, 0));
	}
	else
	{
		// directly use the member inside MMVAD structure
		pControlArea = (PVOID) *((PULONG_PTR) SYMW_MEMBER_FROM_OFFSET(pVadNode, uMMVADControlAreaOffset, 0));
	}

	return pControlArea;
}

BOOLEAN VADhpIsPrivateMemory(const IN ULONG_PTR uFlags)
{
	return (BOOLEAN) VADhpCheckFlag(uFlags, uMMVADFlagsPrivateMemoryBitPos, uMMVADFlagsPrivateMemoryBitLen);
}

ULONG VADhpGetNumOfCommitedPages(const IN ULONG_PTR uFlags)
{
	return VADhpCheckFlag(uFlags, uMMVADFlagsCommitChargeBitPos, uMMVADFlagsCommitChargeBitLen);
}

ULONG_PTR VADhpCheckFlag(const IN ULONG_PTR uFlags, const IN ULONG uBitPosition, const IN ULONG uBitLength)
{
	// do some simple error checking
	ASSERTMSG("Bit length cannot be greater than the flags size", uBitLength <= (sizeof(ULONG_PTR) * 8));
	ASSERTMSG("Bit position cannot be greater than the flags size", uBitPosition <= (sizeof(ULONG_PTR) * 8));
	ASSERTMSG("Bit position or length invalid - the value lies outside of the range of flags \"register\"", (uBitLength + uBitPosition) <= (sizeof(ULONG_PTR) * 8));

	return ((uFlags >> uBitPosition) & ((1 << uBitLength) - 1));
}