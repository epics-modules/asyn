/* asynDriver.h */
/***********************************************************************
* Copyright (c) 2002 The University of Chicago, as Operator of Argonne
* National Laboratory, and the Regents of the University of
* California, as Operator of Los Alamos National Laboratory, and
* Berliner Elektronenspeicherring-Gesellschaft m.b.H. (BESSY).
* gpibCore is distributed subject to a Software License Agreement
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
    asynSuccess,asynTimeout,asynError
}asynStatus;

typedef enum {
    asynQueuePriorityLow,asynQueuePriorityMedium,asynQueuePriorityHigh
}asynQueuePriority;

typedef enum {
    asynTimoutFirst, /*timeout for first input character.*/
    asynTimoutRead,
    asynTimeoutWrite
}asynTimeoutType;

typedef enum {
   asynCancelSuccess,
   asynCancelCallbackActive,
   asynCancelError
}asynCancelStatus;

typedef void (*userCallback)(void *puserPvt);

typedef struct asynUser {
    char *errorMessage;
    int errorMessageSize;
    void *puserPvt; 
}asynUser;

typedef void (*peekHandler)(void *puserPvt,
    char byte,int isReceive,int gotEOI);

typedef struct driverInterface{
    const char *driverType;
    void *pinterface;
}driverInterface;

typedef struct deviceDriver{
    driverInterface *pdriverInterface;
    void *pdrvPvt;
}deviceDriver;

typedef struct asynQueueManager {
    void (*report)(int details);
    asynUser  *(*createAsynUser)(
        userCallback queue,userCallback timeout,void *puserPvt);
    asynStatus (*freeAsynUser)(asynUser *pasynUser);
    asynStatus (*connectDevice)(asynUser *pasynUser, const char *deviceName);
    asynStatus (*disconnectDevice)(asynUser *pasynUser);
    deviceDriver *(*findDriver)(asynUser *pasynUser,
        const char *driverType,int processModuleOK);
    asynStatus (*queueRequest)(asynUser *pasynUser,
        asynQueuePriority priority,double timeout);
    asynCancelStatus (*cancelRequest)(asynUser *pasynUser);
    asynStatus (*lock)(asynUser *pasynUser);
    asynStatus (*unlock)(asynUser *pasynUser);
    /* drivers call the following*/
    asynStatus (*registerDevice)(
        const char *deviceName,
        deviceDriver *padeviceDriver,int ndeviceDrivers,
        unsigned int priority,unsigned int stackSize);
    /*process modules call the following */
    asynStatus (*registerProcessModule)(
        const char *processModuleName,const char *deviceName,
        deviceDriver *padeviceDriver,int ndeviceDrivers);
}asynQueueManager;
epicsShareExtern asynQueueManager *pasynQueueManager;

/*Methods supported by ALL asyn drivers*/
#define asynDriverType "asynDriver"
typedef struct  asynDriver {
    void       (*report)(void *pdrvPvt,asynUser *pasynUser,int details);
    /*following are to connect/disconnect to/from hardware*/
    asynStatus (*connect)(void *pdrvPvt,asynUser *pasynUser);
    asynStatus (*disconnect)(void *pdrvPvt,asynUser *pasynUser);
}asynDriver;

/* Methods supported by low level octet drivers. */
#define octetDriverType "octetDriver"
typedef struct octetDriver{
    int        (*read)(void *pdrvPvt,asynUser *pasynUser,int addr,char *data,int maxchars);
    int        (*write)(void *pdrvPvt,asynUser *pasynUser,
                        int addr,const char *data,int numchars);
    asynStatus (*flush)(void *pdrvPvt,asynUser *pasynUser,int addr);
    asynStatus (*setTimeout)(void *pdrvPvt,asynUser *pasynUser, 
                             asynTimeoutType type,double timeout);
    asynStatus (*setEos)(void *pdrvPvt,asynUser *pasynUser,
                         const char *eos,int eoslen);
    asynStatus (*installPeekHandler)(void *pdrvPvt,asynUser *pasynUser,
                                    peekHandler handler);
    asynStatus (*removePeekHandler)(void *pdrvPvt,asynUser *pasynUser);
}octetDriver;


#ifdef __cplusplus
}
#endif  /* __cplusplus */
#endif  /* ASYNDRIVER_H */
