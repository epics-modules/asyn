/*drvVxi11.c*/
/***********************************************************************
* Copyright (c) 2002 The University of Chicago, as Operator of Argonne
* National Laboratory, and the Regents of the University of
* California, as Operator of Los Alamos National Laboratory, and
* Berliner Elektronenspeicherring-Gesellschaft m.b.H. (BESSY).
* asynDriver is distributed subject to a Software License Agreement
* found in file LICENSE that is included with this distribution.
***********************************************************************/
/*
 * Low level GPIB driver for HP E2050A LAN-GPIB gateway
 *
 * Original Author: Benjamin Franksen
 * Current Authors: Marty Kraimer, Eric Norum
 *****************************************************************************/
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <ctype.h>
#include <limits.h>

/* epics includes */
#include <dbDefs.h>
#include <taskwd.h>
#include <epicsMutex.h>
#include <epicsEvent.h>
#include <epicsSignal.h>
#include <epicsStdio.h>
#include <epicsStdlib.h>
#include <osiSock.h>
#include <epicsThread.h>
#include <epicsTime.h>
#include <cantProceed.h>
#include <epicsString.h>
#include <epicsInterruptibleSyscall.h>
/* local includes */
#include "vxi11.h"
#include "osiRpc.h"
#include "vxi11core.h"
#include "vxi11intr.h"
#include <epicsExport.h>
#include "asynDriver.h"
#include "asynOctet.h"
#include "asynOption.h"
#include "asynGpibDriver.h"
#include "drvVxi11.h"

#ifdef BOOL
#undef BOOL
#endif
#define BOOL int

#define FLAG_RECOVER_WITH_IFC   0x1
#define FLAG_LOCK_DEVICES       0x2

#define DEFAULT_RPC_TIMEOUT 4

typedef struct devLink {
    Device_Link lid;
    BOOL        connected;
    int         eos;
}devLink;
typedef struct linkPrimary {
    devLink primary;
    devLink secondary[NUM_GPIB_ADDRESSES];
}linkPrimary;
/******************************************************************************
 * This structure is used to hold the hardware-specific information for a
 * single GPIB link. There is one for each gateway.
 ******************************************************************************/
typedef struct vxiPort {
    void          *asynGpibPvt;
    BOOL          rpcTaskInitCalled; /*Only call rpcTaskInit once*/
    struct timeval vxiRpcTimeout;/* time to wait for RPC completion */
    const char    *portName;
    char          *hostName;   /* ip address of VXI-11 server */
    char          *vxiName;   /* Holds name of logical link */
    int           ctrlAddr;
    BOOL          isGpibLink;  /* Is the VXI-11.2 */
    BOOL          isSingleLink; /* Is this VXI-11.3 */
    BOOL          singleLinkInterrupt; /*Is there an unhandled interrupt */
    struct in_addr inAddr; /* ip address of gateway */
    CLIENT        *rpcClient; /* returned by clnttcp_create() */
    unsigned long maxRecvSize; /* max. # of bytes accepted by link */
    unsigned short abortPort;   /* TCP port for abort channel */
    double        defTimeout;
    devLink       server;
    linkPrimary   primary[NUM_GPIB_ADDRESSES];
    asynUser      *pasynUser;
    unsigned char recoverWithIFC;/*fire out IFC pulse on timeout (read/write)*/
    unsigned char lockDevices;/*lock devices when creating link*/
    asynInterface option;
    epicsEventId  srqThreadDone;
    int           srqBindSock; /*socket for bind*/
    osiSockAddr   vxiServerAddr; /*addess of vxi11 server*/
    char          *srqThreadName;
    epicsInterruptibleSyscallContext *srqInterrupt;
    int           srqEnabled;
}vxiPort;

/* Local routines */
static char *vxiError(Device_ErrorCode error);
static unsigned long getIoTimeout(asynUser *pasynUser,vxiPort *ppvxiPort);
static BOOL vxiIsPortConnected(vxiPort * pvxiPort,asynUser *pasynUser);
static void vxiDisconnectException(vxiPort *pvxiPort,int addr);
static BOOL vxiCreateDeviceLink(vxiPort * pvxiPort,
    char *devName,Device_Link *pDevice_Link);
static BOOL vxiCreateDevLink(vxiPort * pvxiPort,int addr,Device_Link *plid);
static devLink *vxiGetDevLink(vxiPort * pvxiPort, asynUser *pasynUser,int addr);
static BOOL vxiDestroyDevLink(vxiPort * pvxiPort, Device_Link devLink);
static int vxiWriteAddressed(vxiPort * pvxiPort,asynUser *pasynUser,
    Device_Link lid,char *buffer,int length,double timeout);
static int vxiWriteCmd(vxiPort * pvxiPort,asynUser *pasynUser,
    char *buffer, int length);
static enum clnt_stat clientCall(vxiPort * pvxiPort,
    u_long req,xdrproc_t proc1, caddr_t addr1,xdrproc_t proc2, caddr_t addr2);
static enum clnt_stat clientIoCall(vxiPort * pvxiPort,asynUser *pasynUser,
    u_long req,xdrproc_t proc1, caddr_t addr1,xdrproc_t proc2, caddr_t addr2);
static asynStatus vxiBusStatus(vxiPort * pvxiPort, int request,
    double timeout,int *status);
static void vxiCreateIrqChannel(vxiPort *pvxiPort,asynUser *pasynUser);
static void vxiDestroyIrqChannel(vxiPort *pvxiPort);
static void vxiSrqThread(void *pvxiPort);
static asynStatus vxiConnectPort(vxiPort *pvxiPort,asynUser *pasynUser);
static asynStatus vxiDisconnectPort(vxiPort *pvxiPort);

/* asynGpibPort methods */
static void vxiReport(void *drvPvt,FILE *fd,int details);
static asynStatus vxiConnect(void *drvPvt,asynUser *pasynUser);
static asynStatus vxiDisconnect(void *drvPvt,asynUser *pasynUser);
static asynStatus vxiRead(void *drvPvt,asynUser *pasynUser,
    char *data,int maxchars,int *nbytesTransfered,int *eomReason);
static asynStatus vxiWrite(void *drvPvt,asynUser *pasynUser,
    const char *data,int numchars,int *nbytesTransfered);
static asynStatus vxiFlush(void *drvPvt,asynUser *pasynUser);
static asynStatus vxiSetEos(void *drvPvt,asynUser *pasynUser,
    const char *eos,int eoslen);
static asynStatus vxiGetEos(void *drvPvt,asynUser *pasynUser,
    char *eos, int eossize, int *eoslen);
static asynStatus vxiAddressedCmd(void *drvPvt,asynUser *pasynUser,
    const char *data, int length);
static asynStatus vxiUniversalCmd(void *drvPvt, asynUser *pasynUser, int cmd);
static asynStatus vxiIfc(void *drvPvt, asynUser *pasynUser);
static asynStatus vxiRen(void *drvPvt,asynUser *pasynUser, int onOff);
static asynStatus vxiSrqStatus(void *drvPvt,int *srqStatus);
static asynStatus vxiSrqEnable(void *drvPvt, int onOff);
static asynStatus vxiSerialPollBegin(void *drvPvt);
static asynStatus vxiSerialPoll(void *drvPvt, int addr,
    double timeout,int *statusByte);
static asynStatus vxiSerialPollEnd(void *drvPvt);

static asynGpibPort vxi11 = {
    vxiReport,
    vxiConnect,
    vxiDisconnect,
    vxiRead,
    vxiWrite,
    vxiFlush,
    vxiSetEos,
    vxiGetEos,
    vxiAddressedCmd,
    vxiUniversalCmd,
    vxiIfc,
    vxiRen,
    vxiSrqStatus,
    vxiSrqEnable,
    vxiSerialPollBegin,
    vxiSerialPoll,
    vxiSerialPollEnd
};

static asynStatus vxiSetPortOption(void *drvPvt,
    asynUser *pasynUser,const char *key, const char *val);
static asynStatus vxiGetPortOption(void *drvPvt,
    asynUser *pasynUser, const char *key, char *val, int valSize);
static const struct asynOption vxiOption = {
    vxiSetPortOption,
    vxiGetPortOption
};

static char *vxiError(Device_ErrorCode error)
{
    switch (error) {
    case VXI_OK:        return ("VXI: no error");
    case VXI_SYNERR:    return ("VXI: syntax error");
    case VXI_NOACCESS:  return ("VXI: device not accessible");
    case VXI_INVLINK:   return ("VXI: invalid link identifier");
    case VXI_PARAMERR:  return ("VXI: parameter error");
    case VXI_NOCHAN:    return ("VXI: channel not established");
    case VXI_NOTSUPP:   return ("VXI: operation not supported");
    case VXI_NORES:     return ("VXI: out of resources");
    case VXI_DEVLOCK:   return ("VXI: device locked by another link");
    case VXI_NOLOCK:    return ("VXI: no lock held by this link");
    case VXI_IOTIMEOUT: return ("VXI: I/O timeout");
    case VXI_IOERR:     return ("VXI: I/O error");
    case VXI_INVADDR:   return ("VXI: invalid address");
    case VXI_ABORT:     return ("VXI: abort");
    case VXI_CHANEXIST: return ("VXI: channel already established");
    default:
        printf("vxiError error = %d\n", (int)error);
        return ("VXI: unknown error");
    }
}

static unsigned long getIoTimeout(asynUser *pasynUser,vxiPort *ppvxiPort)
{
    double timeout = pasynUser->timeout;

    if(timeout<0.0) return ULONG_MAX;
    if(timeout*1e3 >(double)ULONG_MAX) return ULONG_MAX;
    return (unsigned long)(timeout*1e3);
}

static BOOL vxiIsPortConnected(vxiPort * pvxiPort,asynUser *pasynUser)
{
    if(!pvxiPort) {
        if(pasynUser) {
            asynPrint(pasynUser,ASYN_TRACE_ERROR,
                "vxi11 pvxiPort is null. WHY?\n");
        } else {
            printf("vxi11 pvxiPort is null. WHY?\n");
        }
        return FALSE;
    }
    if(pvxiPort->server.connected) return TRUE;
    if(pasynUser) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "%s port not connected\n",pvxiPort->portName);
    } else {
        printf("%s port not connected\n",pvxiPort->portName);
    }
    return FALSE;
}

static void vxiDisconnectException(vxiPort *pvxiPort,int addr)
{
    asynUser   *pasynUser = pvxiPort->pasynUser;
    asynStatus status;

    status = pasynManager->disconnect(pasynUser);
    assert(status==asynSuccess);
    status = pasynManager->connectDevice(pasynUser,pvxiPort->portName,addr);
    assert(status==asynSuccess);
    status = pasynManager->exceptionDisconnect(pasynUser);
    if(status!=asynSuccess) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "%s adr %d vxiDisconnectException exceptionDisconnect failed %s\n",
            pvxiPort->portName,addr,pasynUser->errorMessage);
    }
    status = pasynManager->disconnect(pasynUser);
    assert(status==asynSuccess);
    status = pasynManager->connectDevice(pasynUser,pvxiPort->portName,-1);
    assert(status==asynSuccess);
}

static BOOL vxiCreateDeviceLink(vxiPort * pvxiPort,
    char *devName,Device_Link *pDevice_Link)
{
    enum clnt_stat   clntStat;
    Create_LinkParms crLinkP;
    Create_LinkResp  crLinkR;
    BOOL             rtnVal = FALSE;
    asynUser         *pasynUser = pvxiPort->pasynUser;

    crLinkP.clientId = (long) pvxiPort->rpcClient;
    crLinkP.lockDevice = (pvxiPort->lockDevices != 0); 
    crLinkP.lock_timeout = 0;/* if device is locked, forget it */
    crLinkP.device = devName;
    /* initialize crLinkR */
    memset((char *) &crLinkR, 0, sizeof(Create_LinkResp));
    /* RPC call */
    clntStat = clnt_call(pvxiPort->rpcClient, create_link,
        (xdrproc_t)xdr_Create_LinkParms,(caddr_t)&crLinkP,
        (xdrproc_t)xdr_Create_LinkResp, (caddr_t)&crLinkR,
        pvxiPort->vxiRpcTimeout);
    if(clntStat != RPC_SUCCESS) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "%s vxiCreateDeviceLink RPC error %s\n",
            devName,clnt_sperror(pvxiPort->rpcClient,""));
    } else if(crLinkR.error != VXI_OK) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "%s vxiCreateDeviceLink error %s\n",
            devName,vxiError(crLinkR.error));
    } else {
        *pDevice_Link = crLinkR.lid;
        rtnVal = TRUE;
        if(pvxiPort->maxRecvSize==0) {
            pvxiPort->maxRecvSize = crLinkR.maxRecvSize;
        } else if(pvxiPort->maxRecvSize!=crLinkR.maxRecvSize) {
            asynPrint(pasynUser,ASYN_TRACE_ERROR,
                "%s vxiCreateDeviceLink maxRecvSize changed from %lu to %lu\n",
                            devName,pvxiPort->maxRecvSize,crLinkR.maxRecvSize);
        }
        if(pvxiPort->abortPort==0) {
            pvxiPort->abortPort = crLinkR.abortPort;
        } else if(pvxiPort->abortPort!=crLinkR.abortPort) {
            asynPrint(pasynUser,ASYN_TRACE_ERROR,
                "%s vxiCreateDeviceLink abort channel TCP port changed from %u to %u\n",
                                devName,pvxiPort->abortPort,crLinkR.abortPort);
        }
    }
    xdr_free((const xdrproc_t) xdr_Create_LinkResp, (char *) &crLinkR);
    return rtnVal;
}

static BOOL vxiCreateDevLink(vxiPort * pvxiPort,int addr,Device_Link *plid)
{
    int         primary,secondary;
    char        devName[40];

    if(addr<100) {
        primary = addr; secondary = 0;
    } else {
        primary = addr / 100;
        secondary = addr % 100;
    }
    assert(primary<NUM_GPIB_ADDRESSES && secondary<NUM_GPIB_ADDRESSES);
    if(addr<100) {
        sprintf(devName, "%s,%d", pvxiPort->vxiName, primary);
    } else {
        sprintf(devName, "%s,%d,%d", pvxiPort->vxiName, primary, secondary);
    }
    return vxiCreateDeviceLink(pvxiPort,devName,plid);
}

static devLink *vxiGetDevLink(vxiPort *pvxiPort, asynUser *pasynUser,int addr)
{
    int primary,secondary;

    if(!pvxiPort) {
        if(pasynUser) {
            asynPrint(pasynUser,ASYN_TRACE_ERROR,
                "vxi11 pvxiPort is null. WHY?\n");
        } else {
            printf("vxi11 pvxiPort is null. WHY?\n");
        }
        return 0;
    }
    if(pvxiPort->isSingleLink || addr<0) return &pvxiPort->server;
    if(addr<100) {
        primary = addr; secondary = 0;
    } else {
        primary = addr / 100;
        secondary = addr % 100;
    }
    if(primary>=NUM_GPIB_ADDRESSES || secondary>=NUM_GPIB_ADDRESSES) {
        if(pasynUser) {
            asynPrint(pasynUser,ASYN_TRACE_ERROR,
                "%s addr %d is illegal\n",pvxiPort->portName,addr);
        } else {
            printf("%s addr %d is illegal\n",pvxiPort->portName,addr);
        }
        return 0;
    }
    if(addr<100) {
        return &pvxiPort->primary[addr].primary;
    } else {
        return &pvxiPort->primary[primary].secondary[secondary];
    }
}

static BOOL vxiDestroyDevLink(vxiPort * pvxiPort, Device_Link devLink)
{
    enum clnt_stat clntStat;
    Device_Error   devErr;
    asynUser       *pasynUser = pvxiPort->pasynUser;
    int            status = TRUE;

    clntStat = clnt_call(pvxiPort->rpcClient, destroy_link,
        (xdrproc_t) xdr_Device_Link,(caddr_t) &devLink,
        (xdrproc_t) xdr_Device_Error, (caddr_t) &devErr,
        pvxiPort->vxiRpcTimeout);
    if(clntStat != RPC_SUCCESS) {
        status = FALSE;
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "%s vxiDestroyDevLink RPC error %s\n",
             pvxiPort->portName,clnt_sperror(pvxiPort->rpcClient,""));
    } else if(devErr.error != VXI_OK) {
        status = FALSE;
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "%s vxiDestroyDevLink error %s\n",
            pvxiPort->portName,vxiError(devErr.error));
    }
    xdr_free((const xdrproc_t) xdr_Device_Error, (char *) &devErr);
    return status;
}

/*write with ATN true */
static int vxiWriteAddressed(vxiPort *pvxiPort,asynUser *pasynUser,
    Device_Link lid, char *buffer, int length, double timeout)
{
    int status = 0;
    enum clnt_stat clntStat;
    Device_DocmdParms devDocmdP;
    Device_DocmdResp devDocmdR;

    assert(pvxiPort);
    assert(buffer);
    devDocmdP.lid = lid;
    devDocmdP.flags = 0;
    devDocmdP.io_timeout = (u_long)(1000.0*timeout);
    devDocmdP.lock_timeout = 0;
    devDocmdP.network_order = NETWORK_ORDER;
    /* This command sets ATN to TRUE before sending */
    devDocmdP.cmd = VXI_CMD_SEND;
    devDocmdP.datasize = 1;
    devDocmdP.data_in.data_in_len = length;
    devDocmdP.data_in.data_in_val = buffer;
    /* initialize devDocmdR */
    memset((char *) &devDocmdR, 0, sizeof(Device_DocmdResp));
    /* RPC call */
    clntStat = clientCall(pvxiPort, device_docmd,
        (const xdrproc_t) xdr_Device_DocmdParms,(void *) &devDocmdP,
        (const xdrproc_t) xdr_Device_DocmdResp,(void *) &devDocmdR);
    if(clntStat != RPC_SUCCESS) {
        printf("%s vxiWriteAddressed %s RPC error %s\n",
            pvxiPort->portName,buffer,clnt_sperror(pvxiPort->rpcClient,""));
        status = -1;
    } else if(devDocmdR.error != VXI_OK) {
        if(devDocmdR.error != VXI_IOTIMEOUT) {
            printf("%s vxiWriteAddressed %s error %s\n",
                pvxiPort->portName,buffer,vxiError(devDocmdR.error));
        }
        status = -1;
    } else {
        status = devDocmdR.data_out.data_out_len;
    }
    xdr_free((const xdrproc_t) xdr_Device_DocmdResp, (char *) &devDocmdR);
    return status;
}

/******************************************************************************
 * Output a command string to the GPIB bus.
 * The string is sent with the ATN line held TRUE (low).
 ******************************************************************************/
static int vxiWriteCmd(vxiPort * pvxiPort,asynUser *pasynUser,
    char *buffer, int length)
{
    long status;
    devLink *pdevLink = vxiGetDevLink(pvxiPort,pasynUser,-1);

    if(!pdevLink) return asynError;
    if(!vxiIsPortConnected(pvxiPort,pasynUser)) return asynError;
    if(!pdevLink->connected) {
        asynPrint(pasynUser, ASYN_TRACE_ERROR,
           "%s vxiIfc port not connected\n",pvxiPort->portName);
        return asynError;
    }
    status = vxiWriteAddressed(pvxiPort,pasynUser,
        pdevLink->lid,buffer,length,pvxiPort->defTimeout);
    return status;
}

/******************************************************************************
 * Check the bus status. Parameter <request> can be a number from 1 to 8 to
 * indicate the information requested (see VXI_BSTAT_XXXX in drvLanGpib.h)
 * or it can be 0 meaning all (exept the bus address) which will then be
 * combined into a bitfield according to the bit numbers+1 (1 corresponds to
 * bit 0, etc.).
 ******************************************************************************/
static asynStatus vxiBusStatus(vxiPort * pvxiPort, int request,
    double timeout,int *busStatus)
{
    int status = 0, start, stop;
    devLink *pdevLink = vxiGetDevLink(pvxiPort,0,-1);
    enum clnt_stat    clntStat;
    Device_DocmdParms devDocmdP;
    Device_DocmdResp  devDocmdR;
    unsigned short    data; /* what to ask */

    if(!pdevLink) return asynError;
    if(!vxiIsPortConnected(pvxiPort,0)) return asynError;
    if(!pdevLink->connected) {
        printf("%s vxiBusStatus port not connected\n",pvxiPort->portName);
        return asynError;
    }
    assert(request >= 0 && request <= VXI_BSTAT_BUS_ADDRESS);
    devDocmdP.lid = pdevLink->lid;
    devDocmdP.flags = 0; /* no timeout on a locked gateway */
    devDocmdP.io_timeout = (u_long)(1000.0*timeout);
    devDocmdP.lock_timeout = 0;
    devDocmdP.network_order = NETWORK_ORDER;
    devDocmdP.cmd = VXI_CMD_STAT;
    devDocmdP.datasize = 2;
    devDocmdP.data_in.data_in_len = 2;
    if(!request) {
        start = VXI_BSTAT_REN;
        stop = VXI_BSTAT_LISTENER;
    } else {
        start = stop = request;
    }
    for(data = start; data <= stop; data++) {
        unsigned short ndata = htons(data);
        unsigned short result;
        devDocmdP.data_in.data_in_val = (char *) &ndata;
        /* initialize devDocmdR */
        memset((char *) &devDocmdR, 0, sizeof(Device_DocmdResp));
        /* RPC call */
        clntStat = clientCall(pvxiPort, device_docmd,
            (const xdrproc_t) xdr_Device_DocmdParms,(void *) &devDocmdP,
            (const xdrproc_t) xdr_Device_DocmdResp, (void *) &devDocmdR);
        if(clntStat != RPC_SUCCESS) {
            printf("%s vxiBusStatus RPC error %s\n",
                pvxiPort->portName, clnt_sperror(pvxiPort->rpcClient,""));
            xdr_free((const xdrproc_t)xdr_Device_DocmdResp,(char *) &devDocmdR);
            return (clntStat==RPC_TIMEDOUT) ? asynTimeout : asynError;
        }
        if(devDocmdR.error != VXI_OK) {
            if(devDocmdR.error != VXI_IOTIMEOUT) {
                printf("%s vxiBusStatus error %s\n",
                    pvxiPort->portName, vxiError(devDocmdR.error));
            }
            xdr_free((const xdrproc_t)xdr_Device_DocmdResp,(char *)&devDocmdR);
            return (devDocmdR.error==VXI_IOTIMEOUT) ? asynTimeout : asynError;
        }
        result = ntohs(*(unsigned short *) devDocmdR.data_out.data_out_val);
        if(request) {
            status = result;
        } else if(result) {
            status |= (1<<data); /* bit numbers start at 0 */
        }
        xdr_free((const xdrproc_t) xdr_Device_DocmdResp,(char *) &devDocmdR);
    }
    *busStatus = status;
    return asynSuccess;
}

static enum clnt_stat clientCall(vxiPort * pvxiPort,
    u_long req,xdrproc_t proc1, caddr_t addr1,xdrproc_t proc2, caddr_t addr2)
{
    enum clnt_stat stat;
    asynUser *pasynUser = pvxiPort->pasynUser;

    stat = clnt_call(pvxiPort->rpcClient,
        req, proc1, addr1, proc2, addr2, pvxiPort->vxiRpcTimeout);
    if(stat!=RPC_SUCCESS) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "%s vxi11 clientCall errno %s clnt_stat %d\n",
            pvxiPort->portName,strerror(errno),stat);
        if(stat!=RPC_TIMEDOUT) vxiDisconnectPort(pvxiPort);
    }
    return stat;
}

static enum clnt_stat clientIoCall(vxiPort * pvxiPort,asynUser *pasynUser,
    u_long req,xdrproc_t proc1, caddr_t addr1,xdrproc_t proc2, caddr_t addr2)
{
    enum clnt_stat stat;
    struct timeval rpcTimeout;
    double timeout = pasynUser->timeout;

    rpcTimeout.tv_usec = 0;
    if(timeout<0.0) {
        rpcTimeout.tv_sec = ULONG_MAX;
    } else if((timeout + 1.0)>((double)ULONG_MAX)) {
        rpcTimeout.tv_sec = ULONG_MAX;
    } else {
        rpcTimeout.tv_sec = (unsigned long)(timeout+1.0);
    }
    while(TRUE) {
        stat = clnt_call(pvxiPort->rpcClient,
            req, proc1, addr1, proc2, addr2, rpcTimeout);
        if(timeout>=0.0 || stat!=RPC_TIMEDOUT) break;
    }
    if(stat!=RPC_SUCCESS) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "%s vxi11 clientIoCall errno %s clnt_stat %d\n",
            pvxiPort->portName,strerror(errno),stat);
        if(stat!=RPC_TIMEDOUT) vxiDisconnectPort(pvxiPort);
    }
    return stat;
}

/*
   In order to create_intr_chan the inet address and port for srqBindSock
   is required.
   Until vxiSrqThread has accepted a connection from the vxiii server
   the local inet address is not known if multiple ethernet ports exist.
   Thus the following code creats a UDP connection to the portmapper
   on the vxi11 server just to determine the local net address for
   connections to the server.

   the srq connection is done as follows:

   bind is only done to the network interface connected to the vxi server
   accept (in vxiSrqThread) is only accepted from the vxi server address
*/
static void vxiCreateIrqChannel(vxiPort *pvxiPort,asynUser *pasynUser)
{
    enum clnt_stat     clntStat;
    Device_Error       devErr;
    Device_RemoteFunc  devRemF;
    osiSockAddr        tempAddr;
    int                tempSock;
    osiSockAddr        srqBindAddr;
    osiSocklen_t       addrlen;

    /*Must find the local address for srqBindAddr */
    /*The following connects to server portmapper to find local address*/
    addrlen = sizeof tempAddr;
    memset((void *)&tempAddr, 0, addrlen);
    tempAddr.ia.sin_family = AF_INET;
    tempAddr.ia.sin_port = htons(111); /*111 is port ob portmapper*/
    if (hostToIPAddr(pvxiPort->hostName, &tempAddr.ia.sin_addr) < 0) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "%s vxiCreateIrqChannel hostToIPAddr failed for %s\n",
            pvxiPort->portName, pvxiPort->hostName);
        return ;
    }
    tempSock = epicsSocketCreate(PF_INET, SOCK_DGRAM, 0);
    if (tempSock < 0) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "%s vxiCreateIrqChannel can't create socket\n",
            pvxiPort->portName);
        return ;
    }
    addrlen = sizeof tempAddr;
    if((connect(tempSock,(struct sockaddr *)&tempAddr,addrlen)<0)) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "%s vxiCreateIrqChannel connect failed %s\n",
             pvxiPort->portName, strerror(errno));
        return ;
    }
    addrlen = sizeof tempAddr;
    memset((void *)&tempAddr, 0, addrlen);
    if (getsockname(tempSock, &tempAddr.sa, &addrlen)) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "%s vxiCreateIrqChannel getsockname failed %s\n",
            pvxiPort->portName,strerror(errno));
        return;
    }
    /*we have the local address*/
    srqBindAddr.ia.sin_addr.s_addr = tempAddr.ia.sin_addr.s_addr;
    /*Get address of vxi11 server*/
    addrlen = sizeof tempAddr;
    memset((void *)&tempAddr, 0, addrlen);
    if (getpeername(tempSock, &tempAddr.sa, &addrlen)) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "%s vxiCreateIrqChannel getsockname failed %s\n",
            pvxiPort->portName,strerror(errno));
        return;
    }
    pvxiPort->vxiServerAddr.ia.sin_addr.s_addr = tempAddr.ia.sin_addr.s_addr;
    close(tempSock);

    /* bind for receiving srq messages*/
    pvxiPort->srqBindSock = epicsSocketCreate (PF_INET, SOCK_STREAM, 0);
    if (pvxiPort->srqBindSock < 0) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "%s vxiCreateIrqChannel  can't create socket: %s\n",
            pvxiPort->portName, strerror(errno));
        return;
    }
    addrlen = sizeof tempAddr;
    memset((void *)&tempAddr, 0, addrlen);
    tempAddr.ia.sin_family = AF_INET;
    tempAddr.ia.sin_port = htons (0);
    tempAddr.ia.sin_addr.s_addr = srqBindAddr.ia.sin_addr.s_addr;
    if (bind (pvxiPort->srqBindSock, &tempAddr.sa, sizeof tempAddr.ia) < 0) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "%s vxiCreateIrqChannel bind failed %s\n",
               pvxiPort->portName,strerror (errno));
        close(pvxiPort->srqBindSock);
        return;
    }
    addrlen = sizeof tempAddr;
    getsockname(pvxiPort->srqBindSock, &tempAddr.sa, &addrlen); 
    /*Now we have srqBindAddr port*/
    srqBindAddr.ia.sin_port = tempAddr.ia.sin_port;
    if (listen (pvxiPort->srqBindSock, 2) < 0) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "%s vxiCreateIrqChannel listen failed %s\n",
               pvxiPort->srqThreadName,strerror (errno));
        close(pvxiPort->srqBindSock);
        return;
    }

    pvxiPort->srqThreadDone = epicsEventMustCreate(epicsEventEmpty);
    pvxiPort->srqInterrupt = epicsInterruptibleSyscallCreate();
    if(pvxiPort->srqInterrupt == NULL) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "%s vxiCreateIrqChannel epicsInterruptibleSyscallCreate failed.\n",
            pvxiPort->portName);
        return;
    }
    epicsThreadCreate(pvxiPort->srqThreadName, 46,
          epicsThreadGetStackSize(epicsThreadStackMedium),
          vxiSrqThread,pvxiPort);
    /* create the interrupt channel */
    devRemF.hostAddr = ntohl(srqBindAddr.ia.sin_addr.s_addr);
    devRemF.hostPort = ntohs(srqBindAddr.ia.sin_port);
    devRemF.progNum = DEVICE_INTR;
    devRemF.progVers = DEVICE_INTR_VERSION;
    devRemF.progFamily = DEVICE_TCP;
    memset((char *) &devErr, 0, sizeof(Device_Error));
    clntStat = clientCall(pvxiPort,  create_intr_chan,
        (const xdrproc_t) xdr_Device_RemoteFunc, (void *) &devRemF,
        (const xdrproc_t) xdr_Device_Error, (void *) &devErr);
    if(clntStat != RPC_SUCCESS) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "%s vxiCreateIrqChannel (create_intr_chan)%s\n",
            pvxiPort->portName,clnt_sperror(pvxiPort->rpcClient,""));
        xdr_free((const xdrproc_t) xdr_Device_Error, (char *) &devErr);
    } else if(devErr.error != VXI_OK) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "%s vxiCreateIrqChannel %s (create_intr_chan)\n",
            pvxiPort->portName, vxiError(devErr.error));
        xdr_free((const xdrproc_t) xdr_Device_Error, (char *) &devErr);
    } else {
        vxiSrqEnable(pvxiPort,1);
        xdr_free((const xdrproc_t) xdr_Device_Error, (char *) &devErr);
        return;
    }
    asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "%s Warning -- SRQ not operational.\n",pvxiPort->portName);
}

static void vxiDestroyIrqChannel(vxiPort *pvxiPort)
{
    Device_Error devErr;
    int          dummy;
    asynUser     *pasynUser = pvxiPort->pasynUser;
    enum clnt_stat clntStat;
    int          ntrys;

    if(!pvxiPort->srqInterrupt) return;
    clntStat = clnt_call(pvxiPort->rpcClient, destroy_intr_chan,
        (xdrproc_t) xdr_void, (caddr_t) &dummy,
        (xdrproc_t) xdr_Device_Error,(caddr_t) &devErr,
        pvxiPort->vxiRpcTimeout);
    /* report errors only if debug flag set. If any errors, the next time this
     * link is initialized, make sure a new SRQ thread is started.  See comments
     * in vxiConnectPort. */
    if(clntStat != RPC_SUCCESS) {
        if(pasynUser) {
            asynPrint(pasynUser,ASYN_TRACE_ERROR,"%s vxiDisconnectPort %s\n",
                pvxiPort->portName,clnt_sperror(pvxiPort->rpcClient,""));
        } else {
            printf("%s vxiCreateIrqChannel vxiDisconnectPort RPC error %s\n",
                pvxiPort->portName,clnt_sperror(pvxiPort->rpcClient,""));
        }
    } else if(devErr.error != VXI_OK) {
        if(pasynUser) {
            asynPrint(pasynUser,ASYN_TRACE_ERROR,"%s vxiDisconnectPort %s\n",
                pvxiPort->portName,vxiError(devErr.error));
        } else {
            printf("%s vxiDisconnectPort %s\n",
               pvxiPort->portName,vxiError(devErr.error));
        }
    }
    xdr_free((const xdrproc_t) xdr_Device_Error, (char *) &devErr);
    for (ntrys = 0 ; ; ntrys++) {
        epicsEventWaitStatus status;
        status = epicsEventWaitWithTimeout(pvxiPort->srqThreadDone,2.0);
        if(status == epicsEventWaitOK) {
            epicsInterruptibleSyscallDelete(pvxiPort->srqInterrupt);
            break;
        }
        if(ntrys == 10) {
            asynPrint(pasynUser,ASYN_TRACE_ERROR,
                        "WARNING -- %s SRQ thread will not terminate!\n",
                         pvxiPort->portName);
            break;
        }
        asynPrint(pasynUser,ASYN_TRACE_FLOW,"%s: unwedge SRQ thread.\n",
                                                       pvxiPort->portName);
        epicsInterruptibleSyscallInterrupt(pvxiPort->srqInterrupt);
    }
    pvxiPort->srqInterrupt = NULL;
    epicsEventDestroy(pvxiPort->srqThreadDone);
    pvxiPort->srqThreadDone = 0;
}

static void vxiSrqThread(void *arg)
{
    vxiPort *pvxiPort = arg;
    asynUser *pasynUser = pvxiPort->pasynUser;
    epicsThreadId myTid;
    int srqSock;
    char buf[512];
    int i;

    myTid = epicsThreadGetIdSelf();
    taskwdInsert(myTid, NULL, NULL);
    while(1) {
        osiSockAddr farAddr;
        osiSocklen_t addrlen;

        epicsInterruptibleSyscallArm(
            pvxiPort->srqInterrupt, pvxiPort->srqBindSock, myTid);
        addrlen = sizeof farAddr;
        srqSock = epicsSocketAccept(pvxiPort->srqBindSock,&farAddr.sa,&addrlen);
        if(epicsInterruptibleSyscallWasInterrupted(pvxiPort->srqInterrupt)) {
            if(!epicsInterruptibleSyscallWasClosed(pvxiPort->srqInterrupt))
                close(pvxiPort->srqBindSock);
            asynPrint(pasynUser,ASYN_TRACE_FLOW,"%s vxiSrqThread terminating\n",
                pvxiPort->portName);
            taskwdRemove(myTid);
            epicsEventSignal(pvxiPort->srqThreadDone);
            return;
        }
        if(srqSock < 0) break;
        if(pvxiPort->vxiServerAddr.ia.sin_addr.s_addr==farAddr.ia.sin_addr.s_addr)
            break;
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
             "%s vxiSrqThread accept but not from vxiServer\n",
              pvxiPort->portName);
        close(srqSock);
    }
    close(pvxiPort->srqBindSock);
    if(srqSock < 0) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,"%s can't accept connection: %s\n",
                pvxiPort->srqThreadName,strerror(errno));
        taskwdRemove(myTid);
        epicsEventSignal(pvxiPort->srqThreadDone);
        return;
    }
    epicsInterruptibleSyscallArm(pvxiPort->srqInterrupt, srqSock, myTid);
    for(;;) {
        if(epicsInterruptibleSyscallWasInterrupted(pvxiPort->srqInterrupt))
            break;
        i = read(srqSock, buf, sizeof buf);
        if(epicsInterruptibleSyscallWasInterrupted(pvxiPort->srqInterrupt))
            break;
        if(i < 0) {
            asynPrint(pasynUser,ASYN_TRACE_ERROR,"%s read error: %s\n",
                pvxiPort->srqThreadName,strerror(errno));
            break;
        }
        else if (i == 0) {
            asynPrint(pasynUser,ASYN_TRACE_ERROR,"%s read EOF\n",
                pvxiPort->srqThreadName);
            break;
        }
        asynPrint(pasynUser,ASYN_TRACE_FLOW,"%s SRQ\n",pvxiPort->srqThreadName);
        if(pvxiPort->isSingleLink) pvxiPort->singleLinkInterrupt = TRUE;
        pasynGpib->srqHappened(pvxiPort->asynGpibPvt);
    }
    if(!epicsInterruptibleSyscallWasClosed(pvxiPort->srqInterrupt))
        close(srqSock);
    asynPrint(pasynUser,ASYN_TRACE_FLOW,
        "%s vxiSrqThread terminating\n",pvxiPort->srqThreadName);
    taskwdRemove(myTid);
    epicsEventSignal(pvxiPort->srqThreadDone);
}

static asynStatus vxiConnectPort(vxiPort *pvxiPort,asynUser *pasynUser)
{
    int         isController;
    Device_Link link;
    int         sock = -1;
    asynStatus  status;
    struct sockaddr_in vxiServer;

    /* Previously this pasynUser was created and connected to the port in vxi11Configure 
     * after calling pasynGpib->registerPort
     * But in asyn R4-12 and later a call to pasynCommon->connect (which calls this function) 
     * can happen almost immediately when pasynGpib->registerPort is called, so we move the code here. */
    if (!pvxiPort->pasynUser) {
        pvxiPort->pasynUser = pasynManager->createAsynUser(0,0);
        pvxiPort->pasynUser->timeout = pvxiPort->defTimeout;
        status = pasynManager->connectDevice(
            pvxiPort->pasynUser,pvxiPort->portName,-1);
        if (status!=asynSuccess) 
            asynPrint(pasynUser, ASYN_TRACE_ERROR,
            "vxiConnectPort: connectDevice failed %s\n",pvxiPort->pasynUser->errorMessage);
    }
    if(pvxiPort->server.connected) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "%s vxiConnectPort but already connected\n",pvxiPort->portName);
        return asynError;
    }
    asynPrint(pasynUser,ASYN_TRACE_FLOW,
        "%s vxiConnectPort\n",pvxiPort->portName);
    if(!pvxiPort->rpcTaskInitCalled) {
        if(rpcTaskInit() == -1) {
            asynPrint(pasynUser,ASYN_TRACE_ERROR,
                "%s Can't init RPC\n",pvxiPort->portName);
            return asynError;
        }
        pvxiPort->rpcTaskInitCalled = TRUE;
    }

    /*
     * We could simplify this by calling clnt_create rather than clnttcp_create
     * but clnt_create often makes use of the non-thread-safe gethostbyname()
     * routine.
     */
    memset((void *)&vxiServer, 0, sizeof vxiServer);
    vxiServer.sin_family = AF_INET;
    vxiServer.sin_port = htons(0);
    if (hostToIPAddr(pvxiPort->hostName, &vxiServer.sin_addr) < 0) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,"%s can't get IP address of %s\n",
                                    pvxiPort->portName, pvxiPort->hostName);
        return asynError;
    }
    pvxiPort->rpcClient = clnttcp_create(&vxiServer, 
                                DEVICE_CORE, DEVICE_CORE_VERSION, &sock, 0, 0);
    if(!pvxiPort->rpcClient) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,"%s vxiConnectPort error %s\n",
            pvxiPort->portName, clnt_spcreateerror(pvxiPort->hostName));
        return asynError;
    }
    /* now establish a link to the gateway (for docmds etc.) */
    pvxiPort->abortPort = 0;
    if(!vxiCreateDeviceLink(pvxiPort,pvxiPort->vxiName,&link)) return asynError;
    pvxiPort->server.lid = link;
    pvxiPort->server.connected = TRUE;
    pvxiPort->ctrlAddr = -1;
    if(pvxiPort->isGpibLink) {
        /* Ask the controller's gpib address.*/
        status = vxiBusStatus(pvxiPort,
            VXI_BSTAT_BUS_ADDRESS,pvxiPort->defTimeout,&pvxiPort->ctrlAddr);
        if(status!=asynSuccess) {
            asynPrint(pasynUser,ASYN_TRACE_ERROR,
           	    "%s vxiConnectPort cannot read bus status initialization aborted\n",
                pvxiPort->portName);
            if (pvxiPort->server.connected)
                vxiDisconnectPort(pvxiPort);
            return status;
        }
        /* initialize the vxiPort structure with the data we have got so far */
        pvxiPort->primary[pvxiPort->ctrlAddr].primary.lid = link;
        pvxiPort->primary[pvxiPort->ctrlAddr].primary.connected = TRUE;
        /* now we can use vxiBusStatus; if we are not the controller fail */
        status = vxiBusStatus(pvxiPort, VXI_BSTAT_SYSTEM_CONTROLLER,
            pvxiPort->defTimeout,&isController);
        if(status!=asynSuccess) {
            asynPrint(pasynUser,ASYN_TRACE_ERROR,
                "%s vxiConnectPort vxiBusStatus error initialization aborted\n",
                pvxiPort->portName);
            if (pvxiPort->server.connected)
                vxiDisconnectPort(pvxiPort);
            return status;
        }
        if(isController == 0) {
            status = vxiBusStatus(pvxiPort, VXI_BSTAT_CONTROLLER_IN_CHARGE,
                pvxiPort->defTimeout,&isController);
            if(status!=asynSuccess) {
                asynPrint(pasynUser,ASYN_TRACE_ERROR,
                    "%s vxiConnectPort vxiBusStatus error initialization aborted\n",
                    pvxiPort->portName);
                if (pvxiPort->server.connected)
                    vxiDisconnectPort(pvxiPort);
                return asynError;
            }
            if(isController == 0) {
                asynPrint(pasynUser,ASYN_TRACE_ERROR,
                    "%s vxiConnectPort neither system controller nor "
                    "controller in charge -- initialization aborted\n",
                    pvxiPort->portName);
                if (pvxiPort->server.connected)
                    vxiDisconnectPort(pvxiPort);
                return asynError;
            }
        }
    }
    vxiCreateIrqChannel(pvxiPort,pasynUser);
    pasynManager->exceptionConnect(pvxiPort->pasynUser);
    return asynSuccess;
}

static asynStatus vxiDisconnectPort(vxiPort *pvxiPort)
{
    int          addr,secondary;
    asynUser     *pasynUser = pvxiPort->pasynUser;

    if(!pvxiPort->server.connected) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "%s vxiDisconnectPort but not connected\n",pvxiPort->portName);
        return asynError;
    }
    if(pasynUser) asynPrint(pasynUser,ASYN_TRACE_FLOW,
        "%s vxiDisconnectPort\n",pvxiPort->portName);
    if(!pvxiPort->isSingleLink)
    for(addr = 0; addr < NUM_GPIB_ADDRESSES; addr++) {
        devLink *pdevLink;

        pdevLink = &pvxiPort->primary[addr].primary;
        if(pdevLink->connected) {
            if(addr!=pvxiPort->ctrlAddr) {
                vxiDestroyDevLink(pvxiPort, pdevLink->lid);
                vxiDisconnectException(pvxiPort,addr);
            }
            pdevLink->lid = 0;
            pdevLink->connected = FALSE;
        }
        for(secondary = 0; secondary < NUM_GPIB_ADDRESSES; secondary++) {
            pdevLink = &pvxiPort->primary[addr].secondary[secondary];
            if(pdevLink->connected) {
                vxiDestroyDevLink(pvxiPort, pdevLink->lid);
                vxiDisconnectException(pvxiPort,(addr*100 + secondary));
                pdevLink->lid = 0;
                pdevLink->connected = FALSE;
            }
        }
    }
    vxiDestroyIrqChannel(pvxiPort);
    vxiDestroyDevLink(pvxiPort, pvxiPort->server.lid);
    pvxiPort->server.connected = FALSE;
    pvxiPort->server.lid = 0;
    clnt_destroy(pvxiPort->rpcClient);
    pasynManager->exceptionDisconnect(pvxiPort->pasynUser);
    return asynSuccess;
}

static void vxiReport(void *drvPvt,FILE *fd,int details)
{
    vxiPort *pvxiPort = (vxiPort *)drvPvt;
    assert(pvxiPort);
    fprintf(fd,"    vxi11, host name: %s\n", pvxiPort->hostName);
    if(details > 1) {
        char nameBuf[60];
        if(ipAddrToHostName(&pvxiPort->inAddr, nameBuf, sizeof nameBuf) > 0)
            fprintf(fd,"    ip address:%s\n", nameBuf);
        fprintf(fd,"    vxi name:%s", pvxiPort->vxiName);
        fprintf(fd," ctrlAddr:%d",pvxiPort->ctrlAddr);
        fprintf(fd," maxRecvSize:%lu", pvxiPort->maxRecvSize);
        fprintf(fd," isSingleLink:%s isGpibLink:%s\n",
            ((pvxiPort->isSingleLink) ? "yes" : "no"),
            ((pvxiPort->isGpibLink) ? "yes" : "no"));
    }
}

static asynStatus vxiConnect(void *drvPvt,asynUser *pasynUser)
{
    vxiPort     *pvxiPort = (vxiPort *)drvPvt;
    int         addr;
    devLink     *pdevLink;
    asynStatus  status;

    status = pasynManager->getAddr(pasynUser,&addr);
    if(status!=asynSuccess) return status;
    pdevLink = vxiGetDevLink(pvxiPort,pasynUser,addr);
    if(!pdevLink) return asynError;
    asynPrint(pasynUser,ASYN_TRACE_FLOW,
        "%s addr %d vxiConnect\n",pvxiPort->portName,addr);
    if(pdevLink->connected) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
            "%s addr %d vxiConnect request but already connected",
            pvxiPort->portName,addr);
        return asynError;
    }
    if(addr!=-1 && !vxiIsPortConnected(pvxiPort,pasynUser)) {
        pasynManager->exceptionConnect(pasynUser);
        return asynSuccess;
    }
    if(addr==-1) return vxiConnectPort(pvxiPort,pasynUser);
    if(!pdevLink->connected) {
        Device_Link lid;

        if(!vxiCreateDevLink(pvxiPort,addr,&lid)) {
            asynPrint(pasynUser,ASYN_TRACE_ERROR,
                "%s vxiCreateDevLink failed for addr %d\n",
                pvxiPort->portName,addr);
            return asynError;
        }
        pdevLink->lid = lid;
        pdevLink->connected = TRUE;
    }
    pasynManager->exceptionConnect(pasynUser);
    return asynSuccess;
}

static asynStatus vxiDisconnect(void *drvPvt,asynUser *pasynUser)
{
    vxiPort *pvxiPort = (vxiPort *)drvPvt;
    int     addr;
    devLink *pdevLink;
    asynStatus status = asynSuccess;

    status = pasynManager->getAddr(pasynUser,&addr);
    if(status!=asynSuccess) return status;
    pdevLink = vxiGetDevLink(pvxiPort,pasynUser,addr);
    if(!pdevLink) return asynError;
    asynPrint(pasynUser,ASYN_TRACE_FLOW,
        "%s addr %d vxiDisconnect\n",pvxiPort->portName,addr);
    if(!pdevLink->connected) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
            "%s addr %d vxiDisconnect request but not connected",
            pvxiPort->portName,addr);
        return asynError;
    }
    if(addr==-1) return vxiDisconnectPort(pvxiPort);
    if(!vxiDestroyDevLink(pvxiPort,pdevLink->lid)) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
            "%s vxiDestroyDevLink failed for addr %d",pvxiPort->portName,addr);
        status = asynError;
    }
    pdevLink->lid = 0;
    pdevLink->connected = 0;
    pasynManager->exceptionDisconnect(pasynUser);
    return status;
}

static asynStatus vxiRead(void *drvPvt,asynUser *pasynUser,
    char *data,int maxchars,int *nbytesTransfered,int *eomReason)
{
    vxiPort *pvxiPort = (vxiPort *)drvPvt;
    int     nRead = 0, thisRead;
    int     addr;
    devLink *pdevLink;
    enum clnt_stat   clntStat;
    Device_ReadParms devReadP;
    Device_ReadResp  devReadR;
    asynStatus       status = asynSuccess;

    status = pasynManager->getAddr(pasynUser,&addr);
    if(status!=asynSuccess) return status;
    pdevLink = vxiGetDevLink(pvxiPort,pasynUser,addr);
    assert(data);
    if(!pdevLink) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
            "%s No devLink. Why?",pvxiPort->portName);
        return asynError;
    }
    if(!vxiIsPortConnected(pvxiPort,pasynUser) || !pdevLink->connected) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
            "%s port is not connected",pvxiPort->portName);
        return asynError;
    }
    devReadP.lid = pdevLink->lid;
    /* device link is created; do the read */
    do {
        thisRead = -1;
        devReadP.requestSize = maxchars;
        devReadP.io_timeout = getIoTimeout(pasynUser,pvxiPort);
        devReadP.lock_timeout = 0;
        devReadP.flags = 0;
        if(pdevLink->eos != -1) {
            devReadP.flags |= VXI_TERMCHRSET;
            devReadP.termChar = pdevLink->eos;
        }
        /* initialize devReadR */
        memset((char *) &devReadR, 0, sizeof(Device_ReadResp));
        /* RPC call */
        while(TRUE) { /*Allow for very long or infinite timeout*/
            clntStat = clientIoCall(pvxiPort, pasynUser, device_read,
                (const xdrproc_t) xdr_Device_ReadParms,(void *) &devReadP,
                (const xdrproc_t) xdr_Device_ReadResp,(void *) &devReadR);
            if(devReadP.io_timeout!=UINT_MAX
            || devReadR.error!=VXI_IOTIMEOUT
            || devReadR.data.data_len>0) break;
        }
        if(clntStat != RPC_SUCCESS) {
            epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                "%s RPC failed",pvxiPort->portName);
            status = asynError;
            break;
        } else if(devReadR.error != VXI_OK) {
            if((devReadR.error == VXI_IOTIMEOUT) && (pvxiPort->recoverWithIFC))
                vxiIfc(drvPvt, pasynUser);
            epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                "%s read request failed",pvxiPort->portName);
            status = (devReadR.error==VXI_IOTIMEOUT) ? asynTimeout : asynError;
            break;
        } 
        thisRead = devReadR.data.data_len;
        if(thisRead>0) {
            asynPrintIO(pasynUser,ASYN_TRACEIO_DRIVER,
                devReadR.data.data_val,devReadR.data.data_len,
                "%s %d vxiRead\n",pvxiPort->portName,addr);
            memcpy(data, devReadR.data.data_val, thisRead);
            nRead += thisRead;
            data += thisRead;
            maxchars -= thisRead;
        }
        xdr_free((const xdrproc_t) xdr_Device_ReadResp, (char *) &devReadR);
    } while(!devReadR.reason && thisRead>0);
    if(eomReason) {
        *eomReason = 0;
        if(devReadR.reason & VXI_REQCNT) *eomReason |= ASYN_EOM_CNT;
        if(devReadR.reason & VXI_CHR) *eomReason |= ASYN_EOM_EOS;
        if(devReadR.reason & VXI_ENDR) *eomReason |= ASYN_EOM_END;
    }
    *nbytesTransfered = nRead;
    return status;
}

static asynStatus vxiWrite(void *drvPvt,asynUser *pasynUser,
    const char *data,int numchars,int *nbytesTransfered)
{
    vxiPort *pvxiPort = (vxiPort *) drvPvt;
    int     addr;
    devLink *pdevLink;
    int     nWrite = 0, thisWrite;
    int     size;  /* devWriteR.size */
    asynStatus        status = asynSuccess;
    enum clnt_stat    clntStat;
    Device_WriteParms devWriteP;
    Device_WriteResp  devWriteR;

    status = pasynManager->getAddr(pasynUser,&addr);
    if(status!=asynSuccess) return status;
    pdevLink = vxiGetDevLink(pvxiPort,pasynUser,addr);
    assert(data);
    asynPrint(pasynUser,ASYN_TRACE_FLOW,
        "%s %d vxiWrite numchars %d\n",pvxiPort->portName,addr,numchars);
    if(!pdevLink) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
            "%s No devLink. Why?",pvxiPort->portName);
        return asynError;
    }
    if(!vxiIsPortConnected(pvxiPort,pasynUser) || !pdevLink->connected) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
            "%s port is not connected",pvxiPort->portName);
        return asynError;
    }
    devWriteP.lid = pdevLink->lid;;
    devWriteP.io_timeout = getIoTimeout(pasynUser,pvxiPort);
    devWriteP.lock_timeout = 0;
    /*write at most maxRecvSize bytes at a time */
    do {
        if(numchars<=pvxiPort->maxRecvSize) {
            devWriteP.flags = VXI_ENDW;
            thisWrite = numchars;
        } else {
            devWriteP.flags = 0;
            thisWrite = pvxiPort->maxRecvSize;
        }
        devWriteP.data.data_len = thisWrite;
        devWriteP.data.data_val = (char *)data;
        /* initialize devWriteR */
        memset((char *) &devWriteR, 0, sizeof(Device_WriteResp));
        /* RPC call */
        clntStat = clientIoCall(pvxiPort, pasynUser, device_write,
            (const xdrproc_t) xdr_Device_WriteParms,(void *) &devWriteP,
            (const xdrproc_t) xdr_Device_WriteResp,(void *) &devWriteR);
        if(clntStat != RPC_SUCCESS) {
            epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                "%s RPC failed",pvxiPort->portName);
            status = asynError;
            break;
        } else if(devWriteR.error != VXI_OK) {
            if(devWriteR.error == VXI_IOTIMEOUT && pvxiPort->recoverWithIFC) 
                vxiIfc(drvPvt, pasynUser);
            epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                "%s write request failed",pvxiPort->portName);
            status = (devWriteR.error==VXI_IOTIMEOUT) ? asynTimeout : asynError;
            break;
        } else {
            size = devWriteR.size;
            asynPrintIO(pasynUser,ASYN_TRACEIO_DRIVER,
                devWriteP.data.data_val,devWriteP.data.data_len,
                "%s %d vxiWrite\n",pvxiPort->portName,addr);
            data += size;
            numchars -= size;
            nWrite += size;
        }
        xdr_free((const xdrproc_t) xdr_Device_WriteResp, (char *) &devWriteR);
    } while(size==thisWrite && numchars>0);
    *nbytesTransfered = nWrite;
    return status;
}

static asynStatus vxiFlush(void *drvPvt,asynUser *pasynUser)
{
    /* Nothing to do */
    return asynSuccess;
}

static asynStatus vxiSetEos(void *drvPvt,asynUser *pasynUser,
    const char *eos,int eoslen)
{
    vxiPort *pvxiPort = (vxiPort *)drvPvt;
    int     addr;
    devLink *pdevLink;
    asynStatus  status;

    status = pasynManager->getAddr(pasynUser,&addr);
    if(status!=asynSuccess) return status;
    pdevLink = vxiGetDevLink(pvxiPort,pasynUser,addr);
    if(!pdevLink) return asynError;
    asynPrintIO(pasynUser, ASYN_TRACE_FLOW, eos, eoslen,
            "%s vxiSetEos %d\n",pvxiPort->portName,eoslen);
    switch (eoslen) {
    default:
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
           "%s vxiSetEos illegal eoslen %d\n",pvxiPort->portName,eoslen);
        return asynError;
    case 1:
        pdevLink->eos = (unsigned char)eos[0];
        break;
    case 0:
        pdevLink->eos = -1;
        break;
    }
    return asynSuccess;
}

static asynStatus vxiGetEos(void *drvPvt,asynUser *pasynUser,
    char *eos, int eossize, int *eoslen)
{
    vxiPort *pvxiPort = (vxiPort *)drvPvt;
    int     addr;
    devLink *pdevLink;
    asynStatus  status;

    status = pasynManager->getAddr(pasynUser,&addr);
    if(status!=asynSuccess) return status;
    pdevLink = vxiGetDevLink(pvxiPort,pasynUser,addr);
    if(!pdevLink) return asynError;
    if(eossize<1) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "%s vxiGetEos eossize %d too small\n",pvxiPort->portName,eossize);
        *eoslen = 0;
        return asynError;
    }
    if(pdevLink->eos==-1) {
        *eoslen = 0;
    } else {
        eos[0] = pdevLink->eos;
        *eoslen = 1;
    }
    asynPrintIO(pasynUser, ASYN_TRACE_FLOW, eos, *eoslen,
            "%s vxiGetEos %d\n",pvxiPort->portName, *eoslen);
    return asynSuccess;
}

static asynStatus vxiAddressedCmd(void *drvPvt,asynUser *pasynUser,
    const char *data, int length)
{
    vxiPort *pvxiPort = (vxiPort *)drvPvt;
    int      nWrite;
    int     addr;
    devLink *pdevLink;
    char    addrBuffer[2];
    int     lenCmd = 0;
    int     primary,secondary;
    asynStatus  status;

    status = pasynManager->getAddr(pasynUser,&addr);
    if(status!=asynSuccess) return status;
    if(addr<100) {
        addrBuffer[lenCmd++] = addr+LADBASE;
    } else {
        primary = addr / 100;
        secondary = addr % 100;
        addrBuffer[lenCmd++] = primary + LADBASE;
        addrBuffer[lenCmd++] = secondary + SADBASE;
    }
    assert(data);
    pdevLink = vxiGetDevLink(pvxiPort,pasynUser,addr);
    if(!pdevLink) return asynError;
    if(!vxiIsPortConnected(pvxiPort,pasynUser)) return asynError;
    if(!pdevLink->connected) return -1;
    asynPrint(pasynUser, ASYN_TRACE_FLOW,
        "%s %d vxiAddressedCmd %2.2x\n",pvxiPort->portName,addr, *data);
    asynPrintIO(pasynUser,ASYN_TRACEIO_DRIVER,
        data,length,"%s %d vxiAddressedCmd\n",pvxiPort->portName,addr);
    nWrite = vxiWriteCmd(pvxiPort,pasynUser,addrBuffer,lenCmd);
    if(nWrite!=lenCmd)asynPrint(pasynUser,ASYN_TRACE_ERROR,
        "%s addr %d vxiAddressedCmd requested %d but sent %d bytes\n",
        pvxiPort->portName,addr,length,nWrite);
    nWrite = vxiWriteCmd(pvxiPort,pasynUser,(char *)data,length);
    if(nWrite!=length) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
            "%s %d vxiAddressedCmd requested %d but sent %d bytes",
            pvxiPort->portName,addr,length,nWrite);
        status = asynError;
    }
    vxiWriteCmd(pvxiPort,pasynUser, "_?", 2); /* UNT UNL*/
    return status;
}

static asynStatus vxiUniversalCmd(void *drvPvt, asynUser *pasynUser, int cmd)
{
    vxiPort    *pvxiPort = (vxiPort *)drvPvt;
    int        nout;
    char       data[2];
    asynStatus status = asynSuccess;

    asynPrint(pasynUser, ASYN_TRACE_FLOW,
        "%s vxiUniversalCmd %2.2x\n",pvxiPort->portName,cmd);
    data[0] = cmd;
    data[1] = 0;
    nout = vxiWriteCmd(pvxiPort,pasynUser, data, 1);
    if(nout!=1) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
            "%s vxiUniversalCmd failed", pvxiPort->portName);
        status = asynError;
    }
    return status;
}

static asynStatus vxiIfc(void *drvPvt, asynUser *pasynUser)
{
    vxiPort *pvxiPort = (vxiPort *)drvPvt;
    int     status = asynSuccess;
    devLink *pdevLink = vxiGetDevLink(pvxiPort,pasynUser,-1);
    enum clnt_stat    clntStat;
    Device_DocmdParms devDocmdP;
    Device_DocmdResp  devDocmdR;

    if(!pdevLink) return asynError;
    if(!vxiIsPortConnected(pvxiPort,pasynUser)) return asynError;
    if(!pdevLink->connected) {
        asynPrint(pasynUser, ASYN_TRACE_ERROR,
           "%s vxiIfc port not connected\n",pvxiPort->portName);
        return asynError;
    }
    asynPrint(pasynUser, ASYN_TRACE_FLOW,"%s vxiIfc\n",pvxiPort->portName);
    /* initialize devDocmdP */
    devDocmdP.lid = pdevLink->lid;
    devDocmdP.flags = 0; /* no timeout on a locked gateway */
    devDocmdP.io_timeout = getIoTimeout(pasynUser,pvxiPort);
    devDocmdP.lock_timeout = 0;
    devDocmdP.network_order = NETWORK_ORDER;
    devDocmdP.cmd = VXI_CMD_IFC;
    devDocmdP.datasize = 0;
    devDocmdP.data_in.data_in_len = 0;
    devDocmdP.data_in.data_in_val = NULL;
    /* initialize devDocmdR */
    memset((char *) &devDocmdR, 0, sizeof(Device_DocmdResp));
    /* RPC call */
    clntStat = clientCall(pvxiPort, device_docmd,
        (const xdrproc_t) xdr_Device_DocmdParms,(void *) &devDocmdP,
        (const xdrproc_t) xdr_Device_DocmdResp, (void *) &devDocmdR);
    if(clntStat != RPC_SUCCESS) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,"%s vxiIfc RPC error %s\n",
            pvxiPort->portName,clnt_sperror(pvxiPort->rpcClient,""));
        status = asynError;
    } else if(devDocmdR.error != VXI_OK) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,"%s vxiIfc %s\n",
                pvxiPort->portName, vxiError(devDocmdR.error));
        status = (devDocmdR.error==VXI_IOTIMEOUT) ? asynTimeout : asynError;
    }
    xdr_free((const xdrproc_t) xdr_Device_DocmdResp, (char *) &devDocmdR);
    return status;
}

static asynStatus vxiRen(void *drvPvt,asynUser *pasynUser, int onOff)
{
    vxiPort *pvxiPort = (vxiPort *)drvPvt;
    int     status = asynSuccess;
    devLink *pdevLink = vxiGetDevLink(pvxiPort,pasynUser,-1);
    enum clnt_stat    clntStat;
    Device_DocmdParms devDocmdP;
    Device_DocmdResp  devDocmdR;
    unsigned short    data,netdata;

    if(!pdevLink) return asynError;
    if(!vxiIsPortConnected(pvxiPort,pasynUser)) return asynError;
    if(!pdevLink->connected) {
        asynPrint(pasynUser, ASYN_TRACE_ERROR,
           "%s vxiRen port not connected\n",pvxiPort->portName);
        return asynError;
    }
    asynPrint(pasynUser, ASYN_TRACE_FLOW,"%s vxiRen\n",pvxiPort->portName);
    /* initialize devDocmdP */
    devDocmdP.lid = pdevLink->lid;
    devDocmdP.flags = 0; /* no timeout on a locked gateway */
    devDocmdP.io_timeout = getIoTimeout(pasynUser,pvxiPort);
    devDocmdP.lock_timeout = 0;
    devDocmdP.network_order = NETWORK_ORDER;
    devDocmdP.cmd = VXI_CMD_REN;
    devDocmdP.datasize = 2;
    devDocmdP.data_in.data_in_len = 2;
    data = onOff ? 1 : 0;
    netdata = htons(data);
    devDocmdP.data_in.data_in_val = (char *)&netdata;
    /* initialize devDocmdR */
    memset((char *) &devDocmdR, 0, sizeof(Device_DocmdResp));
    /* RPC call */
    clntStat = clientCall(pvxiPort, device_docmd,
        (const xdrproc_t) xdr_Device_DocmdParms,(void *) &devDocmdP,
        (const xdrproc_t) xdr_Device_DocmdResp, (void *) &devDocmdR);
    if(clntStat != RPC_SUCCESS) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,"%s vxiRen RPC error %s\n",
            pvxiPort->portName,clnt_sperror(pvxiPort->rpcClient,""));
        status = asynError;
    } else if(devDocmdR.error != VXI_OK) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,"%s vxiRen %s\n", 
                pvxiPort->portName, vxiError(devDocmdR.error));
        status = (devDocmdR.error==VXI_IOTIMEOUT) ? asynTimeout : asynError;
    }
    xdr_free((const xdrproc_t) xdr_Device_DocmdResp, (char *) &devDocmdR);
    return status;
}

static asynStatus vxiSrqStatus(void *drvPvt,int *srqStatus)
{
    vxiPort    *pvxiPort = (vxiPort *)drvPvt;
    asynStatus status;

    assert(pvxiPort);
    if(pvxiPort->isSingleLink) {
        *srqStatus =  pvxiPort->singleLinkInterrupt;
        pvxiPort->singleLinkInterrupt = FALSE;
        return asynSuccess;
    }
    status = vxiBusStatus(pvxiPort, VXI_BSTAT_SRQ,
        pvxiPort->defTimeout,srqStatus);
    return status;
}

static asynStatus vxiSrqEnable(void *drvPvt, int onOff)
{
    vxiPort    *pvxiPort = (vxiPort *)drvPvt;
    asynStatus status = asynSuccess;
    devLink *pdevLink = vxiGetDevLink(pvxiPort,0,-1);
    enum clnt_stat        clntStat;
    Device_EnableSrqParms devEnSrqP;
    Device_Error          devErr;
    char                  handle[16];

    if(!pdevLink) return asynError;
    if(!vxiIsPortConnected(pvxiPort,0)) return asynError;
    if(!pdevLink->connected) {
        printf("%s vxiSrqEnable port not connected\n",pvxiPort->portName);
        return asynError;
    }
    if ((pvxiPort->srqEnabled >= 0)
     && ((onOff && pvxiPort->srqEnabled)
      || (!onOff && !pvxiPort->srqEnabled))) {
        return asynSuccess;
    }
    pvxiPort->srqEnabled = -1;
    devEnSrqP.lid = pdevLink->lid;
    if(onOff) {
        devEnSrqP.enable = TRUE;
        sprintf(handle, "%p", (void *) pvxiPort);
        devEnSrqP.handle.handle_val = handle;
        devEnSrqP.handle.handle_len = strlen(handle) + 1;/* include the '\0' */
    } else {
        devEnSrqP.enable = FALSE;
        devEnSrqP.handle.handle_val = "";
        devEnSrqP.handle.handle_len = 0;
    }
    /* initialize devErr */
    memset((char *) &devErr, 0, sizeof(Device_Error));
    /* RPC call */
    clntStat = clientCall(pvxiPort, device_enable_srq,
        (const xdrproc_t) xdr_Device_EnableSrqParms,(void *) &devEnSrqP,
        (const xdrproc_t) xdr_Device_Error, (void *) &devErr);
    if(clntStat != RPC_SUCCESS) {
        printf("%s vxiSrqEnable RPC error %s\n",
            pvxiPort->portName, clnt_sperror(pvxiPort->rpcClient,""));
        status = asynError;
    } else if(devErr.error != VXI_OK) {
        printf("%s vxiSrqEnable %s\n",
            pvxiPort->portName, vxiError(devErr.error));
        status = asynError;
    }
    else {
        pvxiPort->srqEnabled = (onOff != 0);
    }
    xdr_free((const xdrproc_t) xdr_Device_Error, (char *) &devErr);
    return status;
}

static asynStatus vxiSerialPollBegin(void *drvPvt)
{
    return asynSuccess;
}

static asynStatus vxiSerialPoll(void *drvPvt, int addr,
    double timeout,int *statusByte)
{
    vxiPort             *pvxiPort = (vxiPort *)drvPvt;
    devLink             *pdevLink;
    enum clnt_stat      clntStat;
    Device_GenericParms devGenP;
    Device_ReadStbResp  devGenR;

    assert(pvxiPort);
    if(addr<0) {
        printf("%s vxiSerialPoll for illegal addr %d\n",pvxiPort->portName,addr);
        return asynError;
    }
    pdevLink = vxiGetDevLink(pvxiPort,0,addr);
    if(!pdevLink) return asynError;
    if(!pdevLink->connected) {
        Device_Link lid;
        if(!vxiCreateDevLink(pvxiPort,addr,&lid)) {
            printf("%s vxiCreateDevLink failed for addr %d\n",
                pvxiPort->portName,addr);
            return asynError;
        }
        pdevLink->lid = lid;
    	pdevLink->connected = TRUE;
    }
    devGenP.lid = pdevLink->lid;
    devGenP.flags = 0; /* no timeout on a locked gateway */
    devGenP.io_timeout = (unsigned long)(timeout*1e3);
    devGenP.lock_timeout = 0;
    /* initialize devGenR */
    memset((char *) &devGenR, 0, sizeof(Device_ReadStbResp));
    clntStat = clientCall(pvxiPort, device_readstb,
        (const xdrproc_t) xdr_Device_GenericParms, (void *) &devGenP,
        (const xdrproc_t) xdr_Device_ReadStbResp, (void *) &devGenR);
    if(clntStat != RPC_SUCCESS) {
        printf("%s vxiSerialPoll %d RPC error %s\n",
            pvxiPort->portName, addr, clnt_sperror(pvxiPort->rpcClient,""));
        return asynError;
    } else if(devGenR.error != VXI_OK) {
        if(devGenR.error == VXI_IOTIMEOUT) {
        /*HP 2050 doews not IBSPD,IBUNT on timeout*/
            char data[2];
            data[0] = IBSPD; data[1] = IBUNT;
            vxiWriteCmd(pvxiPort,pvxiPort->pasynUser,data,2);
        } else {
            printf("%s vxiSerialPoll %d: %s\n",
                pvxiPort->portName, addr, vxiError(devGenR.error));
        }
        return asynError;
    }
    xdr_free((const xdrproc_t) xdr_Device_ReadStbResp, (char *) &devGenR);
    *statusByte = (int)devGenR.stb;
    return asynSuccess;
}

static asynStatus vxiSerialPollEnd(void *drvPvt)
{
    return asynSuccess;
}

static asynStatus vxiSetPortOption(void *drvPvt,
    asynUser *pasynUser,const char *key, const char *val)
{
    vxiPort *pvxiPort = (vxiPort *)drvPvt;
    double  timeout;
    int     seconds,microseconds;
    int     nitems;

    if(epicsStrCaseCmp(key, "rpctimeout") != 0) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
            "Unsupported key \"%s\"", key);
        return asynError;
    }
    nitems = sscanf(val,"%lf",&timeout);
    if(nitems!=1) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
            "Illegal value \"%s\"", val);
        return asynError;
    }
    seconds = (int)timeout;
    microseconds = (int)(((timeout - (double)seconds))*1e6);
    pvxiPort->vxiRpcTimeout.tv_sec = seconds;
    pvxiPort->vxiRpcTimeout.tv_usec = microseconds;
    return asynSuccess;
}

static asynStatus vxiGetPortOption(void *drvPvt,
    asynUser *pasynUser, const char *key, char *val, int valSize)
{
    vxiPort *pvxiPort = (vxiPort *)drvPvt;
    double  timeout;

    if(epicsStrCaseCmp(key, "rpctimeout") != 0) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
            "Unsupported key \"%s\"", key);
        return asynError;
    }
    timeout = pvxiPort->vxiRpcTimeout.tv_sec;
    timeout += ((double)pvxiPort->vxiRpcTimeout.tv_usec)/1e6;
    epicsSnprintf(val,valSize,"%f",timeout);
    return asynSuccess;
}

int vxi11Configure(char *dn, char *hostName, int flags,
    char *defTimeoutString,
    char *vxiName,
    unsigned int priority,
    int noAutoConnect)
{
    char    *portName;
    char    *srqThreadName;
    vxiPort *pvxiPort;
    int     addr,secondary;
    asynStatus status;
    struct sockaddr_in ip;
    struct in_addr     inAddr;
    double defTimeout=0.;
    int     len;
    int     attributes;

    assert(dn && hostName && vxiName);
    /* Force registration */
    if(aToIPAddr(hostName, 0, &ip) < 0) {
        printf("%s Unknown host: \"%s\"\n", dn, hostName);
        return 0;
    }
    inAddr.s_addr = ip.sin_addr.s_addr;
    /* allocate vxiPort structure */
    len = sizeof(vxiPort); /*for vxiPort*/
    len += strlen(dn) + 1; /*for portName*/
    len += strlen(dn) + 4; /*for <portName>SRQ*/
    pvxiPort = callocMustSucceed(len,sizeof(char),"vxi11Configure");
    pvxiPort->vxiRpcTimeout.tv_sec = DEFAULT_RPC_TIMEOUT;
    pvxiPort->portName = portName = (char *)(pvxiPort+1);
    strcpy(portName,dn);
    pvxiPort->srqThreadName = srqThreadName = portName + strlen(dn) + 1;
    strcpy(srqThreadName,dn);
    strcat(srqThreadName,"SRQ");
    pvxiPort->srqEnabled = -1;
    pvxiPort->server.eos = -1;
    for(addr = 0; addr < NUM_GPIB_ADDRESSES; addr++) {
        pvxiPort->primary[addr].primary.eos = -1;
        for(secondary = 0; secondary < NUM_GPIB_ADDRESSES; secondary++) {
            pvxiPort->primary[addr].secondary[secondary].eos = -1;
        }
    }
    pvxiPort->vxiName = epicsStrDup(vxiName);
    if (defTimeoutString) defTimeout = epicsStrtod(defTimeoutString, NULL);
    pvxiPort->defTimeout = (defTimeout>.0001) ? 
        defTimeout : (double)DEFAULT_RPC_TIMEOUT ;
    if(flags & FLAG_RECOVER_WITH_IFC) pvxiPort->recoverWithIFC = TRUE;
    if(flags & FLAG_LOCK_DEVICES) pvxiPort->lockDevices = TRUE;
    pvxiPort->inAddr = inAddr;
    pvxiPort->hostName = (char *)callocMustSucceed(1,strlen(hostName)+1,
        "vxi11Configure");
    if(epicsStrnCaseCmp("gpib", vxiName, 4) == 0) pvxiPort->isGpibLink = 1;
    if(epicsStrnCaseCmp("hpib", vxiName, 4) == 0) pvxiPort->isGpibLink = 1;
    if(epicsStrnCaseCmp("inst", vxiName, 4) == 0) pvxiPort->isSingleLink = 1;
    if(epicsStrnCaseCmp("com", vxiName, 3) == 0) pvxiPort->isSingleLink = 1;
    strcpy(pvxiPort->hostName, hostName);
    attributes = ASYN_CANBLOCK;
    if(!pvxiPort->isSingleLink) attributes |= ASYN_MULTIDEVICE;
    pvxiPort->asynGpibPvt = pasynGpib->registerPort(pvxiPort->portName,
        attributes, !noAutoConnect, &vxi11,pvxiPort,priority,0);
    if(!pvxiPort->asynGpibPvt) {
        printf("registerPort failed\n");
        return 0;
    }
    /* pvxiPort->pasynUser may have been created already by a connection callback to vxiConnectPort */
    if (!pvxiPort->pasynUser) {
        pvxiPort->pasynUser = pasynManager->createAsynUser(0,0);
        pvxiPort->pasynUser->timeout = pvxiPort->defTimeout;
        status = pasynManager->connectDevice(
            pvxiPort->pasynUser,pvxiPort->portName,-1);
        if (status!=asynSuccess) 
            printf("vxiConnectPort: connectDevice failed %s\n",pvxiPort->pasynUser->errorMessage);
    }
    pvxiPort->option.interfaceType = asynOptionType;
    pvxiPort->option.pinterface  = (void *)&vxiOption;
    pvxiPort->option.drvPvt = pvxiPort;
    status = pasynManager->registerInterface(pvxiPort->portName,&pvxiPort->option);
    if(status!=asynSuccess) printf("Can't register option.\n");
    return 0;
}

/*
 * IOC shell command registration
 */
#include <iocsh.h>
static const iocshArg vxi11ConfigureArg0 = { "portName",iocshArgString};
static const iocshArg vxi11ConfigureArg1 = { "host name",iocshArgString};
static const iocshArg vxi11ConfigureArg2 = { "flags (lock devices : recover with IFC)",iocshArgInt};
static const iocshArg vxi11ConfigureArg3 = { "default timeout",iocshArgString};
static const iocshArg vxi11ConfigureArg4 = { "vxiName",iocshArgString};
static const iocshArg vxi11ConfigureArg5 = { "priority",iocshArgInt};
static const iocshArg vxi11ConfigureArg6 = { "disable auto-connect",iocshArgInt};
static const iocshArg *vxi11ConfigureArgs[] = {&vxi11ConfigureArg0,
    &vxi11ConfigureArg1, &vxi11ConfigureArg2, &vxi11ConfigureArg3,
    &vxi11ConfigureArg4, &vxi11ConfigureArg5,&vxi11ConfigureArg6};
static const iocshFuncDef vxi11ConfigureFuncDef = {"vxi11Configure",7,vxi11ConfigureArgs};
static void vxi11ConfigureCallFunc(const iocshArgBuf *args)
{
    vxi11Configure (args[0].sval, args[1].sval, args[2].ival,
                    args[3].sval, args[4].sval, args[5].ival, args[6].ival);
}

extern int E5810Reboot(char * inetAddr,char *password);
extern int E2050Reboot(char * inetAddr);
extern int TDS3000Reboot(char * inetAddr);

static const iocshArg E5810RebootArg0 = { "inetAddr",iocshArgString};
static const iocshArg E5810RebootArg1 = { "password",iocshArgString};
static const iocshArg *E5810RebootArgs[2] = {&E5810RebootArg0,&E5810RebootArg1};
static const iocshFuncDef E5810RebootFuncDef = {"E5810Reboot",2,E5810RebootArgs};
static void E5810RebootCallFunc(const iocshArgBuf *args)
{
    E5810Reboot(args[0].sval,args[1].sval);
}

static const iocshArg E2050RebootArg0 = { "inetAddr",iocshArgString};
static const iocshArg *E2050RebootArgs[1] = {&E2050RebootArg0};
static const iocshFuncDef E2050RebootFuncDef = {"E2050Reboot",1,E2050RebootArgs};
static void E2050RebootCallFunc(const iocshArgBuf *args)
{
    E2050Reboot(args[0].sval);
}

static const iocshArg TDS3000RebootArg0 = { "inetAddr",iocshArgString};
static const iocshArg *TDS3000RebootArgs[1] = {&TDS3000RebootArg0};
static const iocshFuncDef TDS3000RebootFuncDef = {"TDS3000Reboot",1,TDS3000RebootArgs};
static void TDS3000RebootCallFunc(const iocshArgBuf *args)
{
    TDS3000Reboot(args[0].sval);
}

/*
 * This routine is called before multitasking has started, so there's
 * no race condition in the test/set of firstTime.
 */
static void vxi11RegisterCommands (void)
{
    static int firstTime = 1;
    if (firstTime) {
        firstTime = 0;
        iocshRegister(&vxi11ConfigureFuncDef,vxi11ConfigureCallFunc);
        iocshRegister(&E2050RebootFuncDef,E2050RebootCallFunc);
        iocshRegister(&E5810RebootFuncDef,E5810RebootCallFunc);
        iocshRegister(&TDS3000RebootFuncDef,TDS3000RebootCallFunc);
    }
}
epicsExportRegistrar(vxi11RegisterCommands);
