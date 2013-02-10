#ifndef __WINVERPROVIDER_H_VERSION__
#define __WINVERPROVIDER_H_VERSION__ 100

#ifdef __cplusplus
extern "C"{
#endif // __cplusplus
#include <ntddk.h>
#ifdef __cplusplus
};
#endif // __cplusplus

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

	ULONG WinGetMajorVersion(VOID);
	ULONG WinGetMinorVersion(VOID);

#ifdef __cplusplus
}; // extern "C"
#endif // __cplusplus

#endif // __WINVERPROVIDER_H_VERSION__