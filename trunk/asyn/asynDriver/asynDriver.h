/* asynDriver.h */
/***********************************************************************
* Copyright (c) 2002 The University of Chicago, as Operator of Argonne
* National Laboratory, and the Regents of the University of
* California, as Operator of Los Alamos National Laboratory, and
* Berliner Elektronenspeicherring-Gesellschaft m.b.H. (BESSY).
* asynDriver is distributed subject to a Software License Agreement
* found in file LICENSE that is included with this distribution.
***********************************************************************/

/* Generic Asynchronous Driver support
 * Author: Marty Kraimer
 */

#ifndef ASYNDRIVER_H
#define ASYNDRIVER_H

#include "shareLib.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

typedef enum {
    asynSuccess,asynTimeout,asynOverflow,asynError
}asynStatus;

typedef enum {
    asynExceptionConnect,asynExceptionEnable,asynExceptionAutoConnect
} asynException;

typedef enum {
    asynQueuePriorityLow,asynQueuePriorityMedium,asynQueuePriorityHigh,
    asynQueuePriorityConnect
}asynQueuePriority;

typedef struct asynUser {
    char *errorMessage;
    int errorMessageSize;
    /* The following can be set by the user */
    double timeout;  /*Timeout for I/O operations*/
    void *userPvt; 
}asynUser;

typedef struct asynInterface{
    const char *interfaceType; /*For example asynCommonType*/
    void *pinterface;          /*For example pasynCommon */
    void *drvPvt;
}asynInterface;

typedef void (*userCallback)(asynUser *pasynUser);
typedef void (*exceptionCallback)(asynUser *pasynUser,asynException exception);

typedef struct asynManager {
    void      (*report)(FILE *fd,int details);
    asynUser  *(*createAsynUser)(userCallback queue,userCallback timeout);
    asynStatus (*freeAsynUser)(asynUser *pasynUser);
    asynStatus (*isMultiDevice)(asynUser *pasynUser,
                                const char *portName,int *yesNo);
    asynStatus (*connectDevice)(asynUser *pasynUser,
                                const char *portName,int addr);
    asynStatus (*disconnect)(asynUser *pasynUser);
    asynStatus (*exceptionCallbackAdd)(asynUser *pasynUser,
                                       exceptionCallback callback);
    asynStatus (*exceptionCallbackRemove)(asynUser *pasynUser);
    asynInterface *(*findInterface)(asynUser *pasynUser,
                            const char *interfaceType,int interposeInterfaceOK);
    asynStatus (*queueRequest)(asynUser *pasynUser,
                              asynQueuePriority priority,double timeout);
    /*cancelRequest returns (0,1) if request (was not, was) queued*/
    int        (*cancelRequest)(asynUser *pasynUser);
    asynStatus (*lock)(asynUser *pasynUser);   /*lock portName,addr */
    asynStatus (*unlock)(asynUser *pasynUser);
    /*getAddr returns -1 if !multiPort or connected to port */
    int        (*getAddr)(asynUser *pasynUser);
    /* drivers call the following*/
    asynStatus (*registerPort)(const char *portName,
                              int multiDevice,int autoConnect,
                              unsigned int priority,unsigned int stackSize);
    asynStatus (*registerInterface)(const char *portName,
                              asynInterface *pasynInterface);
    asynStatus (*exceptionConnect)(asynUser *pasynUser);
    asynStatus (*exceptionDisconnect)(asynUser *pasynUser);
    /*any code can call the following*/
    asynStatus (*interposeInterface)(const char *portName, int addr,
                              asynInterface *pasynInterface,
                              asynInterface **ppPrev);
    asynStatus (*enable)(asynUser *pasynUser,int yesNo);
    asynStatus (*autoConnect)(asynUser *pasynUser,int yesNo);
    int        (*isConnected)(asynUser *pasynUser);
    int        (*isEnabled)(asynUser *pasynUser);
    int        (*isAutoConnect)(asynUser *pasynUser);
}asynManager;
epicsShareExtern asynManager *pasynManager;

/* Interface supported by ALL asyn drivers*/
#define asynCommonType "asynCommon"
typedef struct  asynCommon {
    void       (*report)(void *drvPvt,FILE *fd,int details);
    /*following are to connect/disconnect to/from hardware*/
    asynStatus (*connect)(void *drvPvt,asynUser *pasynUser);
    asynStatus (*disconnect)(void *drvPvt,asynUser *pasynUser);
    /*The following are generic methods to set/get device options*/
    asynStatus (*setOption)(void *drvPvt, asynUser *pasynUser,
                                const char *key, const char *val);
    asynStatus (*getOption)(void *drvPvt, asynUser *pasynUser,
                                const char *key, char *val, int sizeval);
}asynCommon;

/* Interface supported by low level octet drivers. */
#define asynOctetType "asynOctet"
typedef struct asynOctet{
    int        (*read)(void *drvPvt,asynUser *pasynUser,
                       char *data,int maxchars);
    int        (*write)(void *drvPvt,asynUser *pasynUser,
                        const char *data,int numchars);
    asynStatus (*flush)(void *drvPvt,asynUser *pasynUser);
    asynStatus (*setEos)(void *drvPvt,asynUser *pasynUser,
                         const char *eos,int eoslen);
    asynStatus (*getEos)(void *drvPvt,asynUser *pasynUser,
                        char *eos, int eossize, int *eoslen);
}asynOctet;

/*asynTrace is implemented by asynManager*/
/*All asynTrace methods can be called from any thread*/
/* traceMask definitions*/
#define ASYN_TRACE_ERROR     0x0001
#define ASYN_TRACEIO_DEVICE  0x0002
#define ASYN_TRACEIO_FILTER  0x0004
#define ASYN_TRACEIO_DRIVER  0x0008
#define ASYN_TRACE_FLOW      0x0010

/* traceIO mask definitions*/
#define ASYN_TRACEIO_NODATA 0x0000
#define ASYN_TRACEIO_ASCII  0x0001
#define ASYN_TRACEIO_ESCAPE 0x0002
#define ASYN_TRACEIO_HEX    0x0004

/* asynPrint and asynPrintIO are macros that act like
   int asynPrint(asynUser *pasynUser,int reason, const char *format, ... ); 
   int asynPrintIO(asynUser *pasynUser,int reason,
        const char *buffer, int len, const char *format, ... ); 
*/
typedef struct asynTrace {
    /* lock/unlock are only necessary if caller performs I/O other then*/
    /* by calling asynTrace methods                                    */
    asynStatus (*lock)(asynUser *pasynUser);
    asynStatus (*unlock)(asynUser *pasynUser);
    asynStatus (*setTraceMask)(asynUser *pasynUser,int mask);
    int        (*getTraceMask)(asynUser *pasynUser);
    asynStatus (*setTraceIOMask)(asynUser *pasynUser,int mask);
    int        (*getTraceIOMask)(asynUser *pasynUser);
    asynStatus (*setTraceFile)(asynUser *pasynUser,FILE *fd);
    FILE       *(*getTraceFile)(asynUser *pasynUser);
    asynStatus (*setTraceIOTruncateSize)(asynUser *pasynUser,int size);
    int        (*getTraceIOTruncateSize)(asynUser *pasynUser);
    int        (*print)(asynUser *pasynUser,int reason, const char *pformat, ...);
    int        (*printIO)(asynUser *pasynUser,int reason,
               const char *buffer, int len,const char *pformat, ...);
}asynTrace;
epicsShareExtern asynTrace *pasynTrace;

#if defined(__STDC_VERSION__) && __STDC_VERSION__>=199901L
#define asynPrint(pasynUser,reason, ...) \
   ((pasynTrace->getTraceMask((pasynUser))&(reason)) \
    ? pasynTrace->print((pasynUser),(reason),__VAR_ARGS__) \
    : 0)
#elif defined(__GNUC__)
#define asynPrint(pasynUser,reason,format...) \
   ((pasynTrace->getTraceMask((pasynUser))&(reason)) \
    ? pasynTrace->print(pasynUser,reason,format) \
    : 0)
#else
#define asynPrint pasynTrace->print
#endif

#if defined(__STDC_VERSION__) && __STDC_VERSION__>=199901L
#define asynPrintIO(pasynUser,reason,buffer,len, ...) \
   ((pasynTrace->getTraceMask((pasynUser))&(reason)) \
    ? pasynTrace->printIO((pasynUser),(reason),(buffer),(len),__VAR_ARGS__) \
    : 0)
#elif defined(__GNUC__)
#define asynPrintIO(pasynUser,reason,buffer,len,format...) \
   ((pasynTrace->getTraceMask((pasynUser))&(reason)) \
    ? pasynTrace->printIO((pasynUser),(reason),(buffer),(len),format) \
    : 0)
#else
#define asynPrintIO pasynTrace->printIO
#endif

#ifdef __cplusplus
}
#endif  /* __cplusplus */
#endif  /* ASYNDRIVER_H */
