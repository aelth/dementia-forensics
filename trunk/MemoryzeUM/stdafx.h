// stdafx.h : include file for standard system include files,
// or project specific include files that are used frequently, but
// are changed infrequently
//

#pragma once

#include "targetver.h"
//#define WIN32_LEAN_AND_MEAN             // Exclude rarely-used stuff from Windows headers
// Windows Header Files:
#include <windows.h>
#include <string>
#include <iostream>
#include <stdlib.h>
#include <stdio.h>
#include <tchar.h>
#include <vector>
#include <map>
#include <TlHelp32.h>
#include <Psapi.h>

#ifdef _UNICODE
#define tcout std::wcout
#define tcin std::wcin
typedef std::wstring TCharString;
typedef std::wstringstream TCharStringStream;
#else
#define tcout std::cout
#define tcin std::cin
typedef std::string TCharString;
typedef std::istringstream TCharStringStream;
#endif // _UNICODE

// boost smart pointers
#include <boost/scoped_ptr.hpp>
#include <boost/scoped_array.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/shared_array.hpp>
#include <boost/weak_ptr.hpp>
#include <boost/intrusive_ptr.hpp>

// boost filesystem
#include <boost/filesystem.hpp>

// formatting
#include <boost/format.hpp>
#include <boost/lexical_cast.hpp>

// banned functions
#include "../Common/banned.h"
