/*drvVxi11.c */
/***********************************************************************
* Copyright (c) 2002 The University of Chicago, as Operator of Argonne
* National Laboratory, and the Regents of the University of
* California, as Operator of Los Alamos National Laboratory, and
* Berliner Elektronenspeicherring-Gesellschaft m.b.H. (BESSY).
* gpibCore is distributed subject to a Software License Agreement
* found in file LICENSE that is included with this distribution.
***********************************************************************/
/*
 *		Low level GPIB driver for HP E2050A LAN-GPIB gateway
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
/* epics includes */
#include <dbDefs.h>
#include <taskwd.h>
#include <errlog.h>
#include <epicsMutex.h>
#include <epicsEvent.h>
#include <osiSock.h>
#include <epicsThread.h>
#include <epicsTime.h>
#include <cantProceed.h>
#include <epicsExport.h>
/* local includes */
#include "drvVxi11.h"
#include "asynGpibDriver.h"
#include "vxi11.h"
#include "vxi11core.h"
#include "vxi11intr.h"

#ifdef BOOL
#undef BOOL
#endif
#define BOOL int

#define DEFAULT_RPC_TIMEOUT 2
static const char *srqThreadName = "vxi11Srq";

#define setIoTimeout(pvxiLink,pasynUser) \
    ((u_long)(1000.0 * \
    ((pasynUser->timeout > pvxiLink->defTimeout) ? \
      (pasynUser->timeout) : (pvxiLink->defTimeout))))


typedef struct devLinkPrimary {
    Device_Link primary;
    BOOL        primaryConnected;
    Device_Link secondary[NUM_GPIB_ADDRESSES];
    BOOL        secondaryConnected[NUM_GPIB_ADDRESSES];
}devLinkPrimary;
/******************************************************************************
 * This structure is used to hold the hardware-specific information for a
 * single GPIB link. There is one for each gateway.
 ******************************************************************************/
typedef struct vxiLink {
    void *asynGpibPvt;
    epicsThreadId srqThreadId;   /* current SRQ task id */
    osiSockAddr srqPort;         /* TCP port for interrupt channel */
    epicsEventId srqTaskReady;   /* wait for srqTask to be ready*/
    const char *portName;
    char *hostName;	   /* ip address of VXI-11 server */
    char vxiName[20];	   /* Holds name of logical link */
    int ctrlAddr;
    int eos;
    Device_Link serverAddr;
    BOOL isSingleLink; /* Is this an ethernet device */ 
    struct in_addr inAddr;	/* ip address of gateway */
    CLIENT *rpcClient;	/* returned by clnt_create() */
    unsigned long maxRecvSize;	/* max. # of bytes accepted by link */
    double defTimeout;
    /*a devLink for each possible primary device including the gateway itself*/
    devLinkPrimary devLink[NUM_GPIB_ADDRESSES];
    unsigned char recoverWithIFC;/*fire out IFC pulse on timeout (read/write)*/
    unsigned char restartActive; /* Is vxiReconnect active */
}vxiLink;

/* global variables */
int vxi11Debug = 0;
epicsExportAddress(int,vxi11Debug);

/* local variables */
typedef struct vxiLocal {
    struct timeval vxiRpcTimeout;/* time to wait for RPC completion */
}vxiLocal;
static vxiLocal *pvxiLocal = 0;

/* Local routines */
static char *vxiError(Device_ErrorCode error);
static BOOL vxiCreateLink(vxiLink * pvxiLink,
    char *devName,Device_Link *pDevice_Link);
static BOOL vxiSetDevLink(vxiLink * pvxiLink, int addr,
    Device_Link *callerLink);
static int vxiDestroyDevLink(vxiLink * pvxiLink, Device_Link devLink);
static int vxiWriteAddressed(vxiLink * pvxiLink, Device_Link lid,
    char *buffer, int length, double timeout);
static int vxiWriteCmd(vxiLink * pvxiLink, char *buffer, int length);
static enum clnt_stat clientCall(vxiLink * pvxiLink, u_long req,
    xdrproc_t proc1, caddr_t addr1,xdrproc_t proc2, caddr_t addr2);
static int vxiBusStatus(vxiLink * pvxiLink, int request, double timeout);
static void vxiReconnect(vxiLink * pvxiLink);
static int vxiInit(void);
static void vxiCreateIrqChannel(vxiLink *pvxiLink);
static int vxiSrqTask(vxiLink *pvxiLink);

/* asynGpibPort methods */
static void vxiReport(void *pdrvPvt,FILE *fd,int details);
static asynStatus vxiConnect(void *pdrvPvt,asynUser *pasynUser);
static asynStatus vxiDisconnect(void *pdrvPvt,asynUser *pasynUser);
static int vxiRead(void *pdrvPvt,asynUser *pasynUser,char *data,int maxchars);
static int vxiWrite(void *pdrvPvt,asynUser *pasynUser,const char *data,int numchars);
static asynStatus vxiFlush(void *pdrvPvt,asynUser *pasynUser);
static asynStatus vxiSetEos(void *pdrvPvt,asynUser *pasynUser,
    const char *eos,int eoslen);
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

static BOOL vxiCreateLink(vxiLink * pvxiLink,
    char *devName,Device_Link *pDevice_Link)
{
    enum clnt_stat clntStat;
    Create_LinkParms crLinkP;
    Create_LinkResp crLinkR;
    BOOL rtnVal = 0;

    crLinkP.clientId = (long) pvxiLink->rpcClient;
    crLinkP.lockDevice = 0;	/* do not try to lock the device */
    crLinkP.lock_timeout = 0;	/* if device is locked, forget it */
    crLinkP.device = devName;
    /* initialize crLinkR */
    memset((char *) &crLinkR, 0, sizeof(Create_LinkResp));
    /* RPC call */
    clntStat = clientCall(pvxiLink, create_link,
        (const xdrproc_t) xdr_Create_LinkParms,(void *) &crLinkP,
        (const xdrproc_t) xdr_Create_LinkResp,(void *) &crLinkR);
    if(clntStat != RPC_SUCCESS) {
        errlogPrintf("%s vxiCreateLink RPC error %s\n",
            devName,clnt_sperror(pvxiLink->rpcClient, ""));
    } else if(crLinkR.error != VXI_OK) {
        errlogPrintf("%s vxiCreateLink error %s\n",
            devName,vxiError(crLinkR.error));
    } else {
	*pDevice_Link = crLinkR.lid;
        rtnVal = 1;
        if(pvxiLink->maxRecvSize==0) {
            pvxiLink->maxRecvSize = crLinkR.maxRecvSize;
        }
    }
    xdr_free((const xdrproc_t) xdr_Create_LinkResp, (char *) &crLinkR);
    return(rtnVal);
}

static BOOL vxiSetDevLink(vxiLink *pvxiLink, int addr,
    Device_Link *callerLink)
{
    char devName[40];
    Device_Link link = 0;
    int status;
    int primary,secondary;

    assert(pvxiLink);
    if(pvxiLink->isSingleLink) {
        *callerLink = pvxiLink->serverAddr;
        return(1);
    }
    if(addr<100) {
        primary = addr; secondary = 0;
    } else {
        primary = addr / 100;
        secondary = addr % 100;
    }
    if(addr<100) {
        if(pvxiLink->devLink[addr].primaryConnected) {
	    *callerLink = pvxiLink->devLink[addr].primary;
            return(1);
        }
    } else {
        if(pvxiLink->devLink[primary].secondaryConnected[secondary]) {
	    *callerLink = pvxiLink->devLink[primary].secondary[secondary];
            return(1);
        }
    }
    /* Make sure bus is available */
    status = vxiBusStatus(pvxiLink, VXI_BSTAT_SYSTEM_CONTROLLER,
        pvxiLink->defTimeout);
    if(status==-1) return 0;
    if(addr<100) {
        sprintf(devName, "%s,%d", pvxiLink->vxiName, primary);
    } else {
        sprintf(devName, "%s,%d,%d", pvxiLink->vxiName, primary, secondary);
    }
    if(!vxiCreateLink(pvxiLink,devName,&link)) return(0);
    if(addr<100) {
	pvxiLink->devLink[primary].primary = link;
	pvxiLink->devLink[primary].primaryConnected = 1;
    } else {
	pvxiLink->devLink[primary].secondary[secondary] = link;
	pvxiLink->devLink[primary].secondaryConnected[secondary] = 1;
    }
    *callerLink = link;
    return(1);
}

static int vxiDestroyDevLink(vxiLink * pvxiLink, Device_Link devLink)
{
    enum clnt_stat clntStat;
    Device_Error devErr;
    int status = 0;
    assert(pvxiLink);
    clntStat = clientCall(pvxiLink, destroy_link,
        (const xdrproc_t) xdr_Device_Link,(void *) &devLink,
        (const xdrproc_t) xdr_Device_Error, (void *) &devErr);
    if(clntStat != RPC_SUCCESS) {
	status = -1;
        if(vxi11Debug) printf("%s vxiDestroyDevLink RPC error %s\n",
             pvxiLink->portName,clnt_sperror(pvxiLink->rpcClient, ""));
    } else if(devErr.error != VXI_OK) {
	status = -1;
        if(vxi11Debug) printf("%s vxiDestroyDevLink error %s\n",
            pvxiLink->portName,vxiError(devErr.error));
    }
    xdr_free((const xdrproc_t) xdr_Device_Error, (char *) &devErr);
    return status;
}

/*write with ATN true */
static int vxiWriteAddressed(vxiLink * pvxiLink, Device_Link lid,
    char *buffer, int length, double timeout)
{
    int status = 0;
    enum clnt_stat clntStat;
    Device_DocmdParms devDocmdP;
    Device_DocmdResp devDocmdR;

    assert(pvxiLink);
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
    clntStat = clientCall(pvxiLink, device_docmd,
        (const xdrproc_t) xdr_Device_DocmdParms,(void *) &devDocmdP,
        (const xdrproc_t) xdr_Device_DocmdResp,(void *) &devDocmdR);
    if(clntStat != RPC_SUCCESS) {
	errlogPrintf("%s vxiWriteAddressed %s error %s\n",
            pvxiLink->portName,buffer,clnt_sperror(pvxiLink->rpcClient, ""));
	status = -1;
    } else if(devDocmdR.error != VXI_OK) {
	if(devDocmdR.error != VXI_IOTIMEOUT) {
	    errlogPrintf("%s vxiWriteAddressed %s error %s\n",
                pvxiLink->portName,buffer,vxiError(devDocmdR.error));
        }
	status = -1;
    } else {
	status = devDocmdR.data_out.data_out_len;
    }
    xdr_free((const xdrproc_t) xdr_Device_DocmdResp, (char *) &devDocmdR);
    return(status);
}

/******************************************************************************
 * Output a command string to the GPIB bus.
 * The string is sent with the ATN line held TRUE (low).
 ******************************************************************************/
static int vxiWriteCmd(vxiLink * pvxiLink, char *buffer, int length)
{
    long status;

    status = vxiWriteAddressed(pvxiLink,
        pvxiLink->serverAddr,buffer,length,pvxiLink->defTimeout);
    return(status);
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
static int vxiBusStatus(vxiLink * pvxiLink, int request, double timeout)
{
    enum clnt_stat clntStat;
    Device_DocmdParms devDocmdP;
    Device_DocmdResp devDocmdR;
    unsigned short data;	/* what to ask */
    int status = 0, start, stop;
    assert(pvxiLink);
    assert(request >= 0 && request <= VXI_BSTAT_BUS_ADDRESS);
    devDocmdP.lid = pvxiLink->serverAddr;
    devDocmdP.flags = 0;	/* no timeout on a locked gateway */
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
	clntStat = clientCall(pvxiLink, device_docmd,
            (const xdrproc_t) xdr_Device_DocmdParms,(void *) &devDocmdP,
            (const xdrproc_t) xdr_Device_DocmdResp, (void *) &devDocmdR);
	if(clntStat != RPC_SUCCESS) {
	    errlogPrintf("%s vxiBusStatus error %s\n",
                pvxiLink->portName, clnt_sperror(pvxiLink->rpcClient, ""));
	    xdr_free((const xdrproc_t)xdr_Device_DocmdResp,(char *) &devDocmdR);
	    return(-1);
	}
	if(devDocmdR.error != VXI_OK) {
	    if(devDocmdR.error != VXI_IOTIMEOUT) {
	        errlogPrintf("%s vxiBusStatus error %s\n",
                    pvxiLink->portName, vxiError(devDocmdR.error));
            }
	    xdr_free((const xdrproc_t)xdr_Device_DocmdResp,(char *)&devDocmdR);
	    return(-1);
	}
	result = ntohs(*(unsigned short *) devDocmdR.data_out.data_out_val);
	if(request) {
	    status = result;
	} else if(result) {
            status |= (1<<data); /* bit numbers start at 0 */
	}
	xdr_free((const xdrproc_t) xdr_Device_DocmdResp,(char *) &devDocmdR);
    }
    return(status);
}

/* If lan server disconnects than automatically restart link             */
/* When clientCall detects a disconnect it starts thread vxiReconnect      */
/* vxiReconnect repeatedly trys to connect to the server. When it connects */
/* it calls drvGpibResetLink and terminates.                             */
static enum clnt_stat clientCall(vxiLink * pvxiLink, u_long req,
    xdrproc_t proc1, caddr_t addr1,xdrproc_t proc2, caddr_t addr2)
{
    int errnosave;
    enum clnt_stat stat;

    if(pvxiLink->restartActive) return (RPC_FAILED);
    errno = 0;
    stat = clnt_call(pvxiLink->rpcClient,
        req, proc1, addr1, proc2, addr2, pvxiLocal->vxiRpcTimeout);
    if(stat == RPC_SUCCESS) return (stat);
    if((errno != ECONNRESET) && (errno != EPIPE)) return (stat);
    errnosave = errno;
    pvxiLink->restartActive = 1;
    printf("epicsThreadCreate vxiReconnect\n");
    epicsThreadCreate("vxiReconnect", epicsThreadPriorityLow,
        epicsThreadGetStackSize(epicsThreadStackMedium),
        (EPICSTHREADFUNC)vxiReconnect, pvxiLink);
    errno = errnosave;
    return (stat);
}

typedef struct reconnect {
    vxiLink *pvxiLink;
    asynUser *pasynUser;
}reconnect;

static void reconnectCallback(asynUser *pasynUser)
{
    reconnect *preconnect = (reconnect *)pasynUser->userPvt;
    vxiLink *pvxiLink = preconnect->pvxiLink;
    asynStatus status;

    status = vxiDisconnect(pvxiLink,pasynUser);
    assert(status==asynSuccess);
    status = vxiConnect(pvxiLink,pasynUser);
    if(status!=asynSuccess) {
        errlogPrintf("%s reconnect attempt failed\n",pvxiLink->portName);
        cantProceed("vxi11");
    }
    pasynManager->freeAsynUser(preconnect->pasynUser);
    free(preconnect);
    pvxiLink->restartActive = 0;
}

static void vxiReconnect(vxiLink * pvxiLink)
{
    struct sockaddr_in serverAddr;
    int fd, status;
    reconnect *preconnect;

    /* wait until we can connect */
    while(1) {
	errno = 0;
	fd = epicsSocketCreate(PF_INET, SOCK_STREAM, 0);
	if(fd == -1) {
	    printf("vxiReconnect failed %s\n", strerror(errno));
	    return;
	}
	memset((char *) &serverAddr, 0, sizeof(struct sockaddr_in));
	serverAddr.sin_family = PF_INET;
	/* 23 is telnet port */
	status = aToIPAddr(pvxiLink->hostName, 23, &serverAddr);
	if(status) {
	    printf("vxiReconnect aToIPAddr failed\n");
	    close(fd);
	    return;
	}
	errno = 0;
	status = connect(fd,(struct sockaddr *)&serverAddr,sizeof(serverAddr));
	printf("connect returned %d\n", status);
	if(status) {
	    epicsThreadSleep(10.0);
	    close(fd);
	    continue;
	}
	break;
    }
    close(fd);
    preconnect = callocMustSucceed(1,sizeof(reconnect),"vxiReconnect");
    preconnect->pvxiLink = pvxiLink;
    preconnect->pasynUser = pasynManager->createAsynUser(reconnectCallback,0);
    preconnect->pasynUser->userPvt = preconnect;
    status = pasynManager->connectDevice(
        preconnect->pasynUser,pvxiLink->portName,0);
    status = pasynManager->queueRequest(preconnect->pasynUser,
        asynQueuePriorityLow,0);
}

static int vxiInit()
{
    if(pvxiLocal) return(0);
    pvxiLocal = callocMustSucceed(1,sizeof(vxiLocal),"vxiInit");
    pvxiLocal->vxiRpcTimeout.tv_sec = DEFAULT_RPC_TIMEOUT;
    if(rpcTaskInit() != 0) {	/* init RPC lib for iocInit task */
	errlogPrintf("vxiInit can't init RPC library\n");
	return(-1);
    }
    return 0;
}

static void vxiCreateIrqChannel(vxiLink *pvxiLink)
{
    enum clnt_stat clntStat;
    Device_Error devErr;
    Device_RemoteFunc devRemF;

    /* create the interrupt channel */
    devRemF.hostAddr = ntohl(pvxiLink->srqPort.ia.sin_addr.s_addr);
    devRemF.hostPort = ntohs(pvxiLink->srqPort.ia.sin_port);
    devRemF.progNum = DEVICE_INTR;
    devRemF.progVers = DEVICE_INTR_VERSION;
    devRemF.progFamily = DEVICE_TCP;
    memset((char *) &devErr, 0, sizeof(Device_Error));
    clntStat = clientCall(pvxiLink, create_intr_chan,
        (const xdrproc_t) xdr_Device_RemoteFunc, (void *) &devRemF,
        (const xdrproc_t) xdr_Device_Error, (void *) &devErr);
    if(clntStat != RPC_SUCCESS) {
	errlogPrintf("%s vxiCreateIrqChannel (create_intr_chan)%s\n",
            pvxiLink->portName,clnt_sperror(pvxiLink->rpcClient, ""));
	xdr_free((const xdrproc_t) xdr_Device_Error, (char *) &devErr);
	clnt_destroy(pvxiLink->rpcClient);
    } else if(devErr.error != VXI_OK) {
	errlogPrintf("%s vxiCreateIrqChannel %s (create_intr_chan)\n",
            pvxiLink->portName, vxiError(devErr.error));
	xdr_free((const xdrproc_t) xdr_Device_Error, (char *) &devErr);
	clnt_destroy(pvxiLink->rpcClient);
    } else {
        vxiSrqEnable(pvxiLink,1);
        xdr_free((const xdrproc_t) xdr_Device_Error, (char *) &devErr);
        return;
    }
    errlogPrintf("Warning -- SRQ not operational.\n");
}

static int vxiSrqTask(vxiLink *pvxiLink)
{
    int s, s1;
    osiSockAddr farAddr;
    osiSocklen_t addrlen;
    char buf[512];
    int i;

    s = socket (AF_INET, SOCK_STREAM, 0);
    if (s < 0) {
        errlogPrintf ("SrqTask(): can't create socket: %s\n", strerror (errno));
        return -1;
    }
    pvxiLink->srqPort.ia.sin_family = AF_INET;
    pvxiLink->srqPort.ia.sin_port = htons (0);
    pvxiLink->srqPort.ia.sin_addr.s_addr = INADDR_ANY;
    memset (pvxiLink->srqPort.ia.sin_zero, '\0',
        sizeof pvxiLink->srqPort.ia.sin_zero);
    if (bind (s, &pvxiLink->srqPort.sa, sizeof pvxiLink->srqPort.ia) < 0) {
        errlogPrintf ("SrqTask(): can't bind socket: %s\n", strerror (errno));
        close(s);
        return -1;
    }
    addrlen = sizeof pvxiLink->srqPort;
    getsockname(s, &pvxiLink->srqPort.sa, &addrlen); /* Gets port but not addres
s */
    i = pvxiLink->srqPort.ia.sin_port;
    pvxiLink->srqPort = osiLocalAddr(s); /* Gets address, but not port */
    pvxiLink->srqPort.ia.sin_port = i;
    if (listen (s, 2) < 0) {
        errlogPrintf ("SrqTask(): can't listen on socket: %s\n", strerror (errno
));
        close(s);
        return -1;
    }
    pvxiLink->srqThreadId = epicsThreadGetIdSelf();
    taskwdInsert(pvxiLink->srqThreadId, NULL, NULL);
    epicsEventSignal(pvxiLink->srqTaskReady);
    addrlen = sizeof farAddr.ia;
    s1 = accept (s, &farAddr.sa, &addrlen);
    if (s1 < 0) {
        errlogPrintf ("SrqTask(): can't accept connection: %s\n",
            strerror (errno));
        close(s);
        return -1;
    }
    while((i = read(s1, buf, sizeof buf)) >= 0)
        pasynGpib->srqHappened(pvxiLink->asynGpibPvt);
    if (i < 0) {
        errlogPrintf ("SrqTask(): read error: %s\n", strerror (errno));
    }
    else {
        errlogPrintf ("SrqTask(): read EOF\n");
    }
    close(s);
    close(s1);
    return i;
}

static void vxiReport(void *pdrvPvt,FILE *fd,int details)
{
    vxiLink *pvxiLink = (vxiLink *)pdrvPvt;
    assert(pvxiLink);
    fprintf(fd,"  HP link, host name: %s\n", pvxiLink->hostName);
    if(details > 1) {
	char nameBuf[60];
	if(ipAddrToHostName(&pvxiLink->inAddr, nameBuf, sizeof nameBuf) > 0)
	    fprintf(fd," ip address: %s\n", nameBuf);
	fprintf(fd,"  vxi name: %s\n", pvxiLink->vxiName);
	fprintf(fd,"  maxRecvSize: %lu\n", pvxiLink->maxRecvSize);
	fprintf(fd,"  isSingleLink: %s\n", ((pvxiLink->isSingleLink) ? "yes" : "no"));
    }
}

/******************************************************************************
 * Create an RPC client, create an interface link for the gateway, and finally
 * create device links for all registered devices.
 *
 * This function may only be called from a asynTask! (Otherwise results are
 * desastrous, i.e. system crash. This is caused by the VxWorks implementation
 * of the RPC library.)
 ******************************************************************************/
static asynStatus vxiConnect(void *pdrvPvt,asynUser *pasynUser)
{
    vxiLink *pvxiLink = (vxiLink *)pdrvPvt;
    int isController;
    Device_Link link;

    if(vxi11Debug) printf("%s vxiConnect\n",pvxiLink->portName);
    assert(pvxiLink);
    if(rpcTaskInit() == -1) {
        errlogPrintf("%s Can't init RPC\n",pvxiLink->portName);
        return(asynError);
    }
    pvxiLink->rpcClient = clnt_create(pvxiLink->hostName,
        DEVICE_CORE, DEVICE_CORE_VERSION, "tcp");
    if(!pvxiLink->rpcClient) {
	errlogPrintf("%s vxiConnect error %s\n",
            pvxiLink->portName, clnt_spcreateerror(pvxiLink->hostName));
	return(asynError);
    }
    /* now establish a link to the gateway (for docmds etc.) */
    if(!vxiCreateLink(pvxiLink,pvxiLink->vxiName,&link)) return(asynError);
    pvxiLink->serverAddr = link;
    /* Ask the controller's gpib address.*/
    memset((char *) pvxiLink->devLink, 0, NUM_GPIB_ADDRESSES * sizeof(devLinkPrimary));
    pvxiLink->ctrlAddr = vxiBusStatus(pvxiLink,
        VXI_BSTAT_BUS_ADDRESS,pvxiLink->defTimeout);
    if(pvxiLink->ctrlAddr == -1) {
	errlogPrintf("%s vxiConnect cannot read bus status,"
            " initialization aborted\n",pvxiLink->portName);
	clnt_destroy(pvxiLink->rpcClient);
	return(asynError);
    }
    /* initialize the vxiLink structure with the data we have got so far */
    pvxiLink->devLink[pvxiLink->ctrlAddr].primary = link;
    pvxiLink->devLink[pvxiLink->ctrlAddr].primaryConnected = 1;
    /* now we can use vxiBusStatus; if we are not the controller fail */
    isController = vxiBusStatus(pvxiLink, VXI_BSTAT_SYSTEM_CONTROLLER,
        pvxiLink->defTimeout);
    if(isController < 0) {
        errlogPrintf("%s vxiConnect vxiBusStatus error %d"
            " initialization aborted\n",pvxiLink->portName,isController);
        clnt_destroy(pvxiLink->rpcClient);
        return(asynError);
    }
    if(isController == 0) {
        isController = vxiBusStatus(pvxiLink, VXI_BSTAT_CONTROLLER_IN_CHARGE,
            pvxiLink->defTimeout);
        if(isController < 0) {
            errlogPrintf("vxiGenLink vxiBusStatus error %d"
                         " initialization aborted\n",isController);
            clnt_destroy(pvxiLink->rpcClient);
            return -1;
        }
        if(isController == 0) {
            errlogPrintf("vxiGenLink neither system controller nor "
                "controller in charge -- initialization aborted\n");
            clnt_destroy(pvxiLink->rpcClient);
            return -1;
        }
    }
    pvxiLink->srqTaskReady = epicsEventMustCreate(epicsEventEmpty);
    epicsThreadCreate(srqThreadName, 46,
          epicsThreadGetStackSize(epicsThreadStackMedium),
          (EPICSTHREADFUNC) vxiSrqTask,pvxiLink);
    epicsEventMustWait(pvxiLink->srqTaskReady);
    vxiCreateIrqChannel(pvxiLink);
    return(asynSuccess);
}

static asynStatus vxiDisconnect(void *pdrvPvt,asynUser *pasynUser)
{
    vxiLink *pvxiLink = (vxiLink *)pdrvPvt;
    int addr;
    int secondary;
    Device_Error devErr;
    int dummy;
    enum clnt_stat clntStat;

    assert(pvxiLink);
    if(vxi11Debug) printf("%s vxiDisconnect\n",pvxiLink->portName);
    for(addr = 0; addr < NUM_GPIB_ADDRESSES; addr++) {
	int link;
	link = pvxiLink->devLink[addr].primary;
	if(link > 0) vxiDestroyDevLink(pvxiLink, link);
	for(secondary = 0; secondary < NUM_GPIB_ADDRESSES; secondary++) {
	    link = pvxiLink->devLink[addr].secondary[secondary];
	    if(link > 0) vxiDestroyDevLink(pvxiLink, link);
	}
    }
    clntStat = clientCall(pvxiLink, destroy_intr_chan,
        (const xdrproc_t) xdr_void, (void *) &dummy,
        (const xdrproc_t) xdr_Device_Error,(void *) &devErr);
    /* report errors only if debug flag set. If any errors, the next time this
     * link is initialized, make sure a new SRQ task is started.  See comments
     * in vxiConnect. */
    if(clntStat != RPC_SUCCESS) {
        if(vxi11Debug) printf("%s vxiDestroyLink %s\n",
            pvxiLink->portName,clnt_sperror(pvxiLink->rpcClient,""));
    } else if(devErr.error != VXI_OK) {
        if(vxi11Debug) printf("%s vxiDestroyLink %s\n",
            pvxiLink->portName,vxiError(devErr.error));
    }
    xdr_free((const xdrproc_t) xdr_Device_Error, (char *) &devErr);
    clnt_destroy(pvxiLink->rpcClient);
    return(0);
}

static int vxiRead(void *pdrvPvt,asynUser *pasynUser,char *data,int maxchars)
{
    vxiLink *pvxiLink = (vxiLink *)pdrvPvt;
    int status = 0;
    int addr = pasynManager->getAddr(pasynUser);
    enum clnt_stat clntStat;
    Device_ReadParms devReadP;
    Device_ReadResp devReadR;

    assert(pvxiLink);
    assert(data);
    if(vxi11Debug) printf("%s vxiRead addr %d\n",pvxiLink->portName,addr);
    if(!vxiSetDevLink(pvxiLink, addr, &devReadP.lid)) return(-1);
    /* device link is created; do the read */
    do{
	devReadP.requestSize = maxchars;
	devReadP.io_timeout = setIoTimeout(pvxiLink,pasynUser);
	devReadP.lock_timeout = 0;
	devReadP.flags = 0;
	if(pvxiLink->eos != -1) {
	    devReadP.flags |= VXI_TERMCHRSET;
	    devReadP.termChar = pvxiLink->eos;
	}
	/* initialize devReadR */
	memset((char *) &devReadR, 0, sizeof(Device_ReadResp));
	/* RPC call */
	clntStat = clientCall(pvxiLink, device_read,
            (const xdrproc_t) xdr_Device_ReadParms,(void *) &devReadP,
            (const xdrproc_t) xdr_Device_ReadResp,(void *) &devReadR);
	if(clntStat != RPC_SUCCESS) {
	    errlogPrintf("%s vxiRead %d, %s, %d %s\n",
                pvxiLink->portName, addr, data, maxchars,
                clnt_sperror(pvxiLink->rpcClient, ""));
	    status = -1;
	} else if(devReadR.error != VXI_OK) {
	    if(devReadR.error != VXI_IOTIMEOUT) {
		errlogPrintf("%s vxiRead %d, %s, %d %s\n",
                    pvxiLink->portName, addr, data, maxchars,
                    vxiError(devReadR.error));
            }
	    if(devReadR.error == VXI_IOTIMEOUT && pvxiLink->recoverWithIFC){
		/* try to recover */
		vxiAddressedCmd(pvxiLink,pasynUser,IBSDC,1);
            }
	    status = -1;
	} else {
	    memcpy(data, devReadR.data.data_val, devReadR.data.data_len);
	    status += devReadR.data.data_len;
	    data += devReadR.data.data_len;
	    maxchars -= devReadR.data.data_len;
	}
	xdr_free((const xdrproc_t) xdr_Device_ReadResp, (char *) &devReadR);
    }
    while(status != -1 && !devReadR.reason);
    /* send <UNT,UNL> after completion */
    /* SHOULD THIS BE DONE ???*/
    if(vxiWriteCmd(pvxiLink, "_?", 2) != 2) return(-1);
    return(status);
}

static int vxiWrite(void *pdrvPvt,asynUser *pasynUser,
    const char *data,int numchars)
{
    vxiLink *pvxiLink = (vxiLink *) pdrvPvt;
    int status = 0;
    int addr = pasynManager->getAddr(pasynUser);
    enum clnt_stat clntStat;
    Device_WriteParms devWriteP;
    Device_WriteResp devWriteR;
    int rtnlen = 0;
    int lennow;

    assert(pvxiLink && data);
    if(vxi11Debug) printf("%s %d vxiWrite %s\n",pvxiLink->portName,addr,data);
    assert(pvxiLink);
    assert(data);
    if(!vxiSetDevLink(pvxiLink, addr, &devWriteP.lid)) return(-1);
    devWriteP.io_timeout = setIoTimeout(pvxiLink,pasynUser);
    devWriteP.lock_timeout = 0;
    /*write at most maxRecvSize bytes at a time */
    do {
        if(numchars<=pvxiLink->maxRecvSize) {
            devWriteP.flags = VXI_ENDW;
            lennow = numchars;
        } else {
            devWriteP.flags = 0;
            lennow = pvxiLink->maxRecvSize;
        }
        devWriteP.data.data_len = lennow;
        devWriteP.data.data_val = (char *)data;
        /* initialize devWriteR */
        memset((char *) &devWriteR, 0, sizeof(Device_WriteResp));
        /* RPC call */
        clntStat = clientCall(pvxiLink, device_write,
            (const xdrproc_t) xdr_Device_WriteParms,(void *) &devWriteP,
            (const xdrproc_t) xdr_Device_WriteResp,(void *) &devWriteR);
        if(clntStat != RPC_SUCCESS) {
    	    errlogPrintf("%s vxiWrite %d, \"%s\", %d)%s\n",
                pvxiLink->portName,
                addr, data, numchars,
                clnt_sperror(pvxiLink->rpcClient, ""));
    	    status = -1;
        } else if(devWriteR.error != VXI_OK) {
    	    if(devWriteR.error != VXI_IOTIMEOUT) {
    	        errlogPrintf("%s vxiWrite %d, \"%s\", %d: %s\n",
                    pvxiLink->portName, addr, data, numchars,
                    vxiError(devWriteR.error));
            }
    	    if(devWriteR.error == VXI_IOTIMEOUT && pvxiLink->recoverWithIFC) 
    	        vxiAddressedCmd(pvxiLink,pasynUser,IBSDC,1);
    	    status = -1;
        } else {
    	    status = devWriteR.size;
        }
        xdr_free((const xdrproc_t) xdr_Device_WriteResp, (char *) &devWriteR);
        data += lennow;
        numchars -= lennow;
        rtnlen += lennow;
    } while(status==lennow && numchars>0);
    /* send <UNT,UNL> after completion */
    /* SHOULD THIS BE DONE ???*/
    if(vxiWriteCmd(pvxiLink, "_?", 2) != 2) return(-1);
    return(rtnlen);
}

static asynStatus vxiFlush(void *pdrvPvt,asynUser *pasynUser)
{
    /*Nothing to do */
    return(asynSuccess);
}

static asynStatus vxiSetEos(void *pdrvPvt,asynUser *pasynUser,
    const char *eos,int eoslen)
{
    vxiLink *pvxiLink = (vxiLink *)pdrvPvt;

    if(vxi11Debug) printf("%s vxiSetEos eoslen %d\n",pvxiLink->portName,eoslen);
    if(eoslen>1 || eoslen<0) {
        errlogPrintf("%s vxiSetEos illegal eoslen %d\n",
            pvxiLink->portName,eoslen);
        return(asynError);
    }
    pvxiLink->eos = (eoslen==0) ? -1 : (int)(unsigned int)eos[0] ;
    return(asynSuccess);
}

static asynStatus vxiAddressedCmd(void *pdrvPvt,asynUser *pasynUser,
    const char *data, int length)
{
    vxiLink *pvxiLink = (vxiLink *)pdrvPvt;
    Device_Link lid;
    long status;
    int addr = pasynManager->getAddr(pasynUser);

    assert(pvxiLink);
    assert(data);
    if(vxi11Debug) printf("%s vxiAddressedCmd %s\n",pvxiLink->portName,data);
    if(!vxiSetDevLink(pvxiLink, addr, &lid)) return(-1);
    status = vxiWriteAddressed(pvxiLink,lid,(char *)data,length,pvxiLink->defTimeout);
    return(status);
}

static asynStatus vxiUniversalCmd(void *pdrvPvt, asynUser *pasynUser, int cmd)
{
    vxiLink *pvxiLink = (vxiLink *)pdrvPvt;
    long nout;
    char data[2];

    if(vxi11Debug) printf("%s vxiUniversalCmd\n",pvxiLink->portName);
    data[0] = cmd;
    data[1] = 0;
    nout = vxiWriteCmd(pvxiLink, data, 1);
    return( nout==1 ? 0 : -1 );
}

static asynStatus vxiIfc(void *pdrvPvt, asynUser *pasynUser)
{
    vxiLink *pvxiLink = (vxiLink *)pdrvPvt;
    int status = asynSuccess;
    enum clnt_stat clntStat;
    Device_DocmdParms devDocmdP;
    Device_DocmdResp devDocmdR;

    assert(pvxiLink);
    if(vxi11Debug) printf("%s vxiIfc\n",pvxiLink->portName);
    /* initialize devDocmdP */
    devDocmdP.lid = pvxiLink->serverAddr;
    devDocmdP.flags = 0;	/* no timeout on a locked gateway */
    devDocmdP.io_timeout = (u_long)(1000.0*pvxiLink->defTimeout);
    devDocmdP.lock_timeout = 0;
    devDocmdP.network_order = NETWORK_ORDER;
    devDocmdP.cmd = VXI_CMD_IFC;
    devDocmdP.datasize = 0;
    devDocmdP.data_in.data_in_len = 0;
    devDocmdP.data_in.data_in_val = NULL;
    /* initialize devDocmdR */
    memset((char *) &devDocmdR, 0, sizeof(Device_DocmdResp));
    /* RPC call */
    clntStat = clientCall(pvxiLink, device_docmd,
        (const xdrproc_t) xdr_Device_DocmdParms,(void *) &devDocmdP,
        (const xdrproc_t) xdr_Device_DocmdResp, (void *) &devDocmdR);
    if(clntStat != RPC_SUCCESS) {
	errlogPrintf("%s vxiIfc\n",pvxiLink->portName);
	status = asynError;
    } else if(devDocmdR.error != VXI_OK) {
	if(devDocmdR.error != VXI_IOTIMEOUT) {
	    errlogPrintf("%s vxiIfc %s\n",
                pvxiLink->portName, vxiError(devDocmdR.error));
        }
	status = asynError;
    }
    xdr_free((const xdrproc_t) xdr_Device_DocmdResp, (char *) &devDocmdR);
    return(status);
}

static asynStatus vxiRen(void *pdrvPvt,asynUser *pasynUser, int onOff)
{
    vxiLink *pvxiLink = (vxiLink *)pdrvPvt;
    int status = asynSuccess;
    enum clnt_stat clntStat;
    Device_DocmdParms devDocmdP;
    Device_DocmdResp devDocmdR;
    unsigned short data,netdata;

    assert(pvxiLink);
    if(vxi11Debug) printf("%s vxiRen\n",pvxiLink->portName);
    /* initialize devDocmdP */
    devDocmdP.lid = pvxiLink->serverAddr;
    devDocmdP.flags = 0;	/* no timeout on a locked gateway */
    devDocmdP.io_timeout = (u_long)(1000.0*pvxiLink->defTimeout);
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
    clntStat = clientCall(pvxiLink, device_docmd,
        (const xdrproc_t) xdr_Device_DocmdParms,(void *) &devDocmdP,
        (const xdrproc_t) xdr_Device_DocmdResp, (void *) &devDocmdR);
    if(clntStat != RPC_SUCCESS) {
	errlogPrintf("%s vxiRen\n", pvxiLink->portName);
	status = asynError;
    } else if(devDocmdR.error != VXI_OK) {
	if(devDocmdR.error != VXI_IOTIMEOUT) {
	    errlogPrintf("%s vxiRen %s\n", 
                pvxiLink->portName, vxiError(devDocmdR.error));
        }
	status = asynError;
    }
    xdr_free((const xdrproc_t) xdr_Device_DocmdResp, (char *) &devDocmdR);
    return(status);
}

static int vxiSrqStatus(void *pdrvPvt)
{
    vxiLink *pvxiLink = (vxiLink *)pdrvPvt;
    int status;
    assert(pvxiLink);
    if(vxi11Debug) printf("%s vxiSrqStatus\n",pvxiLink->portName);
    status = vxiBusStatus(pvxiLink, VXI_BSTAT_SRQ, pvxiLink->defTimeout);
    return(status);
}

static asynStatus vxiSrqEnable(void *pdrvPvt, int onOff)
{
    vxiLink *pvxiLink = (vxiLink *)pdrvPvt;
    asynStatus status = asynSuccess;
    enum clnt_stat clntStat;
    Device_EnableSrqParms devEnSrqP;
    Device_Error devErr;
    char handle[16];

    assert(pvxiLink);
    if(vxi11Debug) printf("%s vxiSrqEnable\n",pvxiLink->portName);
    devEnSrqP.lid = pvxiLink->serverAddr;
    if(onOff) {
        devEnSrqP.enable = TRUE;
        sprintf(handle, "%p", (void *) pvxiLink);
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
    clntStat = clientCall(pvxiLink, device_enable_srq,
        (const xdrproc_t) xdr_Device_EnableSrqParms,(void *) &devEnSrqP,
        (const xdrproc_t) xdr_Device_Error, (void *) &devErr);
    if(clntStat != RPC_SUCCESS) {
	errlogPrintf("%s vxiSrqEnable %s\n",
		     pvxiLink->portName, clnt_sperror(pvxiLink->rpcClient, ""));
	status = asynError;
    } else if(devErr.error != VXI_OK) {
	errlogPrintf("%s vxiSrqEnable %s\n",
		     pvxiLink->portName, vxiError(devErr.error));
	status = asynError;
    }
    xdr_free((const xdrproc_t) xdr_Device_Error, (char *) &devErr);
    return(status);
}

static asynStatus vxiSerialPollBegin(void *pdrvPvt)
{
    return asynSuccess;
}

static int vxiSerialPoll(void *pdrvPvt, int addr, double timeout)
{
    vxiLink *pvxiLink = (vxiLink *)pdrvPvt;
    enum clnt_stat clntStat;
    Device_GenericParms devGenP;
    Device_ReadStbResp devGenR;
    unsigned char stb;

    assert(pvxiLink);
    if(vxi11Debug) printf("%s vxiSerialPoll addr %d\n",pvxiLink->portName,addr);
    if(!vxiSetDevLink(pvxiLink, addr, &devGenP.lid)) return -1;
    devGenP.flags = 0;	/* no timeout on a locked gateway */
    devGenP.io_timeout = (u_long)(1000.0*pvxiLink->defTimeout);
    devGenP.lock_timeout = 0;
    /* initialize devGenR */
    memset((char *) &devGenR, 0, sizeof(Device_ReadStbResp));
    clntStat = clientCall(pvxiLink, device_readstb,
        (const xdrproc_t) xdr_Device_GenericParms, (void *) &devGenP,
        (const xdrproc_t) xdr_Device_ReadStbResp, (void *) &devGenR);
    if(clntStat != RPC_SUCCESS) {
	errlogPrintf("%s vxiSerialPoll %d %s\n",
            pvxiLink->portName, addr, clnt_sperror(pvxiLink->rpcClient,""));
	return(-1);
    } else if(devGenR.error != VXI_OK) {
	if(devGenR.error != VXI_IOTIMEOUT) {
	    errlogPrintf("%s vxiSerialPoll %d: %s\n",
                pvxiLink->portName, addr, vxiError(devGenR.error));
        }
	return(-1);
    } else {
	stb = devGenR.stb;
    }
    xdr_free((const xdrproc_t) xdr_Device_ReadStbResp, (char *) &devGenR);
    return(stb);
}

static asynStatus vxiSerialPollEnd(void *pdrvPvt)
{
    return asynSuccess;
}

static asynGpibPort vxi11 = {
    vxiReport,
    vxiConnect,
    vxiDisconnect,
    vxiRead,
    vxiWrite,
    vxiFlush,
    vxiSetEos,
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
    return(0);
}

int vxi11Config(char *dn, char *hostName, int recoverWithIFC,
    double defTimeout,
    char *vxiName,
    unsigned int priority,unsigned int stackSize,
    int isSingleLink)
{
    char *portName;
    vxiLink *pvxiLink;
    struct sockaddr_in ip;
    struct in_addr inAddr;

    assert(dn && hostName && vxiName);
    /* Force registration */
    if(vxiInit() != 0) return -1;
    portName = callocMustSucceed(strlen(dn)+1,sizeof(char),
        "vxi11Config");
    strcpy(portName,dn);
    if(aToIPAddr(hostName, 0, &ip) < 0) {
        errlogPrintf("%s Unknown host: \"%s\"\n", portName, hostName);
        return(0);
    }
    inAddr.s_addr = ip.sin_addr.s_addr;
    /* allocate vxiLink structure */
    pvxiLink = (vxiLink *)callocMustSucceed(1,sizeof(vxiLink),"vxi11Config");
    pvxiLink->portName = portName;
    pvxiLink->eos = -1;
    assert(strlen(vxiName)<sizeof(pvxiLink->vxiName));
    strcpy(pvxiLink->vxiName, vxiName);
    pvxiLink->defTimeout = (defTimeout>.0001) ? 
        defTimeout : (double)DEFAULT_RPC_TIMEOUT ;
    if(recoverWithIFC) pvxiLink->recoverWithIFC = TRUE;
    pvxiLink->inAddr = inAddr;
    pvxiLink->hostName = (char *)callocMustSucceed(1,strlen(hostName)+1,
        "vxi11Config");
    pvxiLink->isSingleLink = (isSingleLink ? 1 : 0 );
    strcpy(pvxiLink->hostName, hostName);
    pvxiLink->asynGpibPvt = pasynGpib->registerPort(pvxiLink->portName,
        &vxi11,pvxiLink,priority,stackSize);
    return 0;
}

/*
 * IOC shell command registration
 */
#include <iocsh.h>
static const iocshArg vxi11ConfigArg0 = { "portName",iocshArgString};
static const iocshArg vxi11ConfigArg1 = { "host name",iocshArgString};
static const iocshArg vxi11ConfigArg2 = { "recover with IFC?",iocshArgInt};
static const iocshArg vxi11ConfigArg3 = { "default timeout",iocshArgDouble};
static const iocshArg vxi11ConfigArg4 = { "vxiName",iocshArgString};
static const iocshArg vxi11ConfigArg5 = { "priority",iocshArgInt};
static const iocshArg vxi11ConfigArg6 = { "stackSize",iocshArgInt};
static const iocshArg vxi11ConfigArg7 = { "isSingleLink",iocshArgInt};
static const iocshArg *vxi11ConfigArgs[] = {&vxi11ConfigArg0,
    &vxi11ConfigArg1, &vxi11ConfigArg2, &vxi11ConfigArg3,
    &vxi11ConfigArg4, &vxi11ConfigArg5, &vxi11ConfigArg6,
    &vxi11ConfigArg7};
static const iocshFuncDef vxi11ConfigFuncDef = {"vxi11Config",8,vxi11ConfigArgs};
static void vxi11ConfigCallFunc(const iocshArgBuf *args)
{
    char *portName = args[0].sval;
    char *hostName = args[1].sval;
    int recoverWithIFC = args[2].ival;
    double defTimeout = args[3].dval;
    char *vxiName = args[4].sval;
    int priority = args[5].ival;
    int stackSize = args[6].ival;
    int isSingleLink = args[7].ival;

    vxi11Config (portName, hostName, recoverWithIFC,
        defTimeout, vxiName, priority, stackSize, isSingleLink);
}

extern int E5810Reboot(char * inetAddr,char *password);
extern int E2050Reboot(char * inetAddr);

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
        iocshRegister(&vxi11ConfigFuncDef,vxi11ConfigCallFunc);
        iocshRegister(&E2050RebootFuncDef,E2050RebootCallFunc);
        iocshRegister(&E5810RebootFuncDef,E5810RebootCallFunc);
        iocshRegister(&vxi11SetRpcTimeoutFuncDef,vxi11SetRpcTimeoutCallFunc);
    }
}
epicsExportRegistrar(vxi11RegisterCommands);
