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

typedef struct asynUser {
    char *errorMessage;
    int errorMessageSize;
    /* The following can be set by the user */
    double timeout;  /*Timeout for I/O operations*/
    void *userPvt; 
}asynUser;

typedef void (*userCallback)(asynUser *pasynUser);

typedef struct asynInterface{
    const char *interfaceType; /*For example asynCommonType*/
    void *pinterface;          /*For example pasynCommon */
    void *drvPvt;
}asynInterface;

typedef struct asynManager {
    void (*report)(FILE *fd,int details);
    asynUser  *(*createAsynUser)(userCallback queue,userCallback timeout);
    asynStatus (*freeAsynUser)(asynUser *pasynUser);
    asynStatus (*connectDevice)(asynUser *pasynUser,
        const char *portName,int addr);
    asynStatus (*disconnectDevice)(asynUser *pasynUser);
    asynInterface *(*findInterface)(asynUser *pasynUser,
        const char *interfaceType,int processModuleOK);
    asynStatus (*queueRequest)(asynUser *pasynUser,
        asynQueuePriority priority,double timeout);
    /*cancelRequest returns (0,-1) if request (was, was not) queued*/
    int (*cancelRequest)(asynUser *pasynUser);
    asynStatus (*lock)(asynUser *pasynUser);   /*lock portName,addr */
    asynStatus (*unlock)(asynUser *pasynUser);
    int (*getAddr)(asynUser *pasynUser);
    /* drivers call the following*/
    asynStatus (*registerPort)(
        const char *portName,
        asynInterface *paasynInterface,int nasynInterface,
        unsigned int priority,unsigned int stackSize);
    /* paasynInterface is pointer to array of asynInterface */
    /*process modules call the following */
    asynStatus (*registerProcessModule)(
        const char *processModuleName,const char *portName,int addr,
        asynInterface *paasynInterface,int nasynInterface);
}asynManager;
epicsShareExtern asynManager *pasynManager;

/*Methods supported by ALL asyn drivers*/
#define asynCommonType "asynCommon"
typedef struct  asynCommon {
    void       (*report)(void *drvPvt,FILE *fd,int details);
    /*following are to connect/disconnect to/from hardware*/
    asynStatus (*connect)(void *drvPvt,asynUser *pasynUser);
    asynStatus (*disconnect)(void *drvPvt,asynUser *pasynUser);
}asynCommon;

/* Methods supported by low level octet drivers. */
#define asynOctetType "asynOctet"
typedef struct asynOctet{
    int        (*read)(void *drvPvt,asynUser *pasynUser,
                       char *data,int maxchars);
    int        (*write)(void *drvPvt,asynUser *pasynUser,
                        const char *data,int numchars);
    asynStatus (*flush)(void *drvPvt,asynUser *pasynUser);
    asynStatus (*setEos)(void *drvPvt,asynUser *pasynUser,
                         const char *eos,int eoslen);
}asynOctet;


#ifdef __cplusplus
}
#endif  /* __cplusplus */
#endif  /* ASYNDRIVER_H */
