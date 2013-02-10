#include "ForensicAppDumpDetectorFS.h"
#include "../DementiaKM/SymbolWrapper.h"

// boolean representing status of ForensicAppDumpDetector
static BOOLEAN bIsForensicAppDumpDetectorInitialized = FALSE;

// flag which determines the state of symbols used in ForensicAppDumpDetector
static BOOLEAN bSymbolsInitialized = FALSE;

// offset of process name inside EPROCESS block
static ULONG uEPROCImageFileNameOffset = -1;

// offset of LockOperation member inside FILE_OBJECT block
static ULONG uFILEOBJLockOperationOffset = -1;

BOOLEAN FddpIsNTFSSpecialFile(const IN PANSI_STRING);
BOOLEAN FddpTestFileFlags(const IN ULONG);
BOOLEAN FddpFilenameUnicode2Ansi(const IN PUNICODE_STRING, OUT PANSI_STRING);
BOOLEAN FddpIsProcessNameEqual(PCHAR);
BOOLEAN FddpAreStringsEqual(PCHAR, PCHAR);
BOOLEAN FddpAreFlagsEqual(const IN PFILE_OBJECT, const IN UCHAR, const IN UCHAR, const IN UCHAR, const IN UCHAR, const IN UCHAR, const IN UCHAR, const IN UCHAR, const IN UCHAR);
BOOLEAN FddpIsFileExtensionEqual(const IN PANSI_STRING, const IN PCHAR);
BOOLEAN FddpIsMemoryze(const IN PFILE_OBJECT, const IN PANSI_STRING);
BOOLEAN FddpIsMDD(const IN PFILE_OBJECT, const IN PANSI_STRING);
BOOLEAN FddpIsWinpmem(const IN PFILE_OBJECT, const IN PANSI_STRING);
BOOLEAN FddpIsFTK(const IN PFILE_OBJECT, const IN PANSI_STRING);
BOOLEAN FddpIsWinen(const IN PFILE_OBJECT, const IN PANSI_STRING);
BOOLEAN FddpIsWin32DD(const IN PFILE_OBJECT, const IN PANSI_STRING);
BOOLEAN FddpIsOSForensics(const IN PFILE_OBJECT, const IN PANSI_STRING);

BOOLEAN FddInit(VOID)
{
	KeEnterCriticalRegion();
	if(bIsForensicAppDumpDetectorInitialized == TRUE)
	{
		KeLeaveCriticalRegion();
		return TRUE;
	}

	// add wanted symbols
	if(!SymWAddSymbol("_EPROCESS.ImageFileName", -1, -1, -1, -1) || !SymWAddSymbol("_FILE_OBJECT.LockOperation", -1, -1, -1, -1))
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
	bRet = SymWInitializeOffset(&uEPROCImageFileNameOffset, "_EPROCESS.ImageFileName") &&
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

BOOLEAN FddIsForensicAppDumpFile(const IN PFILE_OBJECT pFileObject, const IN PUNICODE_STRING pusFilePath, const ULONG uLength, const IN KPROCESSOR_MODE mode)
{
	ASSERTMSG("Cannot detect forensic applications because ForensicAppDumpDetector is not yet initialized", bIsForensicAppDumpDetectorInitialized == TRUE);
	ASSERTMSG("Internal symbols are not initialized!", bSymbolsInitialized == TRUE);

	if(pFileObject == NULL)
	{
		KdPrint(("[DEBUG] WARNING - Received NULL pointer to file object, cannot determine whether the file is forensic application dump...\n"));
		return FALSE;
	}

	if(pusFilePath == NULL || pusFilePath->Buffer == NULL)
	{
		KdPrint(("[DEBUG] WARNING - Received NULL pointer to file path, cannot determine whether the file is forensic application dump...\n"));
		return FALSE;
	}

	// first check whether the file has a special NTFS prefix - if yes, it's not a forensic application dump
	/*if(FddpIsNTFSSpecialFile(pFileObject))
	{
		return FALSE;
	}*/

	// test the file object flags
	if(!FddpTestFileFlags(pFileObject->Flags))
	{
		return FALSE;
	}

	// all tested memory acquisition tools have PrivateCacheMap and CompletionContext fields equal to NULL
	if(pFileObject->PrivateCacheMap != NULL || pFileObject->CompletionContext != NULL)
	{
		return FALSE;
	}

	// convert the name to ANSI string
	ANSI_STRING szFileName;
	if(!FddpFilenameUnicode2Ansi(pusFilePath, &szFileName))
	{
		KdPrint(("[DEBUG] WARNING - Could not convert unicode file path to ansi, cannot determine whether the file is forensic application dump...\n"));
		return FALSE;
	}

	BOOLEAN bRet = FALSE;

	if(mode == UserMode)
	{
		// MDD, Memoryze, FTK Imager and Winen (Encase) all have FileHandle, IoStatusBlock and Buffer in user mode!
		// this practically means that they are all vulnerable to user-mode attacks:)
		if(uLength == 0x1000)
		{
			// buffer length is 0x1000 for Memoryze and MDD
			if(FddpIsMemoryze(pFileObject, &szFileName))
			{
				KdPrint(("[DEBUG] Found Memoryze dump file - file object pointer 0x%p\n", pFileObject));
				bRet = TRUE;
			}
			else if(FddpIsMDD(pFileObject, &szFileName))
			{
				KdPrint(("[DEBUG] Found MDD dump file - file object pointer 0x%p\n", pFileObject));
				bRet = TRUE;
			}
			else if(FddpIsWinpmem(pFileObject, &szFileName))
			{
				KdPrint(("[DEBUG] Found Winpmem dump file - file object pointer 0x%p\n", pFileObject));
				bRet = TRUE;
			}
			else
			{
				// return true anyway -- there is high probability this is MDD or Memoryze file
				KdPrint(("[DEBUG] High probability of Memoryze, MDD or some other memory acquisition application dump file - file object pointer 0x%p\n", pFileObject));
				bRet = TRUE;
			}
		}
		// FTK Imager uses fixed length of 0x8000
		else if(uLength == 0x8000)
		{
			if(FddpIsFTK(pFileObject, &szFileName))
			{
				KdPrint(("[DEBUG] Found FTK Imager dump file - file object pointer 0x%p\n", pFileObject));
				bRet = TRUE;
			}
			else
			{
				// return true anyway -- there is high probability this is FTK Imager file
				KdPrint(("[DEBUG] High probability of FTK Imager dump file - file object pointer 0x%p\n", pFileObject));
				bRet = TRUE;
			}
		}
		// Winen uses variable buffer length (but bigger than 0x1000) - this condition also catches all other lengths (and thus a high probability of false positives)
		else if(uLength > 0x1000)
		{
			if(FddpIsWinen(pFileObject, &szFileName))
			{
				KdPrint(("[DEBUG] Found Winen dump file - file object pointer 0x%p\n", pFileObject));
				bRet = TRUE;
			}

			// else assume it is not a memory acquisition application dump file!
		}
	}
	else if(mode == KernelMode)
	{
		// OSForensics (interestingly) has buffer in user mode!
		// length is 0x1000
		if(uLength == 0x1000)
		{
			if(FddpIsOSForensics(pFileObject, &szFileName))
			{
				KdPrint(("[DEBUG] Found OSForensics dump file - file object pointer 0x%p\n", pFileObject));
				bRet = TRUE;
			}
			// win32dd can also have length of 0x1000!
			else if(FddpIsWin32DD(pFileObject, &szFileName))
			{
				KdPrint(("[DEBUG] Found Win32dd dump file - file object pointer 0x%p\n", pFileObject));
				bRet = TRUE;
			}
		}
		// length is variable, but always between 0x1000 & 0x100000
		else
		{
			if(FddpIsWin32DD(pFileObject, &szFileName))
			{
				KdPrint(("[DEBUG] Found Win32dd dump file - file object pointer 0x%p\n", pFileObject));
				bRet = TRUE;
			}
		}
	}

	RtlFreeAnsiString(&szFileName);
	return bRet;
}

BOOLEAN FddpTestFileFlags(const IN ULONG uFlags)
{
	// all tested memory acquisition tools use 0x400XX flags, where the last nibble is always 2
	// for example - 0x40042, 0x40062, 0x4000a
	// 0x400XX means FO_HANDLE_CREATED, while the nibble 2 means FO_SYNCHRONOUS_IO
	return (uFlags & 0x40002);
}


BOOLEAN FddpIsNTFSSpecialFile(const IN PANSI_STRING pszFileName)
{
	ASSERTMSG("File object pointer must not be NULL", pszFileName != NULL);

	// all special NTFS files begin with $ (for example: $Logfile, $Boot, $Quota...)
	PCHAR pSpecialPrefix = pszFileName->Buffer;
	BOOLEAN bRet = FddpAreStringsEqual(pSpecialPrefix, "\\$");

	return bRet;
}

BOOLEAN FddpFilenameUnicode2Ansi(const IN PUNICODE_STRING pusFilePath, OUT PANSI_STRING pszString)
{
	ASSERTMSG("File name pointer must not be NULL", pusFilePath != NULL);
	ASSERTMSG("Pointer to return ANSI_STRING must not be NULL", pszString != NULL);

	// transform UNICODE_STRING to ANSI
	memset(pszString, 0, sizeof(ANSI_STRING));
	if(!NT_SUCCESS(RtlUnicodeStringToAnsiString(pszString, pusFilePath, TRUE)))
	{
		// if initialization failed, bail out
		KdPrint(("[DEBUG] WARNING - UNI2ANSI transformation failed!\n"));
		return FALSE;
	}

	return TRUE;
}

BOOLEAN FddpIsProcessNameEqual(PCHAR pProcessName)
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

BOOLEAN FddpIsFileExtensionEqual(const IN PANSI_STRING pszFileName, const IN PCHAR pFileExtension)
{
	ASSERTMSG("File name pointer must not be NULL", pszFileName != NULL);
	ASSERTMSG("Pointer to file extension must not be NULL", pFileExtension != NULL);

	// all (fixed) extensions used in memory acquisition tools have only 3 characters
	PCHAR pExt = pszFileName->Buffer + (pszFileName->Length - 3);
	BOOLEAN bRet = FddpAreStringsEqual(pExt, pFileExtension);

	return bRet;
}

BOOLEAN FddpIsMemoryze(const IN PFILE_OBJECT pFileObject, const IN PANSI_STRING pszFileName)
{
	ASSERTMSG("File name pointer must not be NULL", pszFileName != NULL);

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
		if(	FddpIsFileExtensionEqual(pszFileName, "img") ||
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

BOOLEAN FddpIsMDD(const IN PFILE_OBJECT pFileObject, const IN PANSI_STRING pszFileName)
{
	ASSERTMSG("File name pointer must not be NULL", pszFileName != NULL);

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

BOOLEAN FddpIsWinpmem(const IN PFILE_OBJECT pFileObject, const IN PANSI_STRING pszFileName)
{
	ASSERTMSG("File name pointer must not be NULL", pszFileName != NULL);

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

BOOLEAN FddpIsFTK(const IN PFILE_OBJECT pFileObject, const IN PANSI_STRING pszFileName)
{
	ASSERTMSG("File name pointer must not be NULL", pszFileName != NULL);

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
		if(	FddpIsFileExtensionEqual(pszFileName, "mem") ||
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

BOOLEAN FddpIsWinen(const IN PFILE_OBJECT pFileObject, const IN PANSI_STRING pszFileName)
{
	ASSERTMSG("File name pointer must not be NULL", pszFileName != NULL);

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
		if(	FddpIsFileExtensionEqual(pszFileName, "E01") ||
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

BOOLEAN FddpIsWin32DD(const IN PFILE_OBJECT pFileObject, const IN PANSI_STRING pszFileName)
{
	ASSERTMSG("File name pointer must not be NULL", pszFileName != NULL);

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

BOOLEAN FddpIsOSForensics(const IN PFILE_OBJECT pFileObject, const IN PANSI_STRING pszFileName)
{
	ASSERTMSG("File name pointer must not be NULL", pszFileName != NULL);

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
		if(	FddpIsFileExtensionEqual(pszFileName, "bin") ||
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