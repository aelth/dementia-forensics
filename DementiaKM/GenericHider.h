#ifndef __GENERICHIDER_H_VERSION__
#define __GENERICHIDER_H_VERSION__ 100

#ifdef __cplusplus
extern "C"{
#endif // __cplusplus
#include <ntddk.h>
#include "SortedList.h"
#include "HideEntry.h"
#ifdef __cplusplus
};
#endif // __cplusplus

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

	typedef enum _OP_TYPE
	{
		DEC,					// decrement the value present in the list
		INC,					// increment the value present in the list
		REP,					// replace the value in the list with the value passed as an argument
	} OP_TYPE;

	// simple wrapper around internal ("private") GhpAddHideEntry function
	// this function creates necessary "hide arrays" using values (up to 64 bits) passed as an argument
	VOID GhAddReplacementValues(IN OUT PSORTED_LIST pList, 
								const IN PVOID pPhysicalAddress, 
								const IN PVOID pVirtualAddress,
								const IN ULONG_PTR uOldValue,
								const IN ULONG_PTR uNewValue,
								const IN ULONG uReplaceSize
								);

	// same function as above, but instead of using values, this function is using the buffers created by the caller
	VOID GhAddReplacementBuffers(	IN OUT PSORTED_LIST pList, 
									const IN PVOID pPhysicalAddress, 
									const IN PVOID pVirtualAddress,
									const IN PVOID pOldMemoryContents,
									const IN PVOID pNewMemoryContents,
									const IN ULONG uReplaceSize
									);

	// function for "deleting" (erasing) entire memory range starting at pVirtualAddress and uSize long
	VOID GhAddRangeForHiding(	IN OUT PSORTED_LIST pList, 
								const IN PVOID pPhysicalAddress, 
								const IN PVOID pVirtualAddress,
								const IN ULONG uSize
							);

	// this function is used when address/value to be hidden is influenced by multiple "objects"
	// for example, a global thread count should be decremented by the number of threads to be hidden
	// this function ensures that correct value will be written in the dump
	// if element is not found in the list, value passed as an argument is stored in the list
	// this function is not applicable to hide entry of HIDE_TYPE other than REPLACE!
	// WARNING - very ugly function, needs to be refactored!
	VOID GhAddGlobalHideAddress(IN OUT PSORTED_LIST pList, 
								const IN PVOID pPhysicalAddress, 
								const IN PVOID pVirtualAddress,
								const IN ULONG_PTR uOldValue,
								const IN ULONG_PTR uNewValue,
								const IN ULONG uReplaceSize,
								const IN OP_TYPE operation
								);

	// when hiding multiple objects that are linked together in the objects-list, flink/blink pointer re-linking 
	// won't work properly and some of the objects will be visible
	// this "diagram" explains the situation:
	// imagine 2 target objects: T1 and T2
	// A		T1		T2		B
	//	f: T1	  f: T2	  f: B	 f: Y
	//  b: X	  b: A	  b: T1	 b: T2
	//
	// in order to hide T1 and T2, A(f) must point to B and B(b) must point to A
	// during the traversal, A(f) is updated to T2, which will be deleted later
	// GhAddAndLinkHideAddresses function ensures that A(f) points to B and (in the same manner) B(b) points to A
	VOID GhModifyListFlinkBlinkPointers(IN OUT PSORTED_LIST pList, const IN PVOID pObject, const IN ULONG uObjectListOffset);

	VOID GhAddUnicodeStringAddress(IN OUT PSORTED_LIST pList, const IN PVOID pUnicodeString);
#ifdef __cplusplus
}; // extern "C"
#endif // __cplusplus

#endif // __GENERICHIDER_H_VERSION__