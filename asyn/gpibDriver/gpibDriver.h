#ifndef INCgpibDriverh
#define INCgpibDriverh

/* GPIB Addressed Commands*/
#define IBSDC "\x04"    /* Selective Device Clear */
#define IBGTL "\x01"    /* Go to local */

/* GPIB Universial Commands*/
#define IBDCL 0x14      /* Device Clear */
#define IBLLO 0x11      /* Local Lockout */
#define IBSPE 0x18      /* Serial Poll Enable */
#define IBSPD 0x19      /* Serial Poll Disable */

#include "asynDriver.h"
#define gpibDriverUserType "gpibDriverUser"
/* GPIB drivers */
typedef void (*srqHandler)(void *userPrivate,int gpibAddr,int statusByte);
typedef struct gpibDriverUser gpibDriverUser;
typedef struct gpibDriver gpibDriver;
struct gpibDriverUser{
    /* The following are called by gpib aware users*/
    asynStatus (*registerSrqHandler)(void *pdrvPvt,asynUser *pasynUser,
        srqHandler handler, void *userPrivate);
    asynStatus (*addressedCmd) (void *pdrvPvt,asynUser *pasynUser,
        int addr, const char *data, int length);
    asynStatus (*universalCmd) (void *pdrvPvt,asynUser *pasynUser, int cmd);
    asynStatus (*ifc) (void *pdrvPvt,asynUser *pasynUser);
    asynStatus (*ren) (void *pdrvPvt,asynUser *pasynUser, int onOff);
    void (*pollAddr)(void *pdrvPvt,asynUser *pasynUser,int addr, int onOff);
    void (*srqProcessing)(void *pdrvPvt,asynUser *pasynUser, int onOff);
    void (*srqSet)(void *pdrvPvt,asynUser *pasynUser,
        double srqTimeout,double pollTimeout,double pollRate,
        int srqMaxEvents);
    void (*srqGet)(void *pdrvPvt,asynUser *pasynUser,
        double *srqTimeout,double *pollTimeout,double *pollRate,
        int *srqMaxEvents);
    /* The following are called by low level gpib drivers */
    /*registerDevice returns pointer passed to srqHappened*/
    void *(*registerDevice)(
        const char *deviceName,
        gpibDriver *pgpibDriver, void *pdrvPvt,
        unsigned int priority, unsigned int stackSize);
    void (*srqHappened)(void *pgpibPvt);
};
epicsShareExtern gpibDriverUser *pgpibDriverUser;
/*gpibDriverUser is the driver called by user callbacks.
 * It handles SRQ processing, etc. It calls interface specific
 * drivers. The interface specific drivers register with gpibDriverUser
 * which then registers with asynDriver.
 *
 * registerSrqHandler - registers an SRQ handler for addr.
 * addressedCmd - Issues an addressed command, i.e.send data with ATN true
 * universalCmd - Send a GPIB universial command
 * ifc - Issue an Interface clear
 * ren - if onOff = (0,1) set REN (off,on)
 * registerDevice - A gpib interface driver calls this for each gpib interface.
 *                  gpibDriverUser registers the same name with asynDriver
 */

struct gpibDriver {
    void (*report)(void *pdrvPvt,asynUser *pasynUser,int details);
    asynStatus (*connect)(void *pdrvPvt,asynUser *pasynUser);
    asynStatus (*disconnect)(void *pdrvPvt,asynUser *pasynUser);
    /*octetDriver methods */
    int (*read)(void *pdrvPvt,asynUser *pasynUser,
                int addr,char *data,int maxchars);
    int (*write)(void *pdrvPvt,asynUser *pasynUser,
                int addr,const char *data,int numchars);
    asynStatus (*flush)(void *pdrvPvt,asynUser *pasynUser,int addr);
    asynStatus (*setTimeout)(void *pdrvPvt,asynUser *pasynUser,
                asynTimeoutType type,double timeout);
    asynStatus (*setEos)(void *pdrvPvt,asynUser *pasynUser,const char *eos,int eoslen);
    asynStatus (*installPeekHandler)(void *pdrvPvt,asynUser *pasynUser,peekHandler handler);
    asynStatus (*removePeekHandler)(void *pdrvPvt,asynUser *pasynUser);
    /*gpibDriver methods*/
    asynStatus (*registerSrqHandler)(void *pdrvPvt,asynUser *pasynUser,
        srqHandler handler, void *userPrivate);
    asynStatus (*addressedCmd) (void *pdrvPvt,asynUser *pasynUser,
        int addr, const char *data, int length);
    asynStatus (*universalCmd) (void *pdrvPvt, asynUser *pasynUser, int cmd);
    asynStatus (*ifc) (void *pdrvPvt,asynUser *pasynUser);
    asynStatus (*ren) (void *pdrvPvt,asynUser *pasynUser, int onOff);
    int (*srqStatus) (void *pdrvPvt);
    asynStatus (*srqEnable) (void *pdrvPvt, int onOff);
    asynStatus (*serialPollBegin) (void *pdrvPvt);
    int (*serialPoll) (void *pdrvPvt, int addr, double timeout);
    asynStatus (*serialPollEnd) (void *pdrvPvt);
};
/*gpibDriver is implemented by an interface specific driver.
 *registerSrqHandler, ..., ren are just like for gpibDriverUser.
 *srqStatus - Returns (0,1) if SRQ (is not, is) set
 *srqEnable - enables SRQs
 *srqDisable - disables SRQs
 *serialPollBegin - Start of a serial poll
 *serialPoll - Poll a specific addresss
 *serialPollEnd - End of serial poll
 */

#endif  /* INCgpibDriverh */
