/*asynGpibDriver.h */
/***********************************************************************
* Copyright (c) 2002 The University of Chicago, as Operator of Argonne
* National Laboratory, and the Regents of the University of
* California, as Operator of Los Alamos National Laboratory, and
* Berliner Elektronenspeicherring-Gesellschaft m.b.H. (BESSY).
* asynDriver is distributed subject to a Software License Agreement
* found in file LICENSE that is included with this distribution.
***********************************************************************/

#ifndef INCasynGpibh
#define INCasynGpibh
#include <shareLib.h>

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/* GPIB Addressed Commands*/
#define IBGTL "\x01"    /* Go to local */
#define IBSDC "\x04"    /* Selective Device Clear */
#define IBGET "\x08"    /* Group Execute Trigger */
#define IBTCT "\x09"    /* Take Control */

/* GPIB Universal Commands*/
#define IBDCL 0x14      /* Device Clear */
#define IBLLO 0x11      /* Local Lockout */
#define IBSPE 0x18      /* Serial Poll Enable */
#define IBSPD 0x19      /* Serial Poll Disable */
#define IBUNT 0x5f      /* Untalk */
#define IBUNL 0x3f      /* Unlisten */

/* Talk, Listen, Secondary base addresses */
#define TADBASE    0x40   /* offset to GPIB listen address 0 */
#define LADBASE    0x20   /* offset to GPIB talk address 0 */
#define SADBASE    0x60   /* offset to GPIB secondary address 0 */

#define NUM_GPIB_ADDRESSES    32
#include "asynDriver.h"
#include "asynInt32.h"
#define asynGpibType "asynGpib"
/* GPIB drivers */
typedef struct asynGpib asynGpib;
typedef struct asynGpibPort asynGpibPort;
/*asynGpib defines methods called by gpib aware users*/
struct asynGpib{
    /*addressedCmd,...,ren are just passed to device handler*/
    asynStatus (*addressedCmd) (void *drvPvt,asynUser *pasynUser,
        const char *data, int length);
    asynStatus (*universalCmd) (void *drvPvt,asynUser *pasynUser,int cmd);
    asynStatus (*ifc) (void *drvPvt,asynUser *pasynUser);
    asynStatus (*ren) (void *drvPvt,asynUser *pasynUser, int onOff);
    /* The following are implemented by asynGpib */
    asynStatus (*pollAddr)(void *drvPvt,asynUser *pasynUser, int onOff);
    /* The following are called by low level gpib drivers */
    /*srqHappened is passed the pointer returned by registerPort*/
    void *(*registerPort)(
        const char *portName,
        int attributes,int autoConnect,
        asynGpibPort *pasynGpibPort, void *asynGpibPortPvt,
        unsigned int priority, unsigned int stackSize);
    void (*srqHappened)(void *asynGpibPvt);
};
epicsShareExtern asynGpib *pasynGpib;

struct asynGpibPort {
    /*asynCommon methods */
    void (*report)(void *drvPvt,FILE *fd,int details);
    asynStatus (*connect)(void *drvPvt,asynUser *pasynUser);
    asynStatus (*disconnect)(void *drvPvt,asynUser *pasynUser);
    /*asynOctet methods passed through from asynGpib*/
    asynStatus (*read)(void *drvPvt,asynUser *pasynUser,
                      char *data,int maxchars,int *nbytesTransfered,
                      int *eomReason);
    asynStatus (*write)(void *drvPvt,asynUser *pasynUser,
                      const char *data,int numchars,int *nbytesTransfered);
    asynStatus (*flush)(void *drvPvt,asynUser *pasynUser);
    asynStatus (*setEos)(void *drvPvt,asynUser *pasynUser,
                const char *eos,int eoslen);
    asynStatus (*getEos)(void *drvPvt,asynUser *pasynUser,
                char *eos, int eossize, int *eoslen);
    /*asynGpib methods passed thrtough from asynGpib*/
    asynStatus (*addressedCmd) (void *drvPvt,asynUser *pasynUser,
                const char *data, int length);
    asynStatus (*universalCmd) (void *drvPvt, asynUser *pasynUser, int cmd);
    asynStatus (*ifc) (void *drvPvt,asynUser *pasynUser);
    asynStatus (*ren) (void *drvPvt,asynUser *pasynUser, int onOff);
    /*asynGpibPort specific methods */
    asynStatus (*srqStatus) (void *drvPvt,int *isSet);
    asynStatus (*srqEnable) (void *drvPvt, int onOff);
    asynStatus (*serialPollBegin) (void *drvPvt);
    asynStatus (*serialPoll) (void *drvPvt, int addr, double timeout,int *status);
    asynStatus (*serialPollEnd) (void *drvPvt);
};

#ifdef __cplusplus
}
#endif  /* __cplusplus */

#endif  /* INCasynGpibh */
