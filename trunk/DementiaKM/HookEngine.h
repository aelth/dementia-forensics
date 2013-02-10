#ifndef __HOOKENGINE_H_VERSION__
#define __HOOKENGINE_H_VERSION__ 100

#if defined(_MSC_VER) && (_MSC_VER >= 1020)
#pragma once
#endif

#ifdef __cplusplus
extern "C"{
#endif
#include <ntddk.h>
#ifdef __cplusplus
};
#endif

#define DEFINE_HOOK_CALL(Name, Arguments, CallBuffer) \
	__declspec(naked) Name(Arguments) \
{ \
	__asm mov   eax, [CallBuffer] \
	__asm jmp   eax \
} \

#define TAG_EXEC_BUFFER ('bXE')

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

	VOID		HkInit(VOID);
	VOID		HkUnInit(VOID);
	BOOLEAN		HkIsInitialized(void);

	NTSTATUS	HkHook(IN PVOID pOriginalFunction, IN PVOID pNewFunction, OUT PUCHAR *ppHookedFunctionCallBuffer);
	NTSTATUS	HkMapMDL(IN PVOID pAddressToMap, IN ULONG uBufferLength, IN KPROCESSOR_MODE mode, OUT PMDL *ppMDL, OUT PVOID *ppMappedAddress);
	NTSTATUS	HkUnhook(IN PVOID pOriginalFunction);

#ifdef __cplusplus
}; // extern "C"
#endif // __cplusplus

#endif // __HOOKENGINE_H_VERSION__