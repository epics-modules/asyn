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

#include <shareLib.h>
#ifdef __cplusplus
extern "C" {
#endif /*__cplusplus*/

epicsShareFunc int epicsShareAPI
 asynSetOption(const char *portName, int addr, const char *key, const char *val);
epicsShareFunc int epicsShareAPI
 asynShowOption(const char *portName, int addr,const char *key);
epicsShareFunc int epicsShareAPI
 asynReport(int level, const char *portName);
epicsShareFunc int epicsShareAPI
 asynSetTraceMask(const char *portName,int addr,int mask);
epicsShareFunc int epicsShareAPI
 asynSetTraceIOMask(const char *portName,int addr,int mask);
epicsShareFunc int epicsShareAPI
 asynSetTraceFile(const char *portName,int addr,const char *filename);
epicsShareFunc int epicsShareAPI
 asynSetTraceIOTruncateSize(const char *portName,int addr,int size);
epicsShareFunc int epicsShareAPI
 asynAutoConnect(const char *portName,int addr,int yesNo);
epicsShareFunc int epicsShareAPI
 asynEnable(const char *portName,int addr,int yesNo);

epicsShareFunc int epicsShareAPI
 asynOctetConnect(const char *entry, const char *port, int addr,
                  int timeout, int buffer_len,const char *drvInfo);
epicsShareFunc int epicsShareAPI
 asynOctetRead(const char *entry, int nread);
epicsShareFunc int epicsShareAPI
 asynOctetWrite(const char *entry, const char *output);
epicsShareFunc int epicsShareAPI
 asynOctetWriteRead(const char *entry, const char *output, int nread);
epicsShareFunc int epicsShareAPI
 asynOctetFlush(const char *entry);
epicsShareFunc int epicsShareAPI
 asynOctetSetInputEos(const char *port, int addr,const char *eos,const char *drvInfo);
epicsShareFunc int epicsShareAPI
 asynOctetGetInputEos(const char *port, int addr,const char *drvInfo);
epicsShareFunc int epicsShareAPI
 asynOctetSetOutputEos(const char *port, int addr,const char *eos,const char *drvInfo);
epicsShareFunc int epicsShareAPI
 asynOctetGetOutputEos(const char *port, int addr,const char *drvInfo);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /*INCasynShellCommandsh*/
