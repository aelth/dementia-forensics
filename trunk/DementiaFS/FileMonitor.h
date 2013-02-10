#ifndef __FILEMONITOR_H_VERSION__
#define __FILEMONITOR_H_VERSION__ 100

#ifdef __cplusplus
extern "C"{
#endif // __cplusplus
#include <fltKernel.h>
#include <dontuse.h>
#include <suppress.h>
#include "../Common/CommonTypesDrv.h"
#ifdef __cplusplus
};
#endif // __cplusplus

#define TAG_TARGET_OBJECTS_ARRAY ('rraT')

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

	NTSTATUS FmInit(VOID);
	VOID FmUnInit(VOID);
	NTSTATUS FmStartHiding(IN PTARGET_OBJECT pTargetObjectArray, IN ULONG uArraySize);
	FLT_PREOP_CALLBACK_STATUS FmPreWrite(IN OUT PFLT_CALLBACK_DATA Data, IN PCFLT_RELATED_OBJECTS FltObjects, OUT OPTIONAL PVOID *CompletionContext);

#ifdef __cplusplus
}; // extern "C"
#endif // __cplusplus

#endif // __FILEMONITOR_H_VERSION__