#ifndef __CONTROLAREAHIDER_H_VERSION__
#define __CONTROLAREAHIDER_H_VERSION__ 100

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

	NTSTATUS CAhInit(VOID);
	BOOLEAN CAhInitSymbols(VOID);
	VOID CAhUnInit(VOID);

	BOOLEAN CAhShouldHideRegion(IN OUT PSORTED_LIST pList, const IN PVOID pControlArea);
	BOOLEAN CAhHideControlArea(IN OUT PSORTED_LIST pList, const IN PVOID pControlArea);

#ifdef __cplusplus
}; // extern "C"
#endif // __cplusplus

#endif // __CONTROLAREAHIDER_H_VERSION__