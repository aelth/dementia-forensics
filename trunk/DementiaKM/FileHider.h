#ifndef __FILEHIDER_H_VERSION__
#define __FILEHIDER_H_VERSION__ 100

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

	NTSTATUS FhInit(VOID);
	BOOLEAN FhInitSymbols(VOID);
	VOID FhUnInit(VOID);

	BOOLEAN FhFindHideAddreses(IN OUT PSORTED_LIST pList, const IN PFILE_OBJECT pFileObject);

#ifdef __cplusplus
}; // extern "C"
#endif // __cplusplus

#endif // __FILEHIDER_H_VERSION__