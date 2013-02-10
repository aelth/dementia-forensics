#ifndef __OBJECTHIDER_H_VERSION__
#define __OBJECTHIDER_H_VERSION__ 100

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

	NTSTATUS OhInit(VOID);
	BOOLEAN OhInitSymbols(VOID);
	
	VOID OhUnInit(VOID);

	BOOLEAN OhHideTargetProcessHandleTable(IN OUT PSORTED_LIST pList, const IN PEPROCESS pTargetProcess);
	BOOLEAN OhHideProcessHandles(IN OUT PSORTED_LIST pList, const IN PEPROCESS pTargetProcess);
	BOOLEAN OhHidePspCidTableHandle(IN OUT PSORTED_LIST pList, const IN HANDLE hTargetPIDorTID, const IN PVOID pObject);
	BOOLEAN OhHideCsrssProcessHandles(IN OUT PSORTED_LIST pList, const IN PEPROCESS pCsrssProcess, const IN PEPROCESS pTargetProcess);

#ifdef __cplusplus
}; // extern "C"
#endif // __cplusplus

#endif // __OBJECTHIDER_H_VERSION__