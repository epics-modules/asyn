#include "asynDriver.h"
#define gpibDriverUserType "gpibDriverUser"
/* GPIB drivers */
typedef struct gpibDriverUser{
    /* The following are called by gpib aware users*/
    asynStatus (*registerSrqHandler)(asynUser *pasynUser,
        int addr, srqHandlerFunc handler, void *parm);
    asynStatus (*addressedCmd) (asynUser *pasynUser,
        int addr, char *data, int length, int timeout);
    asynStatus (*universalCmd) (drvPvt * pvt, int cmd);
    asynStatus (*ifc) (drvPvt * pvt);
    asynStatus (*ren) (drvPvt * pvt, int onOff);
    void (*pollAddr)(drvPvt *pvt,int addr, int onOff);
    void (*srqProcessing)(drvPvt *pvt, int onOff);
    void (*srqSet)(drvPvt *pvt,
        double srqTimeout,double pollTimeout,double pollRate,
        int srqMaxEvents);
    void (*srqGet)(drvPvt *pvt,
        double *srqTimeout,double *pollTimeout,double *pollRate,
        int *srqMaxEvents);
    /* The following are called by low level gpib drivers */
    drvPvt *(*registerDriver)(drvPvt pvt,const char *name);
    void (*srqHappened)(drvPvt pvt); /*pvt is gpibDriverUser pvt*/
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
    /*asynDriver methods */
    void (*report)(asynUser *pasynUser,int details);
    void (*connect)(drvPvt *pdrvPvt);
    void (*disconnect)(drvPvt *pdrvPvt);
    /*octetDriver methods */
    int (*read)(asynUser *pasynUser,int addr,char *data,int maxchars);
    int (*write)(asynUser *pasynUser,
                        int addr,const char *data,int numchars);
    asynStatus (*flush)(asynUser *pasynUser,int addr);
    asynStatus (*setTimeout)(asynUser *pasynUser,
                             asynTimeout type,double timeout);
    asynStatus (*setEos)(asynUser *pasynUser,const char *eos,int eoslen);
    asynStatus (*installPeekHandler)(asynUser *pasynUser,peekHandler handler);
    asynStatus (*removePeekHandler)(asynUser *pasynUser);
    /*gpibDriver methods*/
    asynStatus (*registerSrqHandler)(drvPvt pvt,
        int addr, srqHandlerFunc handler, void *parm);
    asynStatus (*addressedCmd) (drvPvt pvt,
        int addr, char *data, int length, int timeout);
    asynStatus (*universalCmd) (drvPvt * pvt, int cmd);
    asynStatus (*ifc) (drvPvt * pvt);
    asynStatus (*ren) (drvPvt * pvt, int onOff);
    int (*srqStatus) (drvPvt * pvt);
    asynStatus (*srqEnable) (drvPvt * pvt, int onOff);
    asynStatus (*serialPollBegin) (drvPvt * pvt);
    int (*serialPoll) (drvPvt * pibLink, int addr, double timeout);
    asynStatus (*serialPollEnd) (drvPvt * pibLink);
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
