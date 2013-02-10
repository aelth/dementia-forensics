// stdafx.h : include file for standard system include files,
// or project specific include files that are used frequently, but
// are changed infrequently
//

#pragma once

#include "targetver.h"
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <tchar.h>
#include <string>
#include <iostream>
#include <list>
#include <Psapi.h>
#include <fltUser.h>

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

// boost date/time functions
#include <boost/date_time.hpp>

// boost program option/argument parser
#include <boost/program_options.hpp>

// boost smart pointers
#include <boost/scoped_ptr.hpp>
#include <boost/scoped_array.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/shared_array.hpp>
#include <boost/weak_ptr.hpp>
#include <boost/intrusive_ptr.hpp>

// threading operations
#include <boost/thread.hpp>
#include <boost/thread/mutex.hpp>

// banned functions
#include "../Common/banned.h"
