/*asynGpibDriver.h */
/***********************************************************************
* Copyright (c) 2002 The University of Chicago, as Operator of Argonne
* National Laboratory, and the Regents of the University of
* California, as Operator of Los Alamos National Laboratory, and
* Berliner Elektronenspeicherring-Gesellschaft m.b.H. (BESSY).
* gpibCore is distributed subject to a Software License Agreement
* found in file LICENSE that is included with this distribution.
***********************************************************************/

#ifndef INCasynGpibh
#define INCasynGpibh

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/* GPIB Addressed Commands*/
#define IBSDC "\x04"    /* Selective Device Clear */
#define IBGTL "\x01"    /* Go to local */

/* GPIB Universial Commands*/
#define IBDCL 0x14      /* Device Clear */
#define IBLLO 0x11      /* Local Lockout */
#define IBSPE 0x18      /* Serial Poll Enable */
#define IBSPD 0x19      /* Serial Poll Disable */

#define NUM_GPIB_ADDRESSES    32
#include "asynDriver.h"
#define asynGpibType "asynGpib"
/* GPIB drivers */
typedef void (*srqHandler)(void *userPrivate,int gpibAddr,int statusByte);
typedef struct asynGpib asynGpib;
typedef struct asynGpibPort asynGpibPort;
/*asynGpib defines methods called by gpib aware users*/
struct asynGpib{
    /*addressedCmd,...,ren are just passed to device handler*/
    asynStatus (*addressedCmd) (void *drvPvt,asynUser *pasynUser,
        const char *data, int length);
    asynStatus (*universalCmd) (void *drvPvt,asynUser *pasynUser, int cmd);
    asynStatus (*ifc) (void *drvPvt,asynUser *pasynUser);
    asynStatus (*ren) (void *drvPvt,asynUser *pasynUser, int onOff);
    /* The following are implemented by asynGpib */
    asynStatus (*registerSrqHandler)(void *drvPvt,asynUser *pasynUser,
        srqHandler handler, void *srqHandlerPvt);
    void (*pollAddr)(void *drvPvt,asynUser *pasynUser, int onOff);
    /* The following are called by low level gpib drivers */
    /*registerPort returns pointer passed to srqHappened*/
    void *(*registerPort)(
        const char *portName,
        asynGpibPort *pasynGpibPort, void *asynGpibPortPvt,
        unsigned int priority, unsigned int stackSize);
    void (*srqHappened)(void *asynGpibPvt);
};
epicsShareExtern asynGpib *pasynGpib;
/*asynGpib is the driver called by user callbacks.
 * It handles SRQ processing, etc. It calls interface specific
 * drivers. The interface specific drivers register with asynGpib
 * which then registers with asynCommon.
 *
 * registerSrqHandler - registers an SRQ handler for addr.
 * addressedCmd - Issues an addressed command, i.e.send data with ATN true
 * universalCmd - Send a GPIB universial command
 * ifc - Issue an Interface clear
 * ren - if onOff = (0,1) set REN (off,on)
 * registerPort - A gpib interface driver calls this for each gpib interface.
 *                  asynGpib registers the same name with asynCommon
 */

struct asynGpibPort {
    /*asynCommon methods */
    void (*report)(void *drvPvt,int details);
    asynStatus (*connect)(void *drvPvt,asynUser *pasynUser);
    asynStatus (*disconnect)(void *drvPvt,asynUser *pasynUser);
    /*asynOctet methods passed through from asynGpib*/
    int (*read)(void *drvPvt,asynUser *pasynUser,char *data,int maxchars);
    int (*write)(void *drvPvt,asynUser *pasynUser,const char *data,int numchars);
    asynStatus (*flush)(void *drvPvt,asynUser *pasynUser);
    asynStatus (*setEos)(void *drvPvt,asynUser *pasynUser,
                const char *eos,int eoslen);
    /*asynGpib methods passed thrtough from asynGpib*/
    asynStatus (*addressedCmd) (void *drvPvt,asynUser *pasynUser,
                const char *data, int length);
    asynStatus (*universalCmd) (void *drvPvt, asynUser *pasynUser, int cmd);
    asynStatus (*ifc) (void *drvPvt,asynUser *pasynUser);
    asynStatus (*ren) (void *drvPvt,asynUser *pasynUser, int onOff);
    /*asynGpibPort specific methods */
    int (*srqStatus) (void *drvPvt);
    asynStatus (*srqEnable) (void *drvPvt, int onOff);
    asynStatus (*serialPollBegin) (void *drvPvt);
    int (*serialPoll) (void *drvPvt, int addr, double timeout);
    asynStatus (*serialPollEnd) (void *drvPvt);
};
/*asynGpibPort is implemented by an interface specific driver.
 *addressedCmd, ..., ren are just like for asynGpibUser.
 *srqStatus - Returns (0,1) if SRQ (is not, is) set
 *srqEnable - enables/disables SRQs
 *serialPollBegin - Start of a serial poll
 *serialPoll - Poll a specific addresss
 *serialPollEnd - End of serial poll
 */

#ifdef __cplusplus
}
#endif  /* __cplusplus */

#endif  /* INCasynGpibh */
