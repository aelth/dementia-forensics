#ifndef __PROCESSHIDER_H_VERSION__
#define __PROCESSHIDER_H_VERSION__ 100

#ifdef __cplusplus
extern "C"{
#endif // __cplusplus
#include <ntddk.h>
#include "../Common/CommonTypesDrv.h"
#include "SortedList.h"
#ifdef __cplusplus
};
#endif // __cplusplus

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

	NTSTATUS PhInit(VOID);
	BOOLEAN PhInitSymbols(VOID);
	VOID PhUnInit(VOID);

	BOOLEAN PhAddTargetProcess(const IN PTARGET_OBJECT pTargetObject);
	BOOLEAN PhFindHideAddreses(IN OUT PSORTED_LIST pList);

	// exported functions for locking/unlocking process
	BOOLEAN PhLockProcess(IN PEPROCESS pProcess);
	VOID PhUnlockProcess(IN PEPROCESS pProcess);

#ifdef __cplusplus
}; // extern "C"
#endif // __cplusplus

#endif // __PROCESSHIDER_H_VERSION__