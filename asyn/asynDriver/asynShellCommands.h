/* asynShellCommands.h */
/***********************************************************************
* Copyright (c) 2002 The University of Chicago, as Operator of Argonne
* National Laboratory, and the Regents of the University of
* California, as Operator of Los Alamos National Laboratory, and
* Berliner Elektronenspeicherring-Gesellschaft m.b.H. (BESSY).
* asynDriver is distributed subject to a Software License Agreement
* found in file LICENSE that is included with this distribution.
***********************************************************************/
/* Author: Marty Kraimer */

#ifndef INCasynShellCommandsh
#define INCasynShellCommandsh 1

#include "shareLib.h"
#ifdef __cplusplus
extern "C" {
#endif

epicsShareFunc int epicsShareAPI
 asynSetOption(const char *portName, int addr, const char *key, const char *val);
epicsShareFunc int epicsShareAPI
 asynShowOption(const char *portName, int addr,const char *key);
epicsShareFunc int epicsShareAPI
 asynReport(const char *filename, int level);
epicsShareFunc int epicsShareAPI
 asynSetTraceMask(const char *portName,int addr,int mask);
epicsShareFunc int epicsShareAPI
 asynSetTraceIOMask(const char *portName,int addr,int mask);
epicsShareFunc int epicsShareAPI
 asynAutoConnect(const char *portName,int addr,int yesNo);
epicsShareFunc int epicsShareAPI
 asynEnable(const char *portName,int addr,int yesNo);

#ifdef __cplusplus
}
#endif

#endif /*INCasynShellCommandsh*/
