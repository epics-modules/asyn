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

#include <epicsStdio.h>
#include <ellLib.h>
#include <shareLib.h>

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */


typedef enum {
    asynSuccess,asynTimeout,asynOverflow,asynError
}asynStatus;

typedef enum {
    asynExceptionConnect,asynExceptionEnable,asynExceptionAutoConnect,
    asynExceptionTraceMask,asynExceptionTraceIOMask,
    asynExceptionTraceFile,asynExceptionTraceIOTruncateSize
} asynException;

typedef enum {
    asynQueuePriorityLow,asynQueuePriorityMedium,asynQueuePriorityHigh,
    asynQueuePriorityConnect
}asynQueuePriority;

typedef struct asynUser {
    char *errorMessage;
    int errorMessageSize;
    /* timeout must be set by the user */
    double       timeout;  /*Timeout for I/O operations*/
    void         *userPvt; 
    void         *userData; 
    /*The following is for user to/from driver communication*/
    void         *drvUser;
    /*The following is normally set by driver*/
    int          reason;
    /* The following are for additional information from method calls */
    int          auxStatus; /*For auxillary status*/
}asynUser;

typedef struct asynInterface{
    const char *interfaceType; /*For example asynCommonType*/
    void *pinterface;          /*For example pasynCommon */
    void *drvPvt;
}asynInterface;

/*registerPort attributes*/
#define ASYN_MULTIDEVICE  0x0001
#define ASYN_CANBLOCK     0x0002

/*standard values for asynUser.reason*/
#define ASYN_REASON_SIGNAL -1

typedef void (*userCallback)(asynUser *pasynUser);
typedef void (*exceptionCallback)(asynUser *pasynUser,asynException exception);

typedef struct interruptNode{
    ELLNODE node;
    void    *drvPvt;
}interruptNode;

typedef struct asynManager {
    void      (*report)(FILE *fp,int details,const char*portName);
    asynUser  *(*createAsynUser)(userCallback process,userCallback timeout);
    asynUser  *(*duplicateAsynUser)(asynUser *pasynUser,
                                 userCallback queue,userCallback timeout);
    asynStatus (*freeAsynUser)(asynUser *pasynUser);
    void       *(*memMalloc)(size_t size);
    void       (*memFree)(void *pmem,size_t size);
    asynStatus (*isMultiDevice)(asynUser *pasynUser,
                                const char *portName,int *yesNo);
    /* addr = (-1,>=0) => connect to (port,device) */
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
    asynStatus (*cancelRequest)(asynUser *pasynUser,int *wasQueued);
    asynStatus (*blockProcessCallback)(asynUser *pasynUser, int allDevices);
    asynStatus (*unblockProcessCallback)(asynUser *pasynUser, int allDevices);
    asynStatus (*lockPort)(asynUser *pasynUser);
    asynStatus (*unlockPort)(asynUser *pasynUser);
    asynStatus (*canBlock)(asynUser *pasynUser,int *yesNo);
    asynStatus (*getAddr)(asynUser *pasynUser,int *addr);
    asynStatus (*getPortName)(asynUser *pasynUser,const char **pportName);
    /* drivers call the following*/
    asynStatus (*registerPort)(const char *portName,
                              int attributes,int autoConnect,
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
    asynStatus (*isConnected)(asynUser *pasynUser,int *yesNo);
    asynStatus (*isEnabled)(asynUser *pasynUser,int *yesNo);
    asynStatus (*isAutoConnect)(asynUser *pasynUser,int *yesNo);
    /*The following are methods for interrupts*/
    asynStatus (*registerInterruptSource)(const char *portName,
                               asynInterface *pasynInterface, void **pasynPvt);
    asynStatus (*getInterruptPvt)(asynUser *pasynUser,
                               const char *interfaceType, void **pasynPvt);
    interruptNode *(*createInterruptNode)(void *pasynPvt);
    asynStatus (*freeInterruptNode)(asynUser *pasynUser,interruptNode *pnode);
    asynStatus (*addInterruptUser)(asynUser *pasynUser,
                                  interruptNode*pinterruptNode);
    asynStatus (*removeInterruptUser)(asynUser *pasynUser,
                                  interruptNode*pinterruptNode);
    asynStatus (*interruptStart)(void *pasynPvt,ELLLIST **plist);
    asynStatus (*interruptEnd)(void *pasynPvt);
    const char *(*strStatus)(asynStatus status);
}asynManager;
epicsShareExtern asynManager *pasynManager;

/* Interface supported by ALL asyn drivers*/
#define asynCommonType "asynCommon"
typedef struct  asynCommon {
    /*report does not have to be called from queueRequest*/
    void       (*report)(void *drvPvt,FILE *fp,int details);
    asynStatus (*connect)(void *drvPvt,asynUser *pasynUser);
    asynStatus (*disconnect)(void *drvPvt,asynUser *pasynUser);
}asynCommon;

/* asynLockPortNotify is for address change drivers */
#define asynLockPortNotifyType "asynLockPortNotify"
typedef struct  asynLockPortNotify {
    asynStatus (*lock)(void *drvPvt,asynUser *pasynUser);
    asynStatus (*unlock)(void *drvPvt,asynUser *pasynUser);
}asynLockPortNotify;

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
        const char *buffer, size_t len, const char *format, ... ); 
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
    asynStatus (*setTraceFile)(asynUser *pasynUser,FILE *fp);
    FILE       *(*getTraceFile)(asynUser *pasynUser);
    asynStatus (*setTraceIOTruncateSize)(asynUser *pasynUser,size_t size);
    size_t     (*getTraceIOTruncateSize)(asynUser *pasynUser);
    int        (*print)(asynUser *pasynUser,int reason, const char *pformat, ...);
    int        (*vprint)(asynUser *pasynUser,int reason, const char *pformat, va_list pvar);
    int        (*printIO)(asynUser *pasynUser,int reason,
               const char *buffer, size_t len,const char *pformat, ...);
    int        (*vprintIO)(asynUser *pasynUser,int reason,
               const char *buffer, size_t len,const char *pformat, va_list pvar);
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
