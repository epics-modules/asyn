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

#define NUM_GPIB_ADDRESSES    32
#include "asynDriver.h"
#define gpibDriverType "gpibDriver"
/* GPIB drivers */
typedef void (*srqHandler)(void *userPrivate,int gpibAddr,int statusByte);
typedef struct gpibDriver gpibDriver;
typedef struct gpibDevice gpibDevice;
/*gpibDriver defines methods called by gpib aware users*/
struct gpibDriver{
    /*addressedCmd,...,ren are just passed to device handler*/
    asynStatus (*addressedCmd) (void *drvPvt,asynUser *pasynUser,
        int addr, const char *data, int length);
    asynStatus (*universalCmd) (void *drvPvt,asynUser *pasynUser, int cmd);
    asynStatus (*ifc) (void *drvPvt,asynUser *pasynUser);
    asynStatus (*ren) (void *drvPvt,asynUser *pasynUser, int onOff);
    /* The following are implemented by gpibDriver */
    asynStatus (*registerSrqHandler)(void *drvPvt,asynUser *pasynUser,
        srqHandler handler, void *srqHandlerPvt);
    void (*pollAddr)(void *drvPvt,asynUser *pasynUser,int addr, int onOff);
    /* The following are called by low level gpib drivers */
    /*registerDevice returns pointer passed to srqHappened*/
    void *(*registerDevice)(
        const char *deviceName,
        gpibDevice *pgpibDevice, void *gpibDevicePvt,
        unsigned int priority, unsigned int stackSize);
    void (*srqHappened)(void *gpibDriverPvt);
};
epicsShareExtern gpibDriver *pgpibDriver;
/*gpibDriver is the driver called by user callbacks.
 * It handles SRQ processing, etc. It calls interface specific
 * drivers. The interface specific drivers register with gpibDriver
 * which then registers with asynDriver.
 *
 * registerSrqHandler - registers an SRQ handler for addr.
 * addressedCmd - Issues an addressed command, i.e.send data with ATN true
 * universalCmd - Send a GPIB universial command
 * ifc - Issue an Interface clear
 * ren - if onOff = (0,1) set REN (off,on)
 * registerDevice - A gpib interface driver calls this for each gpib interface.
 *                  gpibDriver registers the same name with asynDriver
 */

struct gpibDevice {
    /*asynDriver methods */
    void (*report)(void *drvPvt,int details);
    asynStatus (*connect)(void *drvPvt,asynUser *pasynUser);
    asynStatus (*disconnect)(void *drvPvt,asynUser *pasynUser);
    /*octetDriver methods passed through from gpibDriver*/
    int (*read)(void *drvPvt,asynUser *pasynUser,
                int addr,char *data,int maxchars);
    int (*write)(void *drvPvt,asynUser *pasynUser,
                int addr,const char *data,int numchars);
    asynStatus (*flush)(void *drvPvt,asynUser *pasynUser,int addr);
    asynStatus (*setEos)(void *drvPvt,asynUser *pasynUser,
                int addr,const char *eos,int eoslen);
    /*gpibDriver methods passed thrtough from gpibDriver*/
    asynStatus (*addressedCmd) (void *drvPvt,asynUser *pasynUser,
        int addr, const char *data, int length);
    asynStatus (*universalCmd) (void *drvPvt, asynUser *pasynUser, int cmd);
    asynStatus (*ifc) (void *drvPvt,asynUser *pasynUser);
    asynStatus (*ren) (void *drvPvt,asynUser *pasynUser, int onOff);
    /*gpibDevice specific methods */
    int (*srqStatus) (void *drvPvt);
    asynStatus (*srqEnable) (void *drvPvt, int onOff);
    asynStatus (*serialPollBegin) (void *drvPvt);
    int (*serialPoll) (void *drvPvt, int addr, double timeout);
    asynStatus (*serialPollEnd) (void *drvPvt);
};
/*gpibDevice is implemented by an interface specific driver.
 *addressedCmd, ..., ren are just like for gpibDriverUser.
 *srqStatus - Returns (0,1) if SRQ (is not, is) set
 *srqEnable - enables/disables SRQs
 *serialPollBegin - Start of a serial poll
 *serialPoll - Poll a specific addresss
 *serialPollEnd - End of serial poll
 */

#endif  /* INCgpibDriverh */
