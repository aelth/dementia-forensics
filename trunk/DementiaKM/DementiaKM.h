///////////////////////////////////////////////////////////////////////////////
///
/// Copyright (c) 2012 - <company name here>
///
/// Original filename: DementiaKM.h
/// Project          : DementiaKM
/// Date of creation : <see DementiaKM.cpp>
/// Author(s)        : <see DementiaKM.cpp>
///
/// Purpose          : <see DementiaKM.cpp>
///
/// Revisions:         <see DementiaKM.cpp>
///
///////////////////////////////////////////////////////////////////////////////

// $Id$

#ifndef __DEMENTIAKM_H_VERSION__
#define __DEMENTIAKM_H_VERSION__ 100

#if defined(_MSC_VER) && (_MSC_VER >= 1020)
#pragma once
#endif


#include "drvcommon.h"
#include "drvversion.h"
#include "../Common/IOCTLCodes.h"

#define DEVICE_NAME			"\\Device\\DementiaKM"
#define SYMLINK_NAME		"\\DosDevices\\DementiaKM"
PRESET_UNICODE_STRING(usDeviceName, DEVICE_NAME);
PRESET_UNICODE_STRING(usSymlinkName, SYMLINK_NAME);

#endif // __DEMENTIAKM_H_VERSION__
