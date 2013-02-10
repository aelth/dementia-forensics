#ifndef __ALLOCATIONHIDER_H_VERSION__
#define __ALLOCATIONHIDER_H_VERSION__ 100

#ifdef __cplusplus
extern "C"{
#endif // __cplusplus
#include <ntddk.h>
#include "SortedList.h"
#ifdef __cplusplus
};
#endif // __cplusplus

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

	NTSTATUS AhInit(VOID);
	BOOLEAN AhInitSymbols(VOID);
	VOID AhUnInit(VOID);
	BOOLEAN	AhIsInitialized(VOID);

	// adds allocation address and size to the sorted list
	BOOLEAN AhAddAllocation(IN OUT PSORTED_LIST pList, const IN PVOID pObject, const IN ULONG uAllocationTag, const IN ULONG uTagMask = 0xFFFFFFFF);

#ifdef __cplusplus
}; // extern "C"
#endif // __cplusplus

#endif // __ALLOCATIONHIDER_H_VERSION__