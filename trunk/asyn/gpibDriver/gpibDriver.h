#include "asynDriver.h"
#define gpibDriverUserType "gpibDriverUser"
/* GPIB drivers */
typedef void (*srqHandler)(void *userPrivate,int statusByte);
typedef struct gpibDriverUser{
    /* The following are called by gpib aware users*/
    asynStatus (*registerSrqHandler)(void *pdrvPvt,asynUser *pasynUser,
        int addr, srqHandler handler, void *userPrivate);
    asynStatus (*addressedCmd) (void *pdrvPvt,asynUser *pasynUser,
        int addr, char *data, int length, int timeout);
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
    drvPvt *(*registerDriver)(void *pdrvPvt,const char *name);
    void (*srqHappened)(void *pdrvPvt); /*pvt is gpibDriverUser pvt*/
}gpibDriverUser;
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
 * registerDriver - A gpib interface driver calls this for each gpib interface.
 *                  gpibDriverUser registers the same name with asynDriver
 */

typedef struct gpibDriver {
    void (*report)(void *pdrvPvt,asynUser *pasynUser,int details);
    void (*connect)(void *pdrvPvt,asynUser *pasynUser);
    void (*disconnect)(void *pdrvPvt,asynUser *pasynUser);
    /*octetDriver methods */
    int (*read)(void *pdrvPvt,asynUser *pasynUser,int addr,char *data,int maxchars);
    int (*write)(void *pdrvPvt,asynUser *pasynUser,
                        int addr,const char *data,int numchars);
    asynStatus (*flush)(void *pdrvPvt,asynUser *pasynUser,int addr);
    asynStatus (*setTimeout)(void *pdrvPvt,asynUser *pasynUser, double timeout);
    asynStatus (*setEos)(void *pdrvPvt,asynUser *pasynUser,const char *eos,int eoslen);
    asynStatus (*installPeekHandler)(void *pdrvPvt,asynUser *pasynUser,peekHandler handler);
    asynStatus (*removePeekHandler)(void *pdrvPvt,asynUser *pasynUser);
    /*gpibDriver methods*/
    asynStatus (*registerSrqHandler)(void *pdrvPvt,
        int addr, srqHandler handler, void *userPrivate);
    asynStatus (*addressedCmd) (void *pdrvPvt,
        int addr, char *data, int length, int timeout);
    asynStatus (*universalCmd) (void *pdrvPvt, int cmd);
    asynStatus (*ifc) (void *pdrvPvt);
    asynStatus (*ren) (void *pdrvPvt, int onOff);
    int (*srqStatus) (void *pdrvPvt);
    asynStatus (*srqEnable) (void *pdrvPvt, int onOff);
    asynStatus (*serialPollBegin) (void *pdrvPvt);
    int (*serialPoll) (void *pdrvPvt, int addr, double timeout);
    asynStatus (*serialPollEnd) (void *pdrvPvt);
}gpibDriver;
/*gpibDriver is implemented by an interface specific driver.
 *registerSrqHandler, ..., ren are just like for gpibDriverUser.
 *srqStatus - Returns (0,1) if SRQ (is not, is) set
 *srqEnable - enables SRQs
 *srqDisable - disables SRQs
 *serialPollBegin - Start of a serial poll
 *serialPoll - Poll a specific addresss
 *serialPollEnd - End of serial poll
 */
