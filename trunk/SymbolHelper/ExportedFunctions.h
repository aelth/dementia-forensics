#pragma once

#include "../Common/SymbolUDT.h"

// The following ifdef block is the standard way of creating macros which make exporting 
// from a DLL simpler. All files within this DLL are compiled with the SYMBOLHELPER_EXPORTS
// symbol defined on the command line. this symbol should not be defined on any project
// that uses this DLL. This way any other project whose source files include this file see 
// SYMBOLHELPER_API functions as being imported from a DLL, whereas this DLL sees symbols
// defined with this macro as being exported.
#ifdef __cplusplus 
extern "C" 
{ 
#endif // __cplusplus  
#ifdef SYMBOLHELPER_EXPORTS
#define SYMBOLHELPER_API __declspec(dllexport)
#else
#define SYMBOLHELPER_API __declspec(dllimport)
#endif

// functions exported from the DLL - wrappers around static class member functions
// (don't want c++ stuff in exported functions, it's cleaner this way)
SYMBOLHELPER_API bool InitializeDebugSymbols(void);
SYMBOLHELPER_API bool GetSymbolsInformation(SymbolList &symbolList);

#ifdef __cplusplus 
} 
#endif // __cplusplus 