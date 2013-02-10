#ifndef __DRIVERHIDER_H_VERSION__
#define __DRIVERHIDER_H_VERSION__ 100

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

	NTSTATUS DhInit(VOID);
	BOOLEAN DhInitSymbols(VOID);
	VOID DhUnInit(VOID);

	BOOLEAN DhAddTargetDriver(const IN char *pszDriverName);
	BOOLEAN DhFindHideAddreses(IN OUT PSORTED_LIST pList);

#ifdef __cplusplus
}; // extern "C"
#endif // __cplusplus

#endif // __DRIVERHIDER_H_VERSION__