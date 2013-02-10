#ifndef __HIDEENTRY_H_VERSION__
#define __HIDEENTRY_H_VERSION__ 100

#include <ntddk.h>
#include "SortedList.h"

#define TAG_HIDE_ADDRESS_ENTRY ('EdiH')
#define TAG_HIDE_ARRAY ('AdiH')

typedef enum _HIDE_TYPE
{
	REPLACE,
	REPLACE_NATIVE_POINTER,
	DEL,
} HIDE_TYPE;

typedef struct _HIDE_ADDRESS_ENTRY
{
	SORTED_LIST_ENTRY	ListEntry;
	PVOID				pPhysicalAddress;
	PVOID				pVirtualAddress;
	//ULONG				uOldValue;
	//ULONG				uNewValue;
	//ULONG_PTR			uOldNativePointerValue;
	//ULONG_PTR			uNewNativePointerValue;
	PVOID				pOldMemContents;
	PVOID				pNewMemContents;
	ULONG				uSize;
	HIDE_TYPE			type;
} HIDE_ADDRESS_ENTRY, *PHIDE_ADDRESS_ENTRY;

#endif // __HIDEENTRY_H_VERSION__