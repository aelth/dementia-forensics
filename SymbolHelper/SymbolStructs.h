#pragma once

/** Maximum length of the symbol.

In DbgHelp.h this constant equals 2000. However, most of the symbols are well beneath the 64 - character limit.\n
For example, on most kernel images the longest symbol is 84 bytes long:
CmpRegistryMachineSystemCurrentControlSetControlSessionManagerMemoryManagementString:)
*/
#define MAX_SYM_NAME	512

/**	@name DbgHelp important structures and defines (defined in MSDN)

Structures and defines important for DbgHelp functions
*/

//@{
#define SYMOPT_CASE_INSENSITIVE         0x00000001
#define SYMOPT_UNDNAME                  0x00000002
#define SYMOPT_DEFERRED_LOADS           0x00000004
#define SYMOPT_AUTO_PUBLICS             0x00010000
#define SYMOPT_DEBUG                    0x80000000

/**	@enum SYM_TYPE
@brief Enum describing the type of the symbol

Type of the symbol (PDB, COFF, ...) is specified with the values within this enumeration.
*/
typedef enum {
	SymNone = 0,
	SymCoff,
	SymCv,
	SymPdb,
	SymExport,
	SymDeferred,
	SymSym,       // .sym file
	SymDia,
	SymVirtual,
	NumSymTypes
} SYM_TYPE;

typedef enum _IMAGEHLP_SYMBOL_TYPE_INFO {
	TI_GET_SYMTAG,
	TI_GET_SYMNAME,
	TI_GET_LENGTH,
	TI_GET_TYPE,
	TI_GET_TYPEID,
	TI_GET_BASETYPE,
	TI_GET_ARRAYINDEXTYPEID,
	TI_FINDCHILDREN,
	TI_GET_DATAKIND,
	TI_GET_ADDRESSOFFSET,
	TI_GET_OFFSET,
	TI_GET_VALUE,
	TI_GET_COUNT,
	TI_GET_CHILDRENCOUNT,
	TI_GET_BITPOSITION,
	TI_GET_VIRTUALBASECLASS,
	TI_GET_VIRTUALTABLESHAPEID,
	TI_GET_VIRTUALBASEPOINTEROFFSET,
	TI_GET_CLASSPARENTID,
	TI_GET_NESTED,
	TI_GET_SYMINDEX,
	TI_GET_LEXICALPARENT,
	TI_GET_ADDRESS,
	TI_GET_THISADJUST,
	TI_GET_UDTKIND,
	TI_IS_EQUIV_TO,
	TI_GET_CALLING_CONVENTION,
	TI_IS_CLOSE_EQUIV_TO,
	TI_GTIEX_REQS_VALID,
	TI_GET_VIRTUALBASEOFFSET,
	TI_GET_VIRTUALBASEDISPINDEX,
	TI_GET_IS_REFERENCE,
	IMAGEHLP_SYMBOL_TYPE_INFO_MAX,
} IMAGEHLP_SYMBOL_TYPE_INFO;

/**	@struct _IMAGEHLP_MODULE64
@brief Symbol information about the specified module

Structure contains all module information regarding symbols
*/

typedef struct _IMAGEHLP_MODULE64 
{  
	DWORD SizeOfStruct;								/**< Size of the structure in bytes (must be set before using the structure!)*/
	DWORD64 BaseOfImage;							/**< Base virtual address of the image*/
	DWORD ImageSize;								/**< Size of image in bytes*/
	DWORD TimeDateStamp;							/**< Date and timestamp value of image creation*/
	DWORD CheckSum;									/**< Image checksum, usually zero*/
	DWORD NumSyms;									/**< Number of the symbols inside the symbol table*/
	SYM_TYPE SymType;								/**< Type of the symbol - @ref SYM_TYPE*/
	TCHAR ModuleName[32];							/**< The name of the module*/
	TCHAR ImageName[256];							/**< The name of the image containing the module*/							
	TCHAR LoadedImageName[256];						/**< File from which the symbols were loaded*/
	TCHAR LoadedPdbName[256];						/**< Pathname of the .PDB file*/
	DWORD CVSig;									/**< Signature of the CV record*/
	TCHAR CVData[MAX_PATH*3];						/**< Contents of the CV record*/
	DWORD PdbSig;									/**< PDB signature*/
	GUID PdbSig70;									/**< Visual C/C++ >7.0 PDB signature*/
	DWORD PdbAge;									/**< DBI age of PDB symbols*/
	BOOL PdbUnmatched;								/**< Indication whether the loaded PDB is unmatched*/
	BOOL DbgUnmatched;								/**< Indication whether the loaded DBG is unmatched*/
	BOOL LineNumbers;								/**< Indication of the available line numbers*/
	BOOL GlobalSymbols;								/**< Indication of symbol information availability*/
	BOOL TypeInfo;									/**< Indication of type information availability*/
	BOOL SourceIndexed;								/**< Indication whether the PDB supports the source server*/
	BOOL Publics;									/**< Indication of public symbol presence inside the module*/
} IMAGEHLP_MODULE64, *PIMAGEHLP_MODULE64;


/**	@struct _SYMBOL_INFO
@brief Information about particular symbol

Structure containing symbol information such as address, tag, etc.
*/
typedef struct _SYMBOL_INFO 
{
	ULONG SizeOfStruct;								/**< Size of the structure in bytes (must be set before using the structure!)*/
	ULONG TypeIndex;								/**< Type index of the symbol (see PDB documentation)*/
	ULONG64 Reserved[2];							/**< Reserved field*/
	ULONG Index;									/**< Unique value for the symbol*/
	ULONG Size;										/**< Symbol size in bytes, usually zero*/
	ULONG64 ModBase;								/**< Base virtual address of the module containing this symbol*/
	ULONG Flags;									/**< Flags describing the symbol in detail (see MSDN)*/
	ULONG64 Value;									/**< Value (if constant)*/
	ULONG64 Address;								/**< Virtual address of the start of the symbol - <b>useful field</b>*/
	ULONG Register;									/**< Register*/
	ULONG Scope;									/**< DIA scope (see Debug Interface Access SDK)*/
	ULONG Tag;										/**< PDB classification (see PDB documentation)*/
	ULONG NameLen;									/**< Length of the name (without the null character)*/
	ULONG MaxNameLen;								/**< Size of the @ref Name buffer, in characters*/
	TCHAR Name[1];									/**< Name of the symbol (null terminated string) - <b>useful field</b>*/
} SYMBOL_INFO, *PSYMBOL_INFO;

typedef struct _TI_FINDCHILDREN_PARAMS {
	ULONG Count;
	ULONG Start;
	ULONG ChildId[1];
} TI_FINDCHILDREN_PARAMS, *PTI_FINDCHILDREN_PARAMS;

/**	@struct _INTERNAL_SYMBOL
@brief Symbol entity

Structure which defines a symbol entity.\n
Array of these structures is filled with appropriate information

Currently not used!
*/
typedef struct _INTERNAL_SYMBOL
{
	char	name[MAX_SYM_NAME];		/**< Name of the symbol*/
	ULONG64 u64address;				/**< 64-bit virtual address of the symbol*/
	ULONG	uOffset;				/**< If symbol is not a function, this represents the offset inside the parent*/
	ULONG	uBitPosition;			/**< If symbol is a bit field, this member represents bit position inside the bit field*/
} INTERNAL_SYMBOL, *PINTERNAL_SYMBOL;
//@}

/**	@name DbgHelp exports' function pointers and pointer definitions

Basically, these are function pointers pointing to functions exported from @c DbgHelp.dll and used inside
this DLL.\n
Windows XP and Vista ship with an older version of @c DbgHelp.dll file which doesn't contain many useful and important
functions.\n
User must download <em>Debugging tools for Windows</em> and set the path to the new @c DbgHelp.dll file.\n These function pointers
will actually point to the exported functions from this <b>new</b> @c DbgHelp.dll file, so we can be sure these functions will
work and have all features which are important to us.
*/

//@{

/**	@fn SYMINIT
@brief Function pointer pointing to @c SymInitialize function

@param HANDLE Handle which identifies the caller. Actually, this can be any handle that we want, but often pseudohandle from @c GetCurrentProcess() is passed.
@param PCTSTR Path for symbol searching. If this parameter is @c NULL, then we search current directory first, and then use environment variables to find the symbols.
@param BOOL If this is @c TRUE, then all the symbols are automatically enumerated and symbols for them are loaded
@retval BOOL @c TRUE if ok, @c FALSE if error

@c SymInitialize initializes symbol handler for the process.\n
It's the first and necessary function when working with symbols.
*/
typedef BOOL (WINAPI *SYMINIT)(HANDLE, PCTSTR, BOOL);

/**	@fn SYMCLEAN
@brief Function pointer pointing to @c SymCleanup function

@param HANDLE Handle which was passed in @ref SYMINIT function
@retval BOOL @c TRUE if ok, @c FALSE if error

@c SymCleanup deallocates all resources taken when working with symbols.\n
*/
typedef BOOL (WINAPI *SYMCLEAN)(HANDLE);

/**	@fn SYMOPTIONS
@brief Function pointer pointing to @c SymSetOptions function

@param DWORD @c DWORD value which specifies the new option mask
@retval DWORD Current option mask

This function sets the symbol option mask. Mask specifies whether only public or private symbols should be searched,
if C++ names are used, etc.
*/
typedef DWORD (WINAPI *SYMOPTIONS)(DWORD);

/**	@fn FINDEXEIMAGE
@brief Function pointer pointing to @c FindExecutableImage function

@param PCSTR Pointer to the file name to locate
@param PCSTR Pointer to the symbol path (path where symbols are stored)
@param PCSTR Pointer to the buffer which receives the full path of the executable
@retval HANDLE Handle to the executable image, or @c FALSE if error

This function locates executable image and associated symbols.
*/
typedef HANDLE (WINAPI *FINDEXEIMAGE)(PCSTR, PCSTR, PSTR);

/** @fn SYMLOAD64
@brief Function pointer pointing to @c SymLoadModule64 function

@param HANDLE Handle passed to @ref SYMINIT function
@param HANDLE Handle to the executable image (usually @c HANDLE returned from @ref FINDEXEIMAGE function)
@param PCSTR Pointer to the name of the executable (path or without path)
@param PCSTR Pointer to the name of the module contained within specified image
@param DWORD64 Load address of the specified module
@param DWORD Size of the specified module in bytes
@retval DWORD64 Base address of the specified module, or 0 if error

Function locates and loads the symbol table for the specified module (if module is specified).
*/
typedef DWORD64 (WINAPI *SYMLOAD64)(HANDLE, HANDLE, PCSTR, PCSTR, DWORD64, DWORD);

/**	@fn SYMUNLOAD64
@brief Function pointer pointing to @c SymUnloadModule64 function

@param HANDLE Handle passed to @ref SYMINIT function
@param DWORD64 Base address of the module that is to be unloaded
@retval BOOL @c TRUE if ok, @c FALSE if error

After calling @ref SYMLOAD64 function and doing the necessary work, we need to unload the symbol table and make cleanup.
This function makes symbol table cleanup and "safe" unload.
*/
typedef BOOL (WINAPI *SYMUNLOAD64)(HANDLE, DWORD64);

/**	@fn PSYM_ENUMERATESYMBOLS_CALLBACK
@brief Function pointer pointing to callback function used in @ref SYMENUMSYMS function

@param PSYMBOL_INFO Pointer to the @ref _SYMBOL_INFO structure containing information about the symbol
@param ULONG Size of the symbol in bytes (can be zero, value is actually just an approximation)
@param PVOID Pointer to the user-defined buffer passed to this callback function from @ref SYMENUMSYMS function
@retval BOOL @c TRUE if ok (enumeration continues), @c FALSE if error (enumeration stops)

This is the callback function used within @ref SYMENUMSYMS function.
Within this function we find out the address and the name of the symbol, and save those values in our buffer.
*/
typedef BOOL (CALLBACK *PSYM_ENUMERATESYMBOLS_CALLBACK)(PSYMBOL_INFO, ULONG, PVOID);

/**	@fn SYMENUMSYMS
@brief Function pointer pointing to @c SymEnumTypes function

@param HANDLE Handle passed to @ref SYMINIT function
@param ULONG64 Base address of the module
@param PSYM_ENUMERATESYMBOLS_CALLBACK Pointer to the @ref PSYM_ENUMERATESYMBOLS_CALLBACK callback function
@param PVOID Pointer to the user-defined buffer passed to @ref PSYM_ENUMERATESYMBOLS_CALLBACK function
@retval BOOL @c TRUE if ok, @c FALSE if error

Function enumerates all user defined types within the specified module. For every UDT found, @ref PSYM_ENUMERATESYMBOLS_CALLBACK function is called.
*/
typedef BOOL (WINAPI *SYMENUMTYPES)(HANDLE, ULONG64, PSYM_ENUMERATESYMBOLS_CALLBACK, PVOID);


/**	@fn SYMENUMSYMS
@brief Function pointer pointing to @c SymEnumTypesByName function

@param HANDLE Handle passed to @ref SYMINIT function
@param ULONG64 Base address of the module
@param PCSTR Mask - not used or defined in documentation, but this is probably name filter
@param PSYM_ENUMERATESYMBOLS_CALLBACK Pointer to the @ref PSYM_ENUMERATESYMBOLS_CALLBACK callback function
@param PVOID Pointer to the user-defined buffer passed to @ref PSYM_ENUMERATESYMBOLS_CALLBACK function
@retval BOOL @c TRUE if ok, @c FALSE if error

Function enumerates all user defined types within the specified module. For every UDT found, @ref PSYM_ENUMERATESYMBOLS_CALLBACK function is called.
*/
typedef BOOL (WINAPI *SYMENUMTYPESBYNAME)(HANDLE, ULONG64, PCSTR, PSYM_ENUMERATESYMBOLS_CALLBACK, PVOID);

/**	@fn SYMENUMSYMS
@brief Function pointer pointing to @c SymGetTypeFromName function

@param HANDLE Handle passed to @ref SYMINIT function
@param ULONG64 Base address of the module
@param PCTSTR Name of the type
@param PSYMBOL_INFO pointer to @ref SYMBOL_INFO structure
@retval BOOL @c TRUE if ok, @c FALSE if error

Function retrieves information about type given the name of the type.
*/
typedef BOOL (WINAPI *SYMGETTYPEFROMNAME)(HANDLE, ULONG64, PCTSTR, PSYMBOL_INFO);

/**	@fn SYMGETINFO64
@brief Function pointer pointing to @c SymGetModuleInfo64 function

@param HANDLE Handle passed to @ref SYMINIT function
@param DWORD64 Address of the loaded module
@param PIMAGEHLP_MODULE64 Pointer to @ref _IMAGEHLP_MODULE64 structure containing information about module
@retval BOOL @c TRUE if ok, @c FALSE if error

Function which retrieves information about symbols inside the specified module
*/
typedef BOOL (WINAPI *SYMGETINFO64)(HANDLE, DWORD64, PIMAGEHLP_MODULE64);

/**	@fn SYMGETTYPEINFO
@brief Function pointer pointing to @c SymGetTypeInfo function

@param HANDLE Handle passed to @ref SYMINIT function
@param DWORD64 Address of the loaded module
@param ULONG Type Index (returned by the callback)
@param IMAGEHLP_SYMBOL_TYPE_INFO Information type (defined by the enum)
@param PVOID returned data (format specified by the GetType parameter)
@retval BOOL @c TRUE if ok, @c FALSE if error

Function which retrieves information about symbols inside the specified module
*/
typedef BOOL (WINAPI *SYMGETTYPEINFO)(HANDLE, DWORD64, ULONG, IMAGEHLP_SYMBOL_TYPE_INFO, PVOID);

/**	@fn SYMENUMSYMS
@brief Function pointer pointing to @c SymEnumSymbols function

@param HANDLE Handle passed to @ref SYMINIT function
@param ULONG64 Base address of the module
@param PCTSTR Regular expression which specifies <em>the mask</em> to be used when enumerating symbols (for example: "Ob*" enumerates all symbols starting with "Ob")
@param PSYM_ENUMERATESYMBOLS_CALLBACK Pointer to the @ref PSYM_ENUMERATESYMBOLS_CALLBACK callback function
@param PVOID Pointer to the user-defined buffer passed to @ref PSYM_ENUMERATESYMBOLS_CALLBACK function
@retval BOOL @c TRUE if ok, @c FALSE if error

Function enumerates all symbols within the specified module. For every symbol found, @ref PSYM_ENUMERATESYMBOLS_CALLBACK function is called.
*/
typedef BOOL (WINAPI *SYMENUMSYMS)(HANDLE, ULONG64, PCTSTR, PSYM_ENUMERATESYMBOLS_CALLBACK, PVOID);

/**	@fn SYMNAME
@brief Function pointer pointing to @c SymFromName function

@param HANDLE Handle passed to @ref SYMINIT function
@param PCTSTR Pointer to the name of the symbol to be located
@param PSYMBOL_INFO Pointer to the @ref _SYMBOL_INFO structure containing information about the symbol found
@retval BOOL @c TRUE if ok, @c FALSE if error

Function finds the symbol by name, and returns the information in @ref _SYMBOL_INFO structure.\n
@note This function could be a great time-saver for us, because we could search for symbols directly by their name.
But because we are trying to find symbols for the kernel which is not a standalone process, this method cannot be used here (at least according to available knowledge)
*/
typedef BOOL (WINAPI *SYMNAME)(HANDLE, PCTSTR, PSYMBOL_INFO);
//@}