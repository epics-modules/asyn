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
#include <stddef.h>
#include <stdio.h>
#include <errno.h>
#include <ctype.h>
/* epics includes */
#include <dbDefs.h>
#include <taskwd.h>
#include <epicsMutex.h>
#include <epicsEvent.h>
#include <epicsSignal.h>
#include <epicsStdio.h>
#include <osiSock.h>
#include <epicsThread.h>
#include <epicsTime.h>
#include <cantProceed.h>
#include <epicsString.h>
#include <epicsInterruptibleSyscall.h>
#include <epicsExport.h>
/* local includes */
#include "drvVxi11.h"
#include "asynGpibDriver.h"
#include "vxi11.h"
#include "osiRpc.h"
#include "vxi11core.h"
#include "vxi11intr.h"

#ifdef BOOL
#undef BOOL
#endif
#define BOOL int

#define DEFAULT_RPC_TIMEOUT 4
static const char *srqThreadName = "vxi11Srq";

#define setIoTimeout(pvxiPort,pasynUser) \
    ((u_long)(1000.0 * \
    ((pasynUser->timeout > pvxiPort->defTimeout) ? \
      (pasynUser->timeout) : (pvxiPort->defTimeout))))


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
    osiSockAddr   srqPort;         /* TCP port for interrupt channel */
    epicsEventId  srqThreadReady;   /* wait for srqThread to be ready*/
    BOOL          rpcTaskInitCalled; /*Only call rpcTaskInit once*/
    const char    *portName;
    char          *hostName;   /* ip address of VXI-11 server */
    char          *vxiName;   /* Holds name of logical link */
    int           ctrlAddr;
    BOOL          isSingleLink; /* Is this an ethernet device */ 
    struct in_addr inAddr; /* ip address of gateway */
    CLIENT        *rpcClient; /* returned by clnt_create() */
    unsigned long maxRecvSize; /* max. # of bytes accepted by link */
    double        defTimeout;
    devLink       server;
    linkPrimary   primary[NUM_GPIB_ADDRESSES];
    asynUser      *pasynUser;
    unsigned char recoverWithIFC;/*fire out IFC pulse on timeout (read/write)*/
    epicsInterruptibleSyscallContext *srqInterrupt;
}vxiPort;

/* local variables */
typedef struct vxiLocal {
    struct timeval vxiRpcTimeout;/* time to wait for RPC completion */
}vxiLocal;
static vxiLocal *pvxiLocal = 0;

/* Local routines */
static char *vxiError(Device_ErrorCode error);
static BOOL vxiIsPortConnected(vxiPort * pvxiPort,asynUser *pasynUser);
static void vxiDisconnectException(vxiPort *pvxiPort,int addr);
static BOOL vxiCreateLink(vxiPort * pvxiPort,
    char *devName,Device_Link *pDevice_Link);
static devLink *vxiGetDevLink(vxiPort * pvxiPort, asynUser *pasynUser,int addr);
static BOOL vxiDestroyDevLink(vxiPort * pvxiPort, Device_Link devLink);
static int vxiWriteAddressed(vxiPort * pvxiPort,asynUser *pasynUser,
    Device_Link lid,char *buffer,int length,double timeout);
static int vxiWriteCmd(vxiPort * pvxiPort,asynUser *pasynUser,
    char *buffer, int length);
static enum clnt_stat clientCall(vxiPort * pvxiPort,
    u_long req,xdrproc_t proc1, caddr_t addr1,xdrproc_t proc2, caddr_t addr2);
static int vxiBusStatus(vxiPort * pvxiPort, int request, double timeout);
static int vxiInit(void);
static void vxiCreateIrqChannel(vxiPort *pvxiPort);
static asynStatus vxiConnectPort(vxiPort *pvxiPort,asynUser *pasynUser);
static asynStatus vxiDisconnectPort(vxiPort *pvxiPort);
static void vxiSrqThread(void *pvxiPort);

/* asynGpibPort methods */
static void vxiReport(void *pdrvPvt,FILE *fd,int details);
static asynStatus vxiConnect(void *pdrvPvt,asynUser *pasynUser);
static asynStatus vxiDisconnect(void *pdrvPvt,asynUser *pasynUser);
static asynStatus vxiSetPortOption(void *pdrvPvt,asynUser *pasynUser,
    const char *key, const char *val);
static asynStatus vxiGetPortOption(void *pdrvPvt,asynUser *pasynUser,
    const char *key, char *val, int sizeval);
static int vxiRead(void *pdrvPvt,asynUser *pasynUser,char *data,int maxchars);
static int vxiWrite(void *pdrvPvt,asynUser *pasynUser,const char *data,int numchars);
static asynStatus vxiFlush(void *pdrvPvt,asynUser *pasynUser);
static asynStatus vxiSetEos(void *pdrvPvt,asynUser *pasynUser,
    const char *eos,int eoslen);
static asynStatus vxiGetEos(void *pdrvPvt,asynUser *pasynUser,
    char *eos, int eossize, int *eoslen);
static asynStatus vxiAddressedCmd(void *pdrvPvt,asynUser *pasynUser,
    const char *data, int length);
static asynStatus vxiUniversalCmd(void *pdrvPvt, asynUser *pasynUser, int cmd);
static asynStatus vxiIfc(void *pdrvPvt, asynUser *pasynUser);
static asynStatus vxiRen(void *pdrvPvt,asynUser *pasynUser, int onOff);
static int vxiSrqStatus(void *pdrvPvt);
static asynStatus vxiSrqEnable(void *pdrvPvt, int onOff);
static asynStatus vxiSerialPollBegin(void *pdrvPvt);
static int vxiSerialPoll(void *pdrvPvt, int addr, double timeout);
static asynStatus vxiSerialPollEnd(void *pdrvPvt);

/******************************************************************************
 * Convert VXI error code to a string.
 ******************************************************************************/
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
        printf("vxiError error = %ld\n", error);
        return ("VXI: unknown error");
    }
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
            "%s %d vxiDisconnectException exceptionDisconnect failed %s\n",
            pvxiPort->portName,addr,pasynUser->errorMessageSize);
    }
    status = pasynManager->disconnect(pasynUser);
    assert(status==asynSuccess);
    status = pasynManager->connectDevice(pasynUser,pvxiPort->portName,-1);
    assert(status==asynSuccess);
}

static BOOL vxiCreateLink(vxiPort * pvxiPort,
    char *devName,Device_Link *pDevice_Link)
{
    enum clnt_stat   clntStat;
    Create_LinkParms crLinkP;
    Create_LinkResp  crLinkR;
    BOOL             rtnVal = FALSE;
    asynUser         *pasynUser = pvxiPort->pasynUser;

    crLinkP.clientId = (long) pvxiPort->rpcClient;
    crLinkP.lockDevice = 0;  /* do not try to lock the device */
    crLinkP.lock_timeout = 0;/* if device is locked, forget it */
    crLinkP.device = devName;
    /* initialize crLinkR */
    memset((char *) &crLinkR, 0, sizeof(Create_LinkResp));
    /* RPC call */
    clntStat = clnt_call(pvxiPort->rpcClient, create_link,
        (xdrproc_t)xdr_Create_LinkParms,(caddr_t)&crLinkP,
        (xdrproc_t)xdr_Create_LinkResp, (caddr_t)&crLinkR,
        pvxiLocal->vxiRpcTimeout);
    if(clntStat != RPC_SUCCESS) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "%s vxiCreateLink RPC error %s\n",
            devName,clnt_sperror(pvxiPort->rpcClient,""));
    } else if(crLinkR.error != VXI_OK) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "%s vxiCreateLink error %s\n",
            devName,vxiError(crLinkR.error));
    } else {
        *pDevice_Link = crLinkR.lid;
        rtnVal = TRUE;
        if(pvxiPort->maxRecvSize==0) {
            pvxiPort->maxRecvSize = crLinkR.maxRecvSize;
        }
    }
    xdr_free((const xdrproc_t) xdr_Create_LinkResp, (char *) &crLinkR);
    return rtnVal;
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
        pvxiLocal->vxiRpcTimeout);
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
        printf("%s vxiWriteAddressed %s error %s\n",
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
 *
 * Return status if successful else ERROR.
 ******************************************************************************/
static int vxiBusStatus(vxiPort * pvxiPort, int request, double timeout)
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
            printf("%s vxiBusStatus error %s\n",
                pvxiPort->portName, clnt_sperror(pvxiPort->rpcClient,""));
            xdr_free((const xdrproc_t)xdr_Device_DocmdResp,(char *) &devDocmdR);
            return -1;
        }
        if(devDocmdR.error != VXI_OK) {
            if(devDocmdR.error != VXI_IOTIMEOUT) {
                printf("%s vxiBusStatus error %s\n",
                    pvxiPort->portName, vxiError(devDocmdR.error));
            }
            xdr_free((const xdrproc_t)xdr_Device_DocmdResp,(char *)&devDocmdR);
            return -1;
        }
        result = ntohs(*(unsigned short *) devDocmdR.data_out.data_out_val);
        if(request) {
            status = result;
        } else if(result) {
            status |= (1<<data); /* bit numbers start at 0 */
        }
        xdr_free((const xdrproc_t) xdr_Device_DocmdResp,(char *) &devDocmdR);
    }
    return status;
}

static enum clnt_stat clientCall(vxiPort * pvxiPort,
    u_long req,xdrproc_t proc1, caddr_t addr1,xdrproc_t proc2, caddr_t addr2)
{
    enum clnt_stat stat;
    asynUser *pasynUser = pvxiPort->pasynUser;

    errno = 0;
    stat = clnt_call(pvxiPort->rpcClient,
        req, proc1, addr1, proc2, addr2, pvxiLocal->vxiRpcTimeout);
    if(stat!=RPC_SUCCESS || errno!=0) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "%s vxi11 clientCall errno %d clnt_stat %d\n",
            pvxiPort->portName,errno,stat);
        if(stat!=RPC_TIMEDOUT) vxiDisconnectPort(pvxiPort);
    }
    return stat;
}

static int vxiInit()
{
    if(pvxiLocal) return 0;
    pvxiLocal = callocMustSucceed(1,sizeof(vxiLocal),"vxiInit");
    pvxiLocal->vxiRpcTimeout.tv_sec = DEFAULT_RPC_TIMEOUT;
    return 0;
}

static void vxiCreateIrqChannel(vxiPort *pvxiPort)
{
    enum clnt_stat clntStat;
    Device_Error devErr;
    Device_RemoteFunc devRemF;

    /* create the interrupt channel */
    devRemF.hostAddr = ntohl(pvxiPort->srqPort.ia.sin_addr.s_addr);
    devRemF.hostPort = ntohs(pvxiPort->srqPort.ia.sin_port);
    devRemF.progNum = DEVICE_INTR;
    devRemF.progVers = DEVICE_INTR_VERSION;
    devRemF.progFamily = DEVICE_TCP;
    memset((char *) &devErr, 0, sizeof(Device_Error));
    clntStat = clientCall(pvxiPort,  create_intr_chan,
        (const xdrproc_t) xdr_Device_RemoteFunc, (void *) &devRemF,
        (const xdrproc_t) xdr_Device_Error, (void *) &devErr);
    if(clntStat != RPC_SUCCESS) {
        printf("%s vxiCreateIrqChannel (create_intr_chan)%s\n",
            pvxiPort->portName,clnt_sperror(pvxiPort->rpcClient,""));
        xdr_free((const xdrproc_t) xdr_Device_Error, (char *) &devErr);
        clnt_destroy(pvxiPort->rpcClient);
    } else if(devErr.error != VXI_OK) {
        printf("%s vxiCreateIrqChannel %s (create_intr_chan)\n",
            pvxiPort->portName, vxiError(devErr.error));
        xdr_free((const xdrproc_t) xdr_Device_Error, (char *) &devErr);
        clnt_destroy(pvxiPort->rpcClient);
    } else {
        vxiSrqEnable(pvxiPort,1);
        xdr_free((const xdrproc_t) xdr_Device_Error, (char *) &devErr);
        return;
    }
    printf("Warning -- SRQ not operational.\n");
}

static asynStatus vxiConnectPort(vxiPort *pvxiPort,asynUser *pasynUser)
{
    int         isController;
    Device_Link link;

    if(pvxiPort->server.connected) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "%s vxiConnectPort but already connected\n");
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
    pvxiPort->rpcClient = clnt_create(pvxiPort->hostName,
        DEVICE_CORE, DEVICE_CORE_VERSION, "tcp");
    if(!pvxiPort->rpcClient) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,"%s vxiConnectPort error %s\n",
            pvxiPort->portName, clnt_spcreateerror(pvxiPort->hostName));
        return asynError;
    }
    /* now establish a link to the gateway (for docmds etc.) */
    if(!vxiCreateLink(pvxiPort,pvxiPort->vxiName,&link)) return asynError;
    pvxiPort->server.lid = link;
    pvxiPort->server.connected = TRUE;
    /* Ask the controller's gpib address.*/
    pvxiPort->ctrlAddr = vxiBusStatus(pvxiPort,
        VXI_BSTAT_BUS_ADDRESS,pvxiPort->defTimeout);
    if(pvxiPort->ctrlAddr == -1) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
       	    "%s vxiConnectPort cannot read bus status initialization aborted\n",
            pvxiPort->portName);
        clnt_destroy(pvxiPort->rpcClient);
        return asynError;
    }
    /* initialize the vxiPort structure with the data we have got so far */
    pvxiPort->primary[pvxiPort->ctrlAddr].primary.lid = link;
    pvxiPort->primary[pvxiPort->ctrlAddr].primary.connected = TRUE;
    /* now we can use vxiBusStatus; if we are not the controller fail */
    isController = vxiBusStatus(pvxiPort, VXI_BSTAT_SYSTEM_CONTROLLER,
        pvxiPort->defTimeout);
    if(isController < 0) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "%s vxiConnectPort vxiBusStatus error %d initialization aborted\n",
            pvxiPort->portName,isController);
        clnt_destroy(pvxiPort->rpcClient);
        return asynError;
    }
    if(isController == 0) {
        isController = vxiBusStatus(pvxiPort, VXI_BSTAT_CONTROLLER_IN_CHARGE,
            pvxiPort->defTimeout);
        if(isController < 0) {
            asynPrint(pasynUser,ASYN_TRACE_ERROR,
                "%s vxiConnectPort vxiBusStatus error %d initialization aborted\n",
                pvxiPort->portName,isController);
            clnt_destroy(pvxiPort->rpcClient);
            return asynError;
        }
        if(isController == 0) {
            asynPrint(pasynUser,ASYN_TRACE_ERROR,
                "%s vxiConnectPort neither system controller nor "
                "controller in charge -- initialization aborted\n",
                pvxiPort->portName);
            clnt_destroy(pvxiPort->rpcClient);
            return asynError;
        }
    }
    pvxiPort->srqThreadReady = epicsEventMustCreate(epicsEventEmpty);
    pvxiPort->srqInterrupt = epicsInterruptibleSyscallCreate();
    if(pvxiPort->srqInterrupt == NULL) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "%s vxiGenLink can't create interruptible syscall context.\n",
            pvxiPort->portName);
        return asynError;
    }
    epicsThreadCreate(srqThreadName, 46,
          epicsThreadGetStackSize(epicsThreadStackMedium),
          vxiSrqThread,pvxiPort);
    epicsEventMustWait(pvxiPort->srqThreadReady);
    vxiCreateIrqChannel(pvxiPort);
    pasynManager->exceptionConnect(pvxiPort->pasynUser);
    return asynSuccess;
}

static asynStatus vxiDisconnectPort(vxiPort *pvxiPort)
{
    int          addr,secondary;
    Device_Error devErr;
    int          dummy;
    asynUser     *pasynUser = pvxiPort->pasynUser;
    enum clnt_stat clntStat;

    if(!pvxiPort->server.connected) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "%s vxiDisconnectPort but not connected\n");
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
    clntStat = clnt_call(pvxiPort->rpcClient, destroy_intr_chan,
        (xdrproc_t) xdr_void, (caddr_t) &dummy,
        (xdrproc_t) xdr_Device_Error,(caddr_t) &devErr,
        pvxiLocal->vxiRpcTimeout);
    /* report errors only if debug flag set. If any errors, the next time this
     * link is initialized, make sure a new SRQ thread is started.  See comments
     * in vxiConnectPort. */
    if(clntStat != RPC_SUCCESS) {
        if(pasynUser) {
            asynPrint(pasynUser,ASYN_TRACE_ERROR,"%s vxiDisconnectPort %s\n",
                pvxiPort->portName,clnt_sperror(pvxiPort->rpcClient,""));
        } else {
            printf(pvxiPort->portName,clnt_sperror(pvxiPort->rpcClient,""));
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
    vxiDestroyDevLink(pvxiPort, pvxiPort->server.lid);
    pvxiPort->server.connected = FALSE;
    pvxiPort->server.lid = 0;
    clnt_destroy(pvxiPort->rpcClient);
    if(pvxiPort->srqInterrupt) {
        int i;
        for (i = 0 ; ; i++) {
            if (epicsEventWaitWithTimeout(pvxiPort->srqThreadReady,2.0) == epicsEventWaitOK) {
                epicsInterruptibleSyscallDelete(pvxiPort->srqInterrupt);
                break;
            }
            if(i == 10) {
                printf("WARNING -- %s SRQ thread will not terminate!\n",
                                                           pvxiPort->portName);
                break;
            }
            epicsInterruptibleSyscallInterrupt(pvxiPort->srqInterrupt);
        }
        pvxiPort->srqInterrupt = NULL;
    }
    epicsEventDestroy(pvxiPort->srqThreadReady);
    pvxiPort->srqThreadReady = 0;
    pasynManager->exceptionDisconnect(pvxiPort->pasynUser);
    return asynSuccess;
}


static void vxiSrqThread(void *arg)
{
    vxiPort *pvxiPort = arg;
    epicsThreadId myTid;
    int s, s1;
    osiSockAddr farAddr;
    osiSocklen_t addrlen;
    char buf[512];
    int i;

    s = socket (AF_INET, SOCK_STREAM, 0);
    if (s < 0) {
        printf ("SrqThread(): can't create socket: %s\n", strerror (errno));
        return;
    }
    pvxiPort->srqPort.ia.sin_family = AF_INET;
    pvxiPort->srqPort.ia.sin_port = htons (0);
    pvxiPort->srqPort.ia.sin_addr.s_addr = INADDR_ANY;
    memset (pvxiPort->srqPort.ia.sin_zero, '\0',
        sizeof pvxiPort->srqPort.ia.sin_zero);
    if (bind (s, &pvxiPort->srqPort.sa, sizeof pvxiPort->srqPort.ia) < 0) {
        printf ("SrqThread(): can't bind socket: %s\n", strerror (errno));
        close(s);
        return;
    }
    addrlen = sizeof pvxiPort->srqPort;
    getsockname(s, &pvxiPort->srqPort.sa, &addrlen); /* Gets port but not addres
s */
    i = pvxiPort->srqPort.ia.sin_port;
    pvxiPort->srqPort = osiLocalAddr(s); /* Gets address, but not port */
    pvxiPort->srqPort.ia.sin_port = i;
    if (listen (s, 2) < 0) {
        printf ("SrqThread(): can't listen on socket: %s\n", strerror (errno
));
        close(s);
        return;
    }
    myTid = epicsThreadGetIdSelf();
    taskwdInsert(myTid, NULL, NULL);
    epicsInterruptibleSyscallArm(pvxiPort->srqInterrupt, s, myTid);
    epicsEventSignal(pvxiPort->srqThreadReady);
    addrlen = sizeof farAddr.ia;
    s1 = accept(s, &farAddr.sa, &addrlen);
    if(epicsInterruptibleSyscallWasInterrupted(pvxiPort->srqInterrupt)) {
        if(!epicsInterruptibleSyscallWasClosed(pvxiPort->srqInterrupt))
            close(s);
        taskwdRemove(myTid);
        epicsEventSignal(pvxiPort->srqThreadReady);
        return;
    }
    close(s);
    if(s1 < 0) {
        printf("SrqThread(): can't accept connection: %s\n", strerror(errno));
        taskwdRemove(myTid);
        epicsEventSignal(pvxiPort->srqThreadReady);
        return;
    }
    epicsInterruptibleSyscallArm(pvxiPort->srqInterrupt, s1, myTid);
    for(;;) {
        if(epicsInterruptibleSyscallWasInterrupted(pvxiPort->srqInterrupt))
            break;
        i = read(s1, buf, sizeof buf);
        if(epicsInterruptibleSyscallWasInterrupted(pvxiPort->srqInterrupt))
            break;
        if(i < 0) {
            printf("SrqThread(): read error: %s\n", strerror(errno));
            break;
        }
        else if (i == 0) {
            printf("SrqThread(): read EOF\n");
            break;
        }
        pasynGpib->srqHappened(pvxiPort->asynGpibPvt);
    }
    if(!epicsInterruptibleSyscallWasClosed(pvxiPort->srqInterrupt))
        close(s1);
    printf("SrqThread(): terminating\n");
    taskwdRemove(myTid);
    epicsEventSignal(pvxiPort->srqThreadReady);
}

static void vxiReport(void *pdrvPvt,FILE *fd,int details)
{
    vxiPort *pvxiPort = (vxiPort *)pdrvPvt;
    assert(pvxiPort);
    fprintf(fd,"    vxi11, host name: %s\n", pvxiPort->hostName);
    if(details > 1) {
        char nameBuf[60];
        if(ipAddrToHostName(&pvxiPort->inAddr, nameBuf, sizeof nameBuf) > 0)
            fprintf(fd,"    ip address:%s\n", nameBuf);
        fprintf(fd,"    vxi name:%s", pvxiPort->vxiName);
        fprintf(fd," ctrlAddr:%d",pvxiPort->ctrlAddr);
        fprintf(fd," maxRecvSize:%lu", pvxiPort->maxRecvSize);
        fprintf(fd," isSingleLink:%s\n",
            ((pvxiPort->isSingleLink) ? "yes" : "no"));
    }
}

static asynStatus vxiConnect(void *pdrvPvt,asynUser *pasynUser)
{
    vxiPort     *pvxiPort = (vxiPort *)pdrvPvt;
    Device_Link lid;
    int         addr = pasynManager->getAddr(pasynUser);
    devLink     *pdevLink = vxiGetDevLink(pvxiPort,pasynUser,addr);
    int         primary,secondary;
    char        devName[40];

    if(!pdevLink) return asynError;
    asynPrint(pasynUser,ASYN_TRACE_FLOW,
        "%s addr %d vxiConnect\n",pvxiPort->portName,addr);
    if(pdevLink->connected) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
            "%s addr %d vxiConnect request but already connected\n",
            pvxiPort->portName,addr);
        return asynError;
    }
    if(addr!=-1 && !vxiIsPortConnected(pvxiPort,pasynUser)) {
        pasynManager->exceptionConnect(pasynUser);
        return asynSuccess;
    }
    if(addr==-1) return vxiConnectPort(pvxiPort,pasynUser);
    if(addr<100) {
        primary = addr; secondary = 0;
    } else {
        primary = addr / 100;
        secondary = addr % 100;
    }
    if(addr<100) {
        sprintf(devName, "%s,%d", pvxiPort->vxiName, primary);
    } else {
        sprintf(devName, "%s,%d,%d", pvxiPort->vxiName, primary, secondary);
    }
    if(!vxiCreateLink(pvxiPort,devName,&lid)) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "%s vxiCreateLink failed for addr %d\n",pvxiPort->portName,addr);
        return asynError;
    }
    pdevLink->lid = lid;
    pdevLink->connected = TRUE;
    pasynManager->exceptionConnect(pasynUser);
    return asynSuccess;
}

static asynStatus vxiDisconnect(void *pdrvPvt,asynUser *pasynUser)
{
    vxiPort *pvxiPort = (vxiPort *)pdrvPvt;
    int     addr = pasynManager->getAddr(pasynUser);
    devLink *pdevLink = vxiGetDevLink(pvxiPort,pasynUser,addr);
    asynStatus status = asynSuccess;

    if(!pdevLink) return asynError;
    asynPrint(pasynUser,ASYN_TRACE_FLOW,
        "%s addr %d vxiDisconnect\n",pvxiPort->portName,addr);
    if(!pdevLink->connected) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
            "%s addr %d vxiDisconnect request but not connected\n",
            pvxiPort->portName,addr);
        return asynError;
    }
    if(addr==-1) return vxiDisconnectPort(pvxiPort);
    if(!vxiDestroyDevLink(pvxiPort,pdevLink->lid)) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
            "%s vxiDestroyDevLink failed for addr %d\n",pvxiPort->portName,addr);
        status = asynError;
    }
    pdevLink->lid = 0;
    pdevLink->connected = 0;
    pasynManager->exceptionDisconnect(pasynUser);
    return status;
}

static asynStatus vxiSetPortOption(void *pdrvPvt,asynUser *pasynUser,
    const char *key, const char *val)
{
    vxiPort *pvxiPort = (vxiPort *)pdrvPvt;
    epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
        "%s vxi11 does not have any options\n",pvxiPort->portName);
    return asynError;
}

static asynStatus vxiGetPortOption(void *pdrvPvt,asynUser *pasynUser,
    const char *key, char *val, int sizeval)
{
    vxiPort *pvxiPort = (vxiPort *)pdrvPvt;
    epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
        "%s vxi11 does not have any options\n",pvxiPort->portName);
    return asynError;
}

static int vxiRead(void *pdrvPvt,asynUser *pasynUser,char *data,int maxchars)
{
    vxiPort *pvxiPort = (vxiPort *)pdrvPvt;
    int     nRead = 0, thisRead;
    int     addr = pasynManager->getAddr(pasynUser);
    devLink *pdevLink = vxiGetDevLink(pvxiPort,pasynUser,addr);
    enum clnt_stat   clntStat;
    Device_ReadParms devReadP;
    Device_ReadResp  devReadR;

    assert(data);
    if(!pdevLink) return -1;
    if(!vxiIsPortConnected(pvxiPort,pasynUser)) return -1;
    if(!pdevLink->connected) return -1;
    devReadP.lid = pdevLink->lid;
    /* device link is created; do the read */
    do {
        thisRead = -1;
        devReadP.requestSize = maxchars;
        devReadP.io_timeout = setIoTimeout(pvxiPort,pasynUser);
        devReadP.lock_timeout = 0;
        devReadP.flags = 0;
        if(pdevLink->eos != -1) {
            devReadP.flags |= VXI_TERMCHRSET;
            devReadP.termChar = pdevLink->eos;
        }
        /* initialize devReadR */
        memset((char *) &devReadR, 0, sizeof(Device_ReadResp));
        /* RPC call */
        clntStat = clientCall(pvxiPort, device_read,
            (const xdrproc_t) xdr_Device_ReadParms,(void *) &devReadP,
            (const xdrproc_t) xdr_Device_ReadResp,(void *) &devReadR);
        if(clntStat != RPC_SUCCESS) {
            asynPrint(pasynUser,ASYN_TRACE_ERROR,
                        "%s vxiRead %d, %s, %d %s\n",
                                    pvxiPort->portName, addr, data, maxchars,
                                    clnt_sperror(pvxiPort->rpcClient, ""));
        } else if(devReadR.error != VXI_OK) {
            if((devReadR.error == VXI_IOTIMEOUT) && (pvxiPort->recoverWithIFC))
                vxiIfc(pdrvPvt, pasynUser);
            asynPrint(pasynUser,ASYN_TRACE_ERROR,
                        "%s vxiRead %d, %s, %d %s\n",
                                    pvxiPort->portName, addr, data, maxchars,
                                    vxiError(devReadR.error));
        } else {
            asynPrintIO(pasynUser,ASYN_TRACEIO_DRIVER,
                devReadR.data.data_val,devReadR.data.data_len,
                "%s %d vxiRead\n",pvxiPort->portName,addr);
            thisRead = devReadR.data.data_len;
            if(thisRead>0) {
                memcpy(data, devReadR.data.data_val, thisRead);
                nRead += thisRead;
                data += thisRead;
                maxchars -= thisRead;
            }
        }
        xdr_free((const xdrproc_t) xdr_Device_ReadResp, (char *) &devReadR);
    } while(!devReadR.reason && thisRead>0);
    /* send <UNT,UNL> after completion */
    /* SHOULD THIS BE DONE ???*/
    if(vxiWriteCmd(pvxiPort,pasynUser, "_?", 2) != 2) return -1;
    return nRead ? nRead : -1;
}

static int vxiWrite(void *pdrvPvt,asynUser *pasynUser,
    const char *data,int numchars)
{
    vxiPort *pvxiPort = (vxiPort *) pdrvPvt;
    int     status = 0;
    int     addr = pasynManager->getAddr(pasynUser);
    devLink *pdevLink = vxiGetDevLink(pvxiPort,pasynUser,addr);
    enum clnt_stat    clntStat;
    Device_WriteParms devWriteP;
    Device_WriteResp  devWriteR;
    int rtnlen = 0;
    int lennow;

    assert(data);
    asynPrint(pasynUser,ASYN_TRACE_FLOW,
        "%s %d vxiWrite numchars %d\n",pvxiPort->portName,addr,numchars);
    if(!pdevLink) return -1;
    if(!vxiIsPortConnected(pvxiPort,pasynUser)) return -1;
    if(!pdevLink->connected) return -1;
    devWriteP.lid = pdevLink->lid;;
    devWriteP.io_timeout = setIoTimeout(pvxiPort,pasynUser);
    devWriteP.lock_timeout = 0;
    /*write at most maxRecvSize bytes at a time */
    do {
        if(numchars<=pvxiPort->maxRecvSize) {
            devWriteP.flags = VXI_ENDW;
            lennow = numchars;
        } else {
            devWriteP.flags = 0;
            lennow = pvxiPort->maxRecvSize;
        }
        devWriteP.data.data_len = lennow;
        devWriteP.data.data_val = (char *)data;
        /* initialize devWriteR */
        memset((char *) &devWriteR, 0, sizeof(Device_WriteResp));
        /* RPC call */
        clntStat = clientCall(pvxiPort, device_write,
            (const xdrproc_t) xdr_Device_WriteParms,(void *) &devWriteP,
            (const xdrproc_t) xdr_Device_WriteResp,(void *) &devWriteR);
        if(clntStat != RPC_SUCCESS) {
            asynPrint(pasynUser,ASYN_TRACE_ERROR,
                "%s vxiWrite %d, \"%s\", %d)%s\n",
                pvxiPort->portName,
                addr, data, numchars,
                clnt_sperror(pvxiPort->rpcClient, ""));
            status = -1;
        } else if(devWriteR.error != VXI_OK) {
            if(devWriteR.error != VXI_IOTIMEOUT) {
                asynPrint(pasynUser,ASYN_TRACE_ERROR,
                    "%s vxiWrite %d, \"%s\", %d: %s\n",
                    pvxiPort->portName, addr, data, numchars,
                    vxiError(devWriteR.error));
            }
            if(devWriteR.error == VXI_IOTIMEOUT && pvxiPort->recoverWithIFC) 
                vxiIfc(pdrvPvt, pasynUser);
            status = -1;
        } else {
            status = devWriteR.size;
            asynPrintIO(pasynUser,ASYN_TRACEIO_DRIVER,
                devWriteP.data.data_val,devWriteP.data.data_len,
                "%s %d vxiWrite\n",pvxiPort->portName,addr);
            data += status;
            numchars -= status;
            rtnlen += status;
        }
        xdr_free((const xdrproc_t) xdr_Device_WriteResp, (char *) &devWriteR);
    } while(status==lennow && numchars>0);
    /* send <UNT,UNL> after completion */
    /* SHOULD THIS BE DONE ???*/
    if(vxiWriteCmd(pvxiPort,pasynUser, "_?", 2) != 2) return -1;
    return rtnlen;
}

static asynStatus vxiFlush(void *pdrvPvt,asynUser *pasynUser)
{
    /*Nothing to do */
    return asynSuccess;
}

static asynStatus vxiSetEos(void *pdrvPvt,asynUser *pasynUser,
    const char *eos,int eoslen)
{
    vxiPort *pvxiPort = (vxiPort *)pdrvPvt;
    int     addr = pasynManager->getAddr(pasynUser);
    devLink *pdevLink = vxiGetDevLink(pvxiPort,pasynUser,addr);

    if(!pdevLink) return asynError;
    asynPrintIO(pasynUser, ASYN_TRACE_FLOW, eos, eoslen,
            "%s vxiSetEos %d: ",pvxiPort->portName,eoslen);
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

static asynStatus vxiGetEos(void *pdrvPvt,asynUser *pasynUser,
    char *eos, int eossize, int *eoslen)
{
    vxiPort *pvxiPort = (vxiPort *)pdrvPvt;
    int     addr = pasynManager->getAddr(pasynUser);
    devLink *pdevLink = vxiGetDevLink(pvxiPort,pasynUser,addr);

    if(!pdevLink) return asynError;
    if(eossize<1) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "%s vxiGetEos eossize %d too small\n",eossize);
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
            "%s vxiGetEos %d: ",pvxiPort->portName,eoslen);
    return asynSuccess;
}

static asynStatus vxiAddressedCmd(void *pdrvPvt,asynUser *pasynUser,
    const char *data, int length)
{
    vxiPort *pvxiPort = (vxiPort *)pdrvPvt;
    long    status;
    int     addr = pasynManager->getAddr(pasynUser);
    devLink *pdevLink = vxiGetDevLink(pvxiPort,pasynUser,addr);
    Device_Link lid;

    assert(data);
    if(!pdevLink) return asynError;
    if(!vxiIsPortConnected(pvxiPort,pasynUser)) return asynError;
    if(!pdevLink->connected) return -1;
    lid = pdevLink->lid;
    asynPrint(pasynUser, ASYN_TRACE_FLOW,
        "%s %d vxiAddressedCmd %2.2x\n",pvxiPort->portName,addr,data);
    asynPrintIO(pasynUser,ASYN_TRACEIO_DRIVER,
        data,length,"%s %d vxiAddressedCmd\n",pvxiPort->portName,addr);
    status = vxiWriteAddressed(pvxiPort,pasynUser,lid,
        (char *)data,length,pvxiPort->defTimeout);
    if(status!=length)asynPrint(pasynUser,ASYN_TRACE_ERROR,
        "%s %d vxiAddressedCmd requested %d but sent %d bytes\n",
        pvxiPort->portName,addr,length,status);
    return status;
}

static asynStatus vxiUniversalCmd(void *pdrvPvt, asynUser *pasynUser, int cmd)
{
    vxiPort *pvxiPort = (vxiPort *)pdrvPvt;
    long    nout;
    char    data[2];

    asynPrint(pasynUser, ASYN_TRACE_FLOW,
        "%s vxiUniversalCmd %d\n",pvxiPort->portName,cmd);
    data[0] = cmd;
    data[1] = 0;
    nout = vxiWriteCmd(pvxiPort,pasynUser, data, 1);
    return (nout==1 ? 0 : -1);
}

static asynStatus vxiIfc(void *pdrvPvt, asynUser *pasynUser)
{
    vxiPort *pvxiPort = (vxiPort *)pdrvPvt;
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
    devDocmdP.io_timeout = (u_long)(1000.0*pvxiPort->defTimeout);
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
        asynPrint(pasynUser,ASYN_TRACE_ERROR,"%s vxiIfc\n",pvxiPort->portName);
        status = asynError;
    } else if(devDocmdR.error != VXI_OK) {
        if(devDocmdR.error != VXI_IOTIMEOUT) {
            asynPrint(pasynUser,ASYN_TRACE_ERROR,"%s vxiIfc %s\n",
                pvxiPort->portName, vxiError(devDocmdR.error));
        }
        status = asynError;
    }
    xdr_free((const xdrproc_t) xdr_Device_DocmdResp, (char *) &devDocmdR);
    return status;
}

static asynStatus vxiRen(void *pdrvPvt,asynUser *pasynUser, int onOff)
{
    vxiPort *pvxiPort = (vxiPort *)pdrvPvt;
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
    devDocmdP.io_timeout = (u_long)(1000.0*pvxiPort->defTimeout);
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
        asynPrint(pasynUser,ASYN_TRACE_ERROR,"%s vxiRen\n", pvxiPort->portName);
        status = asynError;
    } else if(devDocmdR.error != VXI_OK) {
        if(devDocmdR.error != VXI_IOTIMEOUT) {
            asynPrint(pasynUser,ASYN_TRACE_ERROR,"%s vxiRen %s\n", 
                pvxiPort->portName, vxiError(devDocmdR.error));
        }
        status = asynError;
    }
    xdr_free((const xdrproc_t) xdr_Device_DocmdResp, (char *) &devDocmdR);
    return status;
}

static int vxiSrqStatus(void *pdrvPvt)
{
    vxiPort *pvxiPort = (vxiPort *)pdrvPvt;
    int     status;

    assert(pvxiPort);
    status = vxiBusStatus(pvxiPort, VXI_BSTAT_SRQ, pvxiPort->defTimeout);
    return status;
}

static asynStatus vxiSrqEnable(void *pdrvPvt, int onOff)
{
    vxiPort    *pvxiPort = (vxiPort *)pdrvPvt;
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
        printf("%s vxiSrqEnable %s\n",
            pvxiPort->portName, clnt_sperror(pvxiPort->rpcClient,""));
        status = asynError;
    } else if(devErr.error != VXI_OK) {
        printf("%s vxiSrqEnable %s\n",
            pvxiPort->portName, vxiError(devErr.error));
        status = asynError;
    }
    xdr_free((const xdrproc_t) xdr_Device_Error, (char *) &devErr);
    return status;
}

static asynStatus vxiSerialPollBegin(void *pdrvPvt)
{
    return asynSuccess;
}

static int vxiSerialPoll(void *pdrvPvt, int addr, double timeout)
{
    vxiPort *pvxiPort = (vxiPort *)pdrvPvt;
    devLink *pdevLink = vxiGetDevLink(pvxiPort,0,addr);
    enum clnt_stat      clntStat;
    Device_GenericParms devGenP;
    Device_ReadStbResp  devGenR;
    unsigned char       stb;

    if(!pdevLink) return asynError;
    if(!vxiIsPortConnected(pvxiPort,0)) return asynError;
    if(!pdevLink->connected) {
        printf("%s vxiSerialPoll port not connected\n",pvxiPort->portName);
        return 0;
    }
    devGenP.lid = pdevLink->lid;
    devGenP.flags = 0; /* no timeout on a locked gateway */
    devGenP.io_timeout = (u_long)(1000.0*pvxiPort->defTimeout);
    devGenP.lock_timeout = 0;
    /* initialize devGenR */
    memset((char *) &devGenR, 0, sizeof(Device_ReadStbResp));
    clntStat = clientCall(pvxiPort, device_readstb,
        (const xdrproc_t) xdr_Device_GenericParms, (void *) &devGenP,
        (const xdrproc_t) xdr_Device_ReadStbResp, (void *) &devGenR);
    if(clntStat != RPC_SUCCESS) {
        printf("%s vxiSerialPoll %d %s\n",
            pvxiPort->portName, addr, clnt_sperror(pvxiPort->rpcClient,""));
        return -1;
    } else if(devGenR.error != VXI_OK) {
        if(devGenR.error != VXI_IOTIMEOUT) {
            printf("%s vxiSerialPoll %d: %s\n",
                pvxiPort->portName, addr, vxiError(devGenR.error));
        }
        return -1;
    } else {
        stb = devGenR.stb;
    }
    xdr_free((const xdrproc_t) xdr_Device_ReadStbResp, (char *) &devGenR);
    return stb;
}

static asynStatus vxiSerialPollEnd(void *pdrvPvt)
{
    return asynSuccess;
}

static asynGpibPort vxi11 = {
    vxiReport,
    vxiConnect,
    vxiDisconnect,
    vxiSetPortOption,
    vxiGetPortOption,
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

int vxi11SetRpcTimeout(double timeout)
{
    int seconds,microseconds;

    seconds = (int)timeout;
    microseconds = (int)(((timeout - (double)seconds))/1e6);
    pvxiLocal->vxiRpcTimeout.tv_sec = seconds;
    pvxiLocal->vxiRpcTimeout.tv_usec = microseconds;
    return 0;
}

int vxi11Configure(char *dn, char *hostName, int recoverWithIFC,
    double defTimeout,
    char *vxiName,
    unsigned int priority,
    int noAutoConnect)
{
    char    *portName;
    vxiPort *pvxiPort;
    int     addr,secondary;
    asynStatus status;
    struct sockaddr_in ip;
    struct in_addr     inAddr;

    assert(dn && hostName && vxiName);
    /* Force registration */
    if(vxiInit() != 0) return -1;
    portName = callocMustSucceed(strlen(dn)+1,sizeof(char),
        "vxi11Configure");
    strcpy(portName,dn);
    if(aToIPAddr(hostName, 0, &ip) < 0) {
        printf("%s Unknown host: \"%s\"\n", portName, hostName);
        return 0;
    }
    inAddr.s_addr = ip.sin_addr.s_addr;
    /* allocate vxiPort structure */
    pvxiPort = (vxiPort *)callocMustSucceed(1,sizeof(vxiPort),"vxi11Configure");
    pvxiPort->portName = portName;
    pvxiPort->server.eos = -1;
    for(addr = 0; addr < NUM_GPIB_ADDRESSES; addr++) {
        pvxiPort->primary[addr].primary.eos = -1;
        for(secondary = 0; secondary < NUM_GPIB_ADDRESSES; secondary++) {
            pvxiPort->primary[addr].secondary[secondary].eos = -1;
        }
    }
    pvxiPort->vxiName = epicsStrDup(vxiName);
    pvxiPort->defTimeout = (defTimeout>.0001) ? 
        defTimeout : (double)DEFAULT_RPC_TIMEOUT ;
    if(recoverWithIFC) pvxiPort->recoverWithIFC = TRUE;
    pvxiPort->inAddr = inAddr;
    pvxiPort->hostName = (char *)callocMustSucceed(1,strlen(hostName)+1,
        "vxi11Configure");
    pvxiPort->isSingleLink = (epicsStrnCaseCmp("inst", vxiName, 4) == 0);
    strcpy(pvxiPort->hostName, hostName);
    pvxiPort->asynGpibPvt = pasynGpib->registerPort(pvxiPort->portName,
        (pvxiPort->isSingleLink ? 0 : 1),!noAutoConnect,
        &vxi11,pvxiPort,priority,0);
    pvxiPort->pasynUser = pasynManager->createAsynUser(0,0);
    status = pasynManager->connectDevice(
        pvxiPort->pasynUser,pvxiPort->portName,-1);
    if(status!=asynSuccess) 
        printf("connectDevice failed %s\n",pvxiPort->pasynUser->errorMessage);
    return 0;
}

/*
 * IOC shell command registration
 */
#include <iocsh.h>
static const iocshArg vxi11ConfigureArg0 = { "portName",iocshArgString};
static const iocshArg vxi11ConfigureArg1 = { "host name",iocshArgString};
static const iocshArg vxi11ConfigureArg2 = { "recover with IFC?",iocshArgInt};
static const iocshArg vxi11ConfigureArg3 = { "default timeout",iocshArgDouble};
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
                    args[3].dval, args[4].sval, args[5].ival, args[6].ival);
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

static const iocshArg vxi11SetRpcTimeoutArg0 = {"double",iocshArgDouble};
static const iocshArg *vxi11SetRpcTimeoutArgs[1] = {&vxi11SetRpcTimeoutArg0};
static const iocshFuncDef vxi11SetRpcTimeoutFuncDef =
    {"vxi11SetRpcTimeout",1,vxi11SetRpcTimeoutArgs};
static void vxi11SetRpcTimeoutCallFunc(const iocshArgBuf *args)
{ 
    vxi11SetRpcTimeout(args[0].dval);
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
        iocshRegister(&vxi11SetRpcTimeoutFuncDef,vxi11SetRpcTimeoutCallFunc);
    }
}
epicsExportRegistrar(vxi11RegisterCommands);
