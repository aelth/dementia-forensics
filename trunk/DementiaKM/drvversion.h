///////////////////////////////////////////////////////////////////////////////
///
/// Copyright (c) 2012 - <company name here>
///
/// Defines for the version information in the resource file
///
/// (File was in the PUBLIC DOMAIN  - Author: ddkwizard.assarbad.net)
///////////////////////////////////////////////////////////////////////////////

// $Id$

#ifndef __DRVVERSION_H_VERSION__
#define __DRVVERSION_H_VERSION__ 100

#if defined(_MSC_VER) && (_MSC_VER >= 1020)
#pragma once
#endif

#include "buildnumber.h"

// ---------------------------------------------------------------------------
// Several defines have to be given before including this file. These are:
// ---------------------------------------------------------------------------
#define TEXT_AUTHOR             // author (optional value)
#define PRD_MAJVER             1 // major product version
#define PRD_MINVER             0 // minor product version
#define PRD_BUILD              0 // build number for product
#define FILE_MAJVER            1 // major file version
#define FILE_MINVER            0 // minor file version
#define FILE_BUILD             _FILE_VERSION_BUILD // file build number
#define DRV_YEAR               2012 // current year or timespan (e.g. 2003-2007)
#define TEXT_WEBSITE           http:/##/www.infigo.hr // website
#define TEXT_PRODUCTNAME       Dementia kernel-mode driver // product's name
#define TEXT_FILEDESC          Dementia kernel-mode driver // component description
#define TEXT_COMPANY           Infigo IS d.o.o. // company
#define TEXT_MODULE            DementiaKM // module name
#define TEXT_COPYRIGHT         Copyright \xA9 DRV_YEAR TEXT_COMPANY // copyright information
// #define TEXT_SPECIALBUILD      // optional comment for special builds
#define TEXT_INTERNALNAME      DementiaKM.sys // copyright information
// #define TEXT_COMMENTS          // optional comments
// ---------------------------------------------------------------------------
// ... well, that's it. Pretty self-explanatory ;)
// ---------------------------------------------------------------------------

#endif // __DRVVERSION_H_VERSION__
