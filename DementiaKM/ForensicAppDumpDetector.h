#ifndef __FORENSICAPPDUMPDETECTOR_H_VERSION__
#define __FORENSICAPPDUMPDETECTOR_H_VERSION__ 100

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

	BOOLEAN FddInit(VOID);
	BOOLEAN FddInitSymbols(VOID);
	VOID FddUnInit(VOID);

	// same arguments as NtWriteFile
	BOOLEAN FddIsForensicAppDumpFile(	IN HANDLE FileHandle, 
										OUT PIO_STATUS_BLOCK IoStatusBlock,
										IN PVOID Buffer,
										IN ULONG Length,
										IN PLARGE_INTEGER ByteOffset
									);
#ifdef __cplusplus
}; // extern "C"
#endif // __cplusplus

#endif // __FORENSICAPPDUMPDETECTOR_H_VERSION__