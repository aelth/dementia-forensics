#ifndef __VADHIDER_H_VERSION__
#define __VADHIDER_H_VERSION__ 100

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

	NTSTATUS VADhInit(VOID);
	BOOLEAN VADhInitSymbols(VOID);
	VOID VADhUnInit(VOID);

	BOOLEAN VADhFindHideAddreses(IN OUT PSORTED_LIST pList, const IN PVOID pVadRoot, const IN PEPROCESS pTargetProcess, const IN PVOID pProcessSectionBase);

#ifdef __cplusplus
}; // extern "C"
#endif // __cplusplus

#endif // __VADHIDER_H_VERSION__