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

epicsShareFunc int 
 asynSetOption(const char *portName, int addr, const char *key, const char *val);
epicsShareFunc int 
 asynShowOption(const char *portName, int addr,const char *key);
epicsShareFunc int 
 asynReport(int level, const char *portName);
epicsShareFunc int 
 asynSetTraceMask(const char *portName,int addr,int mask);
epicsShareFunc int 
 asynSetTraceIOMask(const char *portName,int addr,int mask);
epicsShareFunc int 
 asynSetTraceInfoMask(const char *portName,int addr,int mask);
epicsShareFunc int 
 asynSetTraceFile(const char *portName,int addr,const char *filename);
epicsShareFunc int 
 asynSetTraceIOTruncateSize(const char *portName,int addr,int size);
epicsShareFunc int 
 asynAutoConnect(const char *portName,int addr,int yesNo);
epicsShareFunc int 
 asynEnable(const char *portName,int addr,int yesNo);

epicsShareFunc int 
 asynOctetConnect(const char *entry, const char *port, int addr,
                  int timeout, int buffer_len,const char *drvInfo);
epicsShareFunc int 
 asynOctetRead(const char *entry, int nread);
epicsShareFunc int 
 asynOctetWrite(const char *entry, const char *output);
epicsShareFunc int 
 asynOctetWriteRead(const char *entry, const char *output, int nread);
epicsShareFunc int 
 asynOctetFlush(const char *entry);
epicsShareFunc int 
 asynOctetSetInputEos(const char *port, int addr,const char *eos);
epicsShareFunc int 
 asynOctetGetInputEos(const char *port, int addr);
epicsShareFunc int 
 asynOctetSetOutputEos(const char *port, int addr,const char *eos);
epicsShareFunc int 
 asynOctetGetOutputEos(const char *port, int addr);
epicsShareFunc int 
 asynRegisterTimeStampSource(const char *portName, const char *functionName);
epicsShareFunc int 
 asynUnregisterTimeStampSource(const char *portName);
epicsShareFunc int
 asynSetMinTimerPeriod(double period);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /*INCasynShellCommandsh*/
