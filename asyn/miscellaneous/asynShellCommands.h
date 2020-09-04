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

#include <asynAPI.h>

#ifdef __cplusplus
extern "C" {
#endif /*__cplusplus*/

ASYN_API int
 asynSetOption(const char *portName, int addr, const char *key, const char *val);
ASYN_API int
 asynShowOption(const char *portName, int addr,const char *key);
ASYN_API int
 asynReport(int level, const char *portName);
ASYN_API int
 asynSetTraceMask(const char *portName,int addr,int mask);
ASYN_API int
 asynSetTraceIOMask(const char *portName,int addr,int mask);
ASYN_API int
 asynSetTraceInfoMask(const char *portName,int addr,int mask);
ASYN_API int
 asynSetTraceFile(const char *portName,int addr,const char *filename);
ASYN_API int
 asynSetTraceIOTruncateSize(const char *portName,int addr,int size);
ASYN_API int
 asynAutoConnect(const char *portName,int addr,int yesNo);
ASYN_API int
 asynEnable(const char *portName,int addr,int yesNo);

ASYN_API int
 asynOctetConnect(const char *entry, const char *port, int addr,
                  int timeout, int buffer_len,const char *drvInfo);
ASYN_API int
 asynOctetDisconnect(const char *entry);
ASYN_API int
 asynWaitConnect(const char *portName, double timeout);
ASYN_API int
 asynOctetRead(const char *entry, int nread);
ASYN_API int
 asynOctetWrite(const char *entry, const char *output);
ASYN_API int
 asynOctetWriteRead(const char *entry, const char *output, int nread);
ASYN_API int
 asynOctetFlush(const char *entry);
ASYN_API int
 asynOctetSetInputEos(const char *port, int addr,const char *eos);
ASYN_API int
 asynOctetGetInputEos(const char *port, int addr);
ASYN_API int
 asynOctetSetOutputEos(const char *port, int addr,const char *eos);
ASYN_API int
 asynOctetGetOutputEos(const char *port, int addr);
ASYN_API int
 asynRegisterTimeStampSource(const char *portName, const char *functionName);
ASYN_API int
 asynUnregisterTimeStampSource(const char *portName);
ASYN_API int
 asynSetMinTimerPeriod(double period);
ASYN_API int
 asynSetQueueLockPortTimeout(const char *portName, double timeout);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /*INCasynShellCommandsh*/
