#include "ForensicAppDumpDetector.h"
#include "SymbolWrapper.h"

// boolean representing status of ForensicAppDumpDetector
static BOOLEAN bIsForensicAppDumpDetectorInitialized = FALSE;

// flag which determines the state of symbols used in ForensicAppDumpDetector
static BOOLEAN bSymbolsInitialized = FALSE;

// offset of process name inside EPROCESS block
static ULONG uEPROCImageFileNameOffset = -1;

// offset of LockOperation member inside FILE_OBJECT block
static ULONG uFILEOBJLockOperationOffset = -1;

// define for ObQueryNameString function
typedef NTSTATUS (NTAPI *OBQUERYNAMESTRING) (IN PVOID, OUT POBJECT_NAME_INFORMATION, IN ULONG, OUT PULONG);

// pointer to ObQueryNameString function
static OBQUERYNAMESTRING pObQueryNameString = NULL;

// file object name information tag - 'Fion'
#define TAG_FILE_OBJECT_NAME_INFO ('noiF')

// define file object name information size (complete path) -- 1024 bytes should be enough
#define FILE_OBJECT_NAME_INFO_SIZE	1024

BOOLEAN FddpTestFileFlags(const IN ULONG);
BOOLEAN FddpIsKernelMode(const IN ULONG_PTR);
BOOLEAN FddpIsProcessNameEqual(const IN PCHAR);
//BOOLEAN FddpIsDriverPresent(PCHAR);
BOOLEAN FddpAreStringsEqual(PCHAR, PCHAR);
BOOLEAN FddpAreFlagsEqual(const IN PFILE_OBJECT, const IN UCHAR, const IN UCHAR, const IN UCHAR, const IN UCHAR, const IN UCHAR, const IN UCHAR, const IN UCHAR, const IN UCHAR);
BOOLEAN FddpIsFileExtensionEqual(const IN PFILE_OBJECT, const IN PCHAR);
BOOLEAN FddpIsMemoryze(const IN PFILE_OBJECT);
BOOLEAN FddpIsMDD(const IN PFILE_OBJECT);
BOOLEAN FddpIsWinpmem(const IN PFILE_OBJECT);
BOOLEAN FddpIsFTK(const IN PFILE_OBJECT);
BOOLEAN FddpIsWinen(const IN PFILE_OBJECT);
BOOLEAN FddpIsWin32DD(const IN PFILE_OBJECT);
BOOLEAN FddpIsOSForensics(const IN PFILE_OBJECT);

BOOLEAN FddInit(VOID)
{
	KeEnterCriticalRegion();
	if(bIsForensicAppDumpDetectorInitialized == TRUE)
	{
		KeLeaveCriticalRegion();
		return TRUE;
	}

	// add wanted symbols
	if(	!SymWAddSymbol("ObQueryNameString", -1, -1, -1, -1) ||
		!SymWAddSymbol("_EPROCESS.ImageFileName", -1, -1, -1, -1) ||
		!SymWAddSymbol("_FILE_OBJECT.LockOperation", -1, -1, -1, -1)
	   )
	{
		KdPrint(("[DEBUG] ERROR - Failure while adding ForensicAppDumpDetector symbols...\n"));
		return FALSE;
	}

	bIsForensicAppDumpDetectorInitialized = TRUE;
	KeLeaveCriticalRegion();

	return TRUE;
}

BOOLEAN FddInitSymbols(VOID)
{
	ASSERTMSG("Cannot initialize ForensicAppDumpDetector symbols because it is not yet initialized", bIsForensicAppDumpDetectorInitialized == TRUE);

	KeEnterCriticalRegion();
	if(bSymbolsInitialized == TRUE)
	{
		KeLeaveCriticalRegion();
		return TRUE;
	}

	BOOLEAN bRet = FALSE;
	bRet =	SymWInitializeAddress((PVOID *) &pObQueryNameString, "ObQueryNameString", TRUE) &&
			SymWInitializeOffset(&uEPROCImageFileNameOffset, "_EPROCESS.ImageFileName") &&
			SymWInitializeOffset(&uFILEOBJLockOperationOffset, "_FILE_OBJECT.LockOperation");

	if(!bRet)
	{
		KdPrint(("[DEBUG] ERROR - Cannot find ForensicAppDumpDetector symbols!\n"));
	}

	bSymbolsInitialized = TRUE;
	KeLeaveCriticalRegion();

	return bRet;
}

VOID FddUnInit(VOID)
{
	KeEnterCriticalRegion();
	// check if engine has been initialized
	if(bIsForensicAppDumpDetectorInitialized == FALSE)
	{
		KeLeaveCriticalRegion();
		return;
	}

	bIsForensicAppDumpDetectorInitialized = FALSE;
	KeLeaveCriticalRegion();
}

BOOLEAN FddIsForensicAppDumpFile(	IN HANDLE FileHandle, OUT PIO_STATUS_BLOCK IoStatusBlock, IN PVOID Buffer, 
									IN ULONG Length, IN PLARGE_INTEGER ByteOffset)
{
	ASSERTMSG("Cannot detect forensic applications because ForensicAppDumpDetector is not yet initialized", bIsForensicAppDumpDetectorInitialized == TRUE);
	ASSERTMSG("Internal symbols are not initialized!", bSymbolsInitialized == TRUE);

	PFILE_OBJECT pFileObject = NULL;
	NTSTATUS status = ObReferenceObjectByHandle(FileHandle, 0, *IoFileObjectType, KernelMode, (PVOID *) &pFileObject, NULL);
	if(!NT_SUCCESS(status))
	{
		KdPrint(("[DEBUG] ERROR - Error while obtaining file pointer based on handle... Continuing without file object -- detection will be less precise\n"));
	}

	// perform general tests that eliminate possibility of forensic dump application
	if(pFileObject != NULL)
	{
		// all tested memory acquisition tools have PrivateCacheMap and CompletionContext fields equal to NULL
		if(pFileObject->PrivateCacheMap != NULL || pFileObject->CompletionContext != NULL)
		{
			return FALSE;
		}

		// test the file object flags
		if(!FddpTestFileFlags(pFileObject->Flags))
		{
			return FALSE;
		}
	}

	// perform other tests
	BOOLEAN bRet = FALSE;

	if(	!FddpIsKernelMode((ULONG) FileHandle) &&
		!FddpIsKernelMode((ULONG) IoStatusBlock) &&
		!FddpIsKernelMode((ULONG) Buffer) &&
		ByteOffset == NULL)
	{
		// MDD, Memoryze, FTK Imager and Winen (Encase) all have FileHandle, IoStatusBlock and Buffer in user mode!
		// this practically means that they are all vulnerable to user-mode attacks:)
		// their ByteOffset must be NULL!
		if(Length == 0x1000)
		{
			// buffer length is 0x1000 for Memoryze and MDD
			if(FddpIsMemoryze(pFileObject))
			{
				KdPrint(("[DEBUG] Found Memoryze dump file - handle 0x%x\n", FileHandle));
				bRet = TRUE;
			}
			else if(FddpIsMDD(pFileObject))
			{
				KdPrint(("[DEBUG] Found MDD dump file - handle 0x%x\n", FileHandle));
				bRet = TRUE;
			}
			else if(FddpIsWinpmem(pFileObject))
			{
				KdPrint(("[DEBUG] Found Winpmem dump file - handle 0x%x\n", FileHandle));
				bRet = TRUE;
			}
			else
			{
				// return true anyway -- there is high probability this is MDD or Memoryze file
				KdPrint(("[DEBUG] High probability of Memoryze, MDD or some other memory acquisition application dump file - handle 0x%x\n", FileHandle));
				bRet = TRUE;
			}
		}
		// FTK Imager uses fixed length of 0x8000
		else if(Length == 0x8000)
		{
			if(FddpIsFTK(pFileObject))
			{
				KdPrint(("[DEBUG] Found FTK Imager dump file - handle 0x%x\n", FileHandle));
				bRet = TRUE;
			}
			else
			{
				// return true anyway -- there is high probability this is FTK Imager file
				KdPrint(("[DEBUG] High probability of FTK Imager dump file - handle 0x%x\n", FileHandle));
				bRet = TRUE;
			}
		}
		// Winen uses variable buffer length (but bigger than 0x1000) - this condition also catches all other lengths (and thus a high probability of false positives)
		else if(Length > 0x1000)
		{
			if(FddpIsWinen(pFileObject))
			{
				KdPrint(("[DEBUG] Found Winen dump file - handle 0x%x\n", FileHandle));
				bRet = TRUE;
			}
			
			// else assume it is not a memory acquisition application dump file!
		}
	}
	else if(FddpIsKernelMode((ULONG) FileHandle) &&
			FddpIsKernelMode((ULONG) IoStatusBlock) &&
			FddpIsKernelMode((ULONG) ByteOffset))
	{
		// Win32DD and OSForensics have handle, IOBlock and ByteOffset in kernel mode
		if(FddpIsKernelMode((ULONG) Buffer))
		{
			// Win32DD has all three variables in kernel mode
			// length is variable, but always between 0x1000 & 0x100000
			if(FddpIsWin32DD(pFileObject))
			{
				KdPrint(("[DEBUG] Found Win32dd dump file - handle 0x%x\n", FileHandle));
				bRet = TRUE;
			}
		}
		else
		{
			// OSForensics (interestingly) has buffer in user mode!
			// length is 0x1000
			if(Length == 0x1000)
			{
				if(FddpIsOSForensics(pFileObject))
				{
					KdPrint(("[DEBUG] Found OSForensics dump file - handle 0x%x\n", FileHandle));
					bRet = TRUE;
				}
				else
				{
					// return true anyway -- there is high probability this is OSForensics file
					KdPrint(("[DEBUG] High probability of OSForensics dump file - handle 0x%x\n", FileHandle));
					bRet = TRUE;
				}
			}
		}
	}

	// dereference the FILE_OBJECT
	if(pFileObject)
	{
		ObDereferenceObject(pFileObject);
	}
	return bRet;
}

inline BOOLEAN FddpIsKernelMode(const IN ULONG_PTR uAddress)
{
	return (uAddress > (ULONG_PTR) MmHighestUserAddress);
}

BOOLEAN FddpTestFileFlags(const IN ULONG uFlags)
{
	// all tested memory acquisition tools use 0x400XX flags, where the last nibble is always 2
	// for example - 0x40042, 0x40062, 0x4000a
	// 0x400XX means FO_HANDLE_CREATED, while the nibble 2 means FO_SYNCHRONOUS_IO
	return (uFlags & 0x40002);
}

BOOLEAN FddpIsProcessNameEqual(const IN PCHAR pProcessName)
{
	PEPROCESS pEPROCESS = PsGetCurrentProcess();
	PCHAR pImageName = (PCHAR) SYMW_MEMBER_FROM_OFFSET(pEPROCESS, uEPROCImageFileNameOffset, 0);;

	return FddpAreStringsEqual(pProcessName, pImageName);
}

BOOLEAN FddpAreStringsEqual(PCHAR pString1, PCHAR pString2)
{
	while(*pString1 == *pString2)
	{
		if (*pString1 == '\0' || *pString2 == '\0' )
		{
			break;
		}

		pString1++;
		pString2++;
	}

	if(*pString1 == '\0' && *pString2 == '\0')
	{
		return TRUE;
	}

	return FALSE;
}

BOOLEAN FddpAreFlagsEqual(const IN PFILE_OBJECT pFileObject, const IN UCHAR LockOperation, const IN UCHAR DeletePending, const IN UCHAR ReadAccess, const IN UCHAR WriteAccess, 
						  const IN UCHAR DeleteAccess, const IN UCHAR SharedRead, const IN UCHAR SharedWrite, const IN UCHAR SharedDelete)
{
	PCHAR pLockOperation = (PCHAR) SYMW_MEMBER_FROM_OFFSET(pFileObject, uFILEOBJLockOperationOffset, 0);
	// all other members are one byte from each other
	PCHAR pDeletePending = pLockOperation + 1;
	PCHAR pReadAccess = pDeletePending + 1;
	PCHAR pWriteAccess = pReadAccess + 1;
	PCHAR pDeleteAccess = pWriteAccess + 1;
	PCHAR pSharedRead = pDeleteAccess + 1;
	PCHAR pSharedWrite = pSharedRead + 1;
	PCHAR pSharedDelete = pSharedWrite + 1;

	return (*pLockOperation == LockOperation &&
			*pDeletePending == DeletePending &&
			*pReadAccess == ReadAccess &&
			*pWriteAccess == WriteAccess &&
			*pDeleteAccess == DeleteAccess &&
			*pSharedRead == SharedRead &&
			*pSharedWrite == SharedWrite &&
			*pSharedDelete == SharedDelete);
}

BOOLEAN FddpIsFileExtensionEqual(const IN PFILE_OBJECT pFileObject, const IN PCHAR pFileExtension)
{
	// allocate a buffer for file name
	PUNICODE_STRING fileNameInfo = (PUNICODE_STRING) ExAllocatePoolWithTag(PagedPool, FILE_OBJECT_NAME_INFO_SIZE, TAG_FILE_OBJECT_NAME_INFO);

	if (fileNameInfo == NULL)
	{
		KdPrint(("[DEBUG] WARNING - ObQueryNameString name allocation failed -- cannot compare extensions\n"));
		return FALSE;
	}

	// query file name
	ULONG uReturnLength = 0;
	if(!NT_SUCCESS(pObQueryNameString(pFileObject, (POBJECT_NAME_INFORMATION) fileNameInfo, FILE_OBJECT_NAME_INFO_SIZE, &uReturnLength)))
	{
		KdPrint(("[DEBUG] WARNING - ObQueryNameString name query failed -- cannot compare extensions\n"));
		ExFreePoolWithTag(fileNameInfo, TAG_FILE_OBJECT_NAME_INFO);
		return FALSE;
	}
	
	// transform UNICODE_STRING to ANSI
	ANSI_STRING szFileName;
	memset(&szFileName, 0, sizeof(ANSI_STRING));
	if(!NT_SUCCESS(RtlUnicodeStringToAnsiString(&szFileName, fileNameInfo, TRUE)))
	{
		// if initialization failed, bail out
		KdPrint(("[DEBUG] WARNING - UNI2ANSI transformation failed -- cannot compare extensions\n"));
		ExFreePoolWithTag(fileNameInfo, TAG_FILE_OBJECT_NAME_INFO);
		return FALSE;
	}

	// all (fixed) extensions used in memory acquisition tools have only 3 characters
	PCHAR pExt = szFileName.Buffer + (szFileName.Length - 3);
	BOOLEAN bRet = FddpAreStringsEqual(pExt, pFileExtension);

	RtlFreeAnsiString(&szFileName);
	ExFreePoolWithTag(fileNameInfo, TAG_FILE_OBJECT_NAME_INFO);
	return bRet;
}

BOOLEAN FddpIsMemoryze(const IN PFILE_OBJECT pFileObject)
{
	// if process name equals to Memoryze immediately return TRUE -- it would be very awkward for someone to use this name
	// for any other process
	if(FddpIsProcessNameEqual("Memoryze.exe"))
	{
		KdPrint(("[DEBUG] Handle belongs to a Memoryze process\n"));
		return TRUE;
	}

	// only check additional attributes if file object was successfully referenced
	if(pFileObject != NULL)
	{
		// file extension in Memoryze cannot be easily changed -- return true if equal
		// also, if flags (Write, SharedRead and SharedWrite) are equal, also return true (this could lead to false positives however)
		if(	FddpIsFileExtensionEqual(pFileObject, "img") ||
			FddpAreFlagsEqual(pFileObject, 0, 0, 0, 1, 0, 1, 1, 0))
		{
			KdPrint(("[DEBUG] File extension is \"img\" or file flags indicate W|SR|SW -> this is probably a Memoryze dump file\n"));
			return TRUE;
		}
	}

	// all checks failed
	// driver search could be initiated (mktools.sys), but this is a bit of overhead, since we'll return true anyway in the parent function:)
	return FALSE;
}

BOOLEAN FddpIsMDD(const IN PFILE_OBJECT pFileObject)
{
	// only check additional attributes if file object was successfully referenced
	if(pFileObject != NULL)
	{
		// file extension cannot be checked, since any extension can be used!
		// if Write flag is set, return true -- rather high probability of false positives?
		if(FddpAreFlagsEqual(pFileObject, 0, 0, 0, 1, 0, 0, 0, 0))
		{
			KdPrint(("[DEBUG] File flags indicate Write access -> this is probably a MDD dump file\n"));
			return TRUE;
		}
	}

	// all checks failed
	return FALSE;
}

BOOLEAN FddpIsWinpmem(const IN PFILE_OBJECT pFileObject)
{
	// Winpmem has no "traditional" or static process name - it can be anything
	// only check additional attributes if file object was successfully referenced
	if(pFileObject != NULL)
	{
		// file extension cannot be checked, since any file extension can be used
		// if Write AND SharedRead flags are set, return true -- this combination is unique for Winpmem
		if(FddpAreFlagsEqual(pFileObject, 0, 0, 0, 1, 0, 1, 0, 0))
		{
			KdPrint(("[DEBUG] File flags indicate W|SR access -> this is probably a Winpmem dump file\n"));
			return TRUE;
		}
	}

	// all checks failed
	return FALSE;
}

BOOLEAN FddpIsFTK(const IN PFILE_OBJECT pFileObject)
{
	// if process name equals to FTK Imager immediately return TRUE
	if(FddpIsProcessNameEqual("FTK Imager.exe"))
	{
		KdPrint(("[DEBUG] Handle belongs to a FTK Imager process\n"));
		return TRUE;
	}

	// only check additional attributes if file object was successfully referenced
	if(pFileObject != NULL)
	{
		// file extension in FTK Imager can be easily changed by user, so combine this condition with the flags
		// if flags (Write, SharedRead and SharedWrite) are equal, return true
		if(	FddpIsFileExtensionEqual(pFileObject, "mem") ||
			FddpAreFlagsEqual(pFileObject, 0, 0, 0, 1, 0, 1, 1, 0))
		{
			KdPrint(("[DEBUG] File extension is \"mem\" or file flags indicate W|SR|SW -> this is probably a FTK Imager dump file\n"));
			return TRUE;
		}
	}

	// all checks failed
	// won't search for driver
	return FALSE;
}

BOOLEAN FddpIsWinen(const IN PFILE_OBJECT pFileObject)
{
	// if process name equals to winen immediately return TRUE
	if(FddpIsProcessNameEqual("winen.exe"))
	{
		KdPrint(("[DEBUG] Handle belongs to a Winen (Encase) process\n"));
		return TRUE;
	}

	// only check additional attributes if file object was successfully referenced
	if(pFileObject != NULL)
	{
		// file extension in Winen (E01) cannot be easily changed!
		// if flags (Read, Write, SharedRead and SharedWrite) are equal, return true
		if(	FddpIsFileExtensionEqual(pFileObject, "E01") ||
			FddpAreFlagsEqual(pFileObject, 0, 0, 1, 1, 0, 1, 1, 0))
		{
			KdPrint(("[DEBUG] File extension is \"E01\" or file flags indicate R|W|SR|SW -> this is probably a Winen dump file\n"));
			return TRUE;
		}
	}

	// all checks failed
	// won't search for driver
	return FALSE;
}

BOOLEAN FddpIsWin32DD(const IN PFILE_OBJECT pFileObject)
{
	// if process name equals to win32dd immediately return TRUE
	if(FddpIsProcessNameEqual("win32dd.exe"))
	{
		KdPrint(("[DEBUG] Handle belongs to a Moonsols Win32dd process\n"));
		return TRUE;
	}

	// only check additional attributes if file object was successfully referenced
	if(pFileObject != NULL)
	{
		// file extension in Win32DD can be arbitrarily set by the user, not checked
		// if flags (Read, Write, SharedRead and SharedWrite) are equal, return true
		if(FddpAreFlagsEqual(pFileObject, 0, 0, 1, 1, 0, 1, 1, 0))
		{
			KdPrint(("[DEBUG] File flags indicate R|W|SR|SW -> this is probably a Win32dd dump file\n"));
			return TRUE;
		}
	}

	// all checks failed
	// won't search for driver
	return FALSE;
}

BOOLEAN FddpIsOSForensics(const IN PFILE_OBJECT pFileObject)
{
	// if process name equals to osf32 immediately return TRUE
	if(FddpIsProcessNameEqual("osf32.exe"))
	{
		KdPrint(("[DEBUG] Handle belongs to a OSForensics process\n"));
		return TRUE;
	}

	// only check additional attributes if file object was successfully referenced
	if(pFileObject != NULL)
	{
		// file extension in OSForensics is bin
		// if Write flag is set, return true
		if(	FddpIsFileExtensionEqual(pFileObject, "bin") ||
			FddpAreFlagsEqual(pFileObject, 0, 0, 0, 1, 0, 0, 0, 0))
		{
			KdPrint(("[DEBUG] File extension is \"bin\" or file flags indicate Write access -> this is probably a OSForensics dump file\n"));
			return TRUE;
		}
	}

	// all checks failed
	// won't search for driver
	return FALSE;
}