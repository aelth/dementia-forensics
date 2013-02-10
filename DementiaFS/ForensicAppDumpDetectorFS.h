#ifndef __FORENSICAPPDUMPDETECTORFS_H_VERSION__
#define __FORENSICAPPDUMPDETECTORFS_H_VERSION__ 100

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

	// specific version of forensic application dump file detection - this version is used when minifilter is used for intercepting
	// write operations
	BOOLEAN FddIsForensicAppDumpFile(const IN PFILE_OBJECT pFileObject, const IN PUNICODE_STRING pusFilePath, const ULONG uLength, const IN KPROCESSOR_MODE mode);

#ifdef __cplusplus
}; // extern "C"
#endif // __cplusplus

#endif // __FORENSICAPPDUMPDETECTORFS_H_VERSION__