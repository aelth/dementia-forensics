#ifndef __HIDEENGINE_H_VERSION__
#define __HIDEENGINE_H_VERSION__ 100

#ifdef __cplusplus
extern "C"{
#endif // __cplusplus
#include <ntddk.h>
#include "../Common/CommonTypesDrv.h"
#ifdef __cplusplus
};
#endif // __cplusplus

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

	NTSTATUS HidInit(VOID);
	NTSTATUS HidStartHiding(IN PTARGET_OBJECT pTargetObjectArray, IN ULONG uArraySize, IN PKEVENT pFinishedEvent);
	VOID HidUnInit(VOID);
	BOOLEAN HidIsInitialized(VOID);

#ifdef __cplusplus
}; // extern "C"
#endif // __cplusplus

#endif // __HIDEENGINE_H_VERSION__