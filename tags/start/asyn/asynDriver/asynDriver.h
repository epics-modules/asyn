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

typedef struct asynPvt asynPvt;
typedef struct userPvt userPvt;
typedef struct drvPvt drvPvt;

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

typedef void (*userCallback)(userPvt *puserPvt);

typedef struct asynUser {
    userCallback callback;
    userPvt *puserPvt;
    char *errorMessage;
    int errorMessageSize;
    asynPvt *pasynPvt; /* private to asynQueueManager */
    drvPvt *pdrvPvt; /* Set by asynQueueManager. Used by drivers*/
}asynUser;

typedef void (*peekHandler)(userPvt *puserPvt,
    char byte,int isReceive,int gotEOI);

typedef struct driverInfo{
    void *pinterface;
    const char *driverType;
}driverInfo;

typedef struct asynQueueManager {
    void (*report)(int details);
    asynUser  *(*createAsynUser)(userCallback callback, userPvt *puserPvt);
    asynStatus (*freeAsynUser)(asynUser *pasynUser);
    asynStatus (*connectDevice)(asynUser *pasynUser, const char *name);
    asynStatus (*disconnectDevice)(asynUser *pasynUser);
    /*The following returns address of driver interface*/
    void       *(*findDriver)(asynUser *pasynUser,const char *driverType);
    asynStatus (*queueRequest)(asynUser *pasynUser,asynQueuePriority priority);
    asynCancelStatus (*cancelRequest)(asynUser *pasynUser);
    asynStatus (*lock)(asynUser *pasynUser,double timeout);
    asynStatus (*unlock)(asynUser *pasynUser);
    /* drivers call the following*/
    asynPvt   *(*registerDriver)(drvPvt *pdrvPvt, const char *name,
                              driverInfo *padriverInfo,
                              int ndriverTypes,
                              unsigned int priority,/*epicsThreadPriority*/
                              unsigned int stackSize);/*stack size*/
}asynQueueManager;
epicsShareExtern asynQueueManager *pasynQueueManager;

/*Methods supported by ALL asyn drivers*/
#define asynDriverType "asynDriver"
typedef struct  asynDriver {
    void       (*report)(asynUser *pasynUser,int details);
    /*following are to connect/disconnect to/from hardware*/
    asynStatus (*connect)(asynUser *pasynUser);
    asynStatus (*disconnect)(asynUser *pasynUser);
}asynDriver;

/* Methods supported by low level octet drivers. */
#define octetDriverType "octetDriver"
typedef struct octetDriver{
    int        (*read)(asynUser *pasynUser,int addr,char *data,int maxchars);
    int        (*write)(asynUser *pasynUser,
                        int addr,const char *data,int numchars);
    asynStatus (*flush)(asynUser *pasynUser,int addr);
    asynStatus (*setTimeout)(asynUser *pasynUser, 
                             asynTimeoutType type,double timeout);
    asynStatus (*setEos)(asynUser *pasynUser,const char *eos,int eoslen);
    asynStatus (*installPeekHandler)(asynUser *pasynUser,peekHandler handler);
    asynStatus (*removePeekHandler)(asynUser *pasynUser);
}octetDriver;


#ifdef __cplusplus
}
#endif  /* __cplusplus */
#endif  /* ASYNDRIVER_H */
