/* This is a generated file, do not edit! */

#ifndef INC_asynAPI_H
#define INC_asynAPI_H

#if defined(_WIN32) || defined(__CYGWIN__)

#  if !defined(epicsStdCall)
#    define epicsStdCall __stdcall
#  endif

#  if defined(BUILDING_asyn_API) && defined(EPICS_BUILD_DLL)
/* Building library as dll */
#    define ASYN_API __declspec(dllexport)
#  elif !defined(BUILDING_asyn_API) && defined(EPICS_CALL_DLL)
/* Calling library in dll form */
#    define ASYN_API __declspec(dllimport)
#  endif

#elif __GNUC__ >= 4
#  define ASYN_API __attribute__ ((visibility("default")))
#endif

#if !defined(ASYN_API)
#  define ASYN_API
#endif

#if !defined(epicsStdCall)
#  define epicsStdCall
#endif

#endif /* INC_asynAPI_H */

