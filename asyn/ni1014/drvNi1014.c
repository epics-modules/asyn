/***********************************************************************
* Copyright (c) 2002 The University of Chicago, as Operator of Argonne
* National Laboratory, and the Regents of the University of
* California, as Operator of Los Alamos National Laboratory, and
* Berliner Elektronenspeicherring-Gesellschaft m.b.H. (BESSY).
* gpibCore is distributed subject to a Software License Agreement
* found in file LICENSE that is included with this distribution.
***********************************************************************/

/*
 * Author: Marty Kraimer
 *   Date: 2004.5.20
 * This is major revision of code originally written by John Winans.
 */

#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdio.h>

#include <epicsTypes.h>
#include <epicsEvent.h>
#include <epicsThread.h>
#include <epicsInterrupt.h>
#include <errlog.h>
#include <iocsh.h>
#include <callback.h>
#include <cantProceed.h>
#include <epicsTime.h>
#include <devLib.h>
#include <taskwd.h>

#include <epicsExport.h>
#include "asynDriver.h"
#include "asynOctet.h"
#include "asynGpibDriver.h"

int ni1014Debug = 0;
#define ERROR_MESSAGE_BUFFER_SIZE 160

typedef enum {
    transferStateIdle, transferStateRead, transferStateWrite, transferStateCmd
} transferState_t;

typedef struct niport niport;
struct niport {
    char        *portName;
    void        *asynGpibPvt;
    int         isPortA;
    epicsUInt32 base;
    volatile epicsUInt8 *registers;
    int         vector;
    int         level;
    CALLBACK    callback;
    epicsUInt8  isr1; 
    epicsUInt8  isr2;
    int         srqEnabled;
    transferState_t transferState;
    transferState_t nextTransferState;
    /*bytesRemaining and nextByte are used by interruptHandler*/
    int         bytesRemainingCmd;
    const char  *nextByteCmd;
    int         bytesRemainingWrite;
    const char  *nextByteWrite;
    int         bytesRemainingRead;
    char        *nextByteRead;
    int         eomReason;
    int         eos; /* -1 means no end of string character*/
    asynStatus  status; /*status of ip transfer*/
    epicsEventId waitForInterrupt;
    char errorMessage[ERROR_MESSAGE_BUFFER_SIZE];
};

static epicsUInt8 readRegister(niport *pniport, int offset);
static void writeRegister(niport *pniport,int offset, epicsUInt8 value);
static void printStatus(niport *pniport,const char *source);
/* routine to wait for io completion*/
static void waitTimeout(niport *pniport,double seconds);
/* Interrupt Handlers */
void ni1014Err(void *pvt);
static void srqCallback(CALLBACK *pcallback);
void ni1014(void *pvt);
/*Routines that with interrupt handler perform I/O*/
static asynStatus writeCmd(niport *pniport,const char *buf, int cnt,
    double timeout,transferState_t nextState);
static asynStatus writeAddr(niport *pniport,int talk, int listen,
    double timeout,transferState_t nextState);
static asynStatus writeGpib(niport *pniport,const char *buf, int cnt,
    int *actual, int addr, double timeout);
static asynStatus readGpib(niport *pniport,char *buf, int cnt, int *actual,
    int addr, double timeout,int *eomReason);
/* asynGpibPort methods */
static void gpibPortReport(void *pdrvPvt,FILE *fd,int details);
static asynStatus gpibPortConnect(void *pdrvPvt,asynUser *pasynUser);
static asynStatus gpibPortDisconnect(void *pdrvPvt,asynUser *pasynUser);
static asynStatus gpibPortRead(void *pdrvPvt,asynUser *pasynUser,
    char *data,int maxchars,int *nbytesTransfered,int *eomReason);
static asynStatus gpibPortWrite(void *pdrvPvt,asynUser *pasynUser,
    const char *data,int numchars,int *nbytesTransfered);
static asynStatus gpibPortFlush(void *pdrvPvt,asynUser *pasynUser);
static asynStatus gpibPortSetEos(void *pdrvPvt,asynUser *pasynUser,
    const char *eos,int eoslen);
static asynStatus gpibPortGetEos(void *pdrvPvt,asynUser *pasynUser,
    char *eos, int eossize, int *eoslen);
static asynStatus gpibPortAddressedCmd(void *pdrvPvt,asynUser *pasynUser,
    const char *data, int length);
static asynStatus gpibPortUniversalCmd(void *pdrvPvt, asynUser *pasynUser, int cmd);
static asynStatus gpibPortIfc(void *pdrvPvt, asynUser *pasynUser);
static asynStatus gpibPortRen(void *pdrvPvt,asynUser *pasynUser, int onOff);
static asynStatus gpibPortSrqStatus(void *pdrvPvt,int *isSet);
static asynStatus gpibPortSrqEnable(void *pdrvPvt, int onOff);
static asynStatus gpibPortSerialPollBegin(void *pdrvPvt);
static asynStatus gpibPortSerialPoll(void *pdrvPvt, int addr,
    double timeout,int *status);
static asynStatus gpibPortSerialPollEnd(void *pdrvPvt);

static asynGpibPort gpibPort = {
    gpibPortReport,
    gpibPortConnect,
    gpibPortDisconnect,
    gpibPortRead,
    gpibPortWrite,
    gpibPortFlush,
    gpibPortSetEos,
    gpibPortGetEos,
    gpibPortAddressedCmd,
    gpibPortUniversalCmd,
    gpibPortIfc,
    gpibPortRen,
    gpibPortSrqStatus,
    gpibPortSrqEnable,
    gpibPortSerialPollBegin,
    gpibPortSerialPoll,
    gpibPortSerialPollEnd
};

/* Register definitions */
/* All registers will be addressed as 16 bit integers */
#define PORT_REGISTER_SIZE 0x200

#define CCR0    0x007 /*Channel Control Register*/
#define CSR1    0x040 /*Channel Status Register*/
#define DCR1    0x044 /*Device Control Register*/
#define CCR1    0x047 /*Channel Control Register*/
#define EINT    0x08  /*Enable Interrupts       */
#define NIVR1   0x065 /*Normal Interrupt Vector*/
#define EIVR1   0x067 /*Error Interrupt Vector*/

#define CFG1    0x101 /*Configuration Register 1*/
#define CFG2    0x105 /*Configuration Register 2*/
#define GPIBSR  0x101 /*GPIB Status Register*/

#define GPIBSRSRQ 0x20 /*SRQ is asserted*/

#define SFL     0x08  /*System Fail Bit*/
#define SUP     0x04  /*Supervisor Bit*/
#define LMR     0x02  /*Local Master Reset Bit*/
#define SC      0x01  /*System Controller Bit*/

/* TLC (Talker Listener Control register */
#define DIR     0x111 /*Data In Register*/
#define CDOR    0x111 /*Command/Data Out Register*/
#define ISR1    0x113 /*Interrupt Status Register 1*/
#define IMR1    0x113 /*Interrupt Mask Register 1*/
#define ISR2    0x115 /*Interrupt Status Register 2*/
#define IMR2    0x115 /*Interrupt Mask Register 2*/
#define ADSR    0x119 /*Address Status Register*/
#define ADMR    0x119 /*Address Mode Register*/
#define CPTR    0x11B  /*Command Pass Through Register*/
#define AUXMR   0x11B  /*Auxiliary Mode Register*/

/* ISR1 IMR1*/
#define ENDRX   0x10  /*End Received Bit */
#define ERR     0x04  /*Error Bit */
#define ERRIE   0x04  /*Error Interrupt Enable Bit */
#define DO      0x02  /*Data Out Bit */
#define DOIE    0x02  /*Data Out Interrupt Enable Bit */
#define DO      0x02  /*Data Out Bit */
#define DOIE    0x02  /*Data Out Interrupt Enable Bit */
#define DI      0x01  /*Data In Bit */
#define DIIE    0x01  /*Data In Interrupt Enable Bit */

/* ISR2 IMR2*/
#define SRQI    0x40  /*Service Request Input Bit */
#define SRQIE   0x40  /*Service Request Input Interrupt Enable Bit */
#define REM     0x10  /*Remote Bit */
#define CO      0x08  /*Command Out Bit */
#define COIE    0x08  /*Command Out Interrupt Enable Bit */

/* ADMR */
#define TRM     0x30  /*Transmit/Receive Mode Bits. Leave both on*/
#define MODE1   0x01  /*Address mod 1. Normal Dual addressing*/

/*AUXMR*/
#define AUXPON  0x00    /*Immediate Execute PON*/
#define AUXCR   0x02    /*Chip Reset*/
#define AUXFH   0x03    /*Finish Handshake*/
#define AUXEOI  0x06    /*Send EOI*/
#define AUXGTS  0x10    /*Go To Standby*/
#define AUXTCA  0x11    /*Take Control Asynchronously*/
#define AUXTCS  0x12    /*Take Control Asynchronously*/
#define AUXSIFC 0x1E    /*Set Interface Clear*/
#define AUXCIFC 0x16    /*Clear Interface Clear*/
#define AUXSREN 0x1F    /*Set Remote Enable*/
#define AUXCREN 0x17    /*Clear Remote Enable*/
#define AUXICR  0x20   /*Internal Counter Clock*/
#define AUXPPR  0x50   /*Parallel Poll Register*/
#define AUXRA   0x80   /*Auxilary Register A*/

#define PPRU    0x10   /*AUXPPR Unconfigure*/
#define RABIN   0x10   /*AUXRA BIN, i.e. EOS is 8 bit*/
#define RABHLDA 0x01   /*AUXRA RFD holdoff on All Data*/
#define ADR     0x11D  /*Write address*/
#define ARS     0x80   /* (0,1)=>(primary,secondary) */
#define DT      0x40   /* Disable Talker*/
#define DL      0x20   /* Disable Listener*/

#define EOSR    0x11F  /*End Of String Register*/

/*
 * Must wait 5 ti9914 clock cycles between AUXMR commands
 * Documentation is confusing but experiments indicate that 6 microsecod wait
 * Is required.
*/

static long nloopsPerMicrosecond = 0;

static void wasteTime(int nloops)
{
    volatile int n = nloops;
    while(n>0) n--;
}

static void microSecondDelay(int n)
{
    volatile long nloops = n * nloopsPerMicrosecond;
    while(nloops>0) nloops--;
}

/* The following assumes that nothing else is using large amounts of
 * cpu time between the calls to epicsTimeGetCurrent.
*/

static void initAuxmrWait() {
    epicsTimeStamp stime,etime;
    double elapsedTime = 0.0;
    int ntimes = 1;
    static int nloops = 1000000;
    double totalLoops;

    while(elapsedTime<1.0) {
        int i;
        ntimes *= 2;
        epicsTimeGetCurrent(&stime);
        for(i=0; i<ntimes; i++) wasteTime(nloops);
        epicsTimeGetCurrent(&etime);
        elapsedTime = epicsTimeDiffInSeconds(&etime,&stime);
    }
    totalLoops = ((double)ntimes) * ((double)nloops);
    nloopsPerMicrosecond = (totalLoops/elapsedTime)/1e6 + .99;
    if(ni1014Debug) {
        printf("totalLoops %f elapsedTime %f nloopsPerMicrosecond %ld\n",
             totalLoops,elapsedTime,nloopsPerMicrosecond);
        epicsTimeGetCurrent(&stime);
        microSecondDelay(1000000);
        epicsTimeGetCurrent(&etime);
        elapsedTime = epicsTimeDiffInSeconds(&etime,&stime);
        printf("elapsedTime %f\n",elapsedTime);
    }
}

static epicsUInt8 readRegister(niport *pniport, int offset)
{
    volatile epicsUInt8 *pregister = (epicsUInt8 *)
                             (((char *)pniport->registers)+offset);
    epicsUInt8 value;
    
    value = *pregister;
    if(ni1014Debug) {
        char message[100];

        sprintf(message,"readRegister pregister %p offset %x value %2.2x\n",
            pregister,offset,value);
        if(epicsInterruptIsInterruptContext()) {
            epicsInterruptContextMessage(message);
        } else {
            printf("%s",message);
        }
    }
    return value;
}

static void writeRegister(niport *pniport,int offset, epicsUInt8 value)
{
    volatile epicsUInt8 *pregister = (epicsUInt8 *)
                             (((char *)pniport->registers)+offset);

    if(ni1014Debug) {
        char message[100];

        sprintf(message,"writeRegister pregister %p offset %x value %2.2x\n",
            pregister,offset,value);
        if(epicsInterruptIsInterruptContext()) {
            epicsInterruptContextMessage(message);
        } else {
            printf("%s",message);
        }
    }
    *pregister = value;

    /*
     * Must wait 5 ti9914 clock cycles between AUXMR commands
     * Documentation is confusing but experiments indicate that 6 microsecod wait
     * Is required.
     */
    if (offset == AUXMR)
        microSecondDelay(6);
}

static void printStatus(niport *pniport, const char *source)
{
    sprintf(pniport->errorMessage, "%s "
        "isr1 %2.2x isr2 %2.2x ADSR %2.2x\n",
        source, pniport->isr1,pniport->isr2,readRegister(pniport,ADSR));
}

static void waitTimeout(niport *pniport,double seconds)
{
    epicsEventWaitStatus status;
    transferState_t saveState;

    if(seconds<0.0) {
        status = epicsEventWait(pniport->waitForInterrupt);
    } else {
        status = epicsEventWaitWithTimeout(pniport->waitForInterrupt,seconds);
    }
    if(status==epicsEventWaitOK) return;
    saveState = pniport->transferState;
    pniport->transferState = transferStateIdle;
    switch(saveState) {
    case transferStateRead:
        pniport->status=asynTimeout;
        printStatus(pniport,"waitTimeout transferStateRead\n");
        break;
    case transferStateWrite:
        pniport->status=asynTimeout;
        printStatus(pniport,"waitTimeout transferStateWrite\n");
        break;
    case transferStateCmd:
        pniport->status=asynTimeout;
        printStatus(pniport,"waitTimeout transferStateCmd\n");
        break;
    default:
        pniport->status=asynTimeout;
        printStatus(pniport,"waitTimeout transferState ?\n");
    }
}

void ni1014Err(void *pvt)
{
    niport *pniport = (niport *)pvt;
    char message[50];

    sprintf(message,"%s errorInterrupt\n",pniport->portName);
    epicsInterruptContextMessage(message);
    ni1014(pvt);
}

static void srqCallback(CALLBACK *pcallback)
{
    niport *pniport;

    callbackGetUser(pniport,pcallback);
    if(!pniport->srqEnabled) return;
    pasynGpib->srqHappened(pniport->asynGpibPvt);
}

void ni1014(void *pvt)
{
    niport          *pniport = (niport *)pvt;
    transferState_t state = pniport->transferState;
    epicsUInt8      isr1,isr2,octet;
    char            message[80];

    pniport->isr2 = isr2 = readRegister(pniport,ISR2);
    pniport->isr1 = isr1 = readRegister(pniport,ISR1);
    writeRegister(pniport,CSR1,2); /*acknowledge interrupt*/
    if(isr2&SRQI) callbackRequest(&pniport->callback);
    if(isr1&ERR) {
        if(state!=transferStateIdle) {
            sprintf(pniport->errorMessage,"\n%s interruptHandler ERR state %d\n",
                pniport->portName,state);
            pniport->status = asynError;
            epicsEventSignal(pniport->waitForInterrupt);
        }
        goto exit;
    }
    switch(state) {
    case transferStateCmd:
        if(!isr2&CO)
            goto exit;
        if(pniport->bytesRemainingCmd == 0) {
            pniport->transferState = pniport->nextTransferState;
            if(pniport->transferState==transferStateIdle) {
                epicsEventSignal(pniport->waitForInterrupt);
            } else {
                writeRegister(pniport,AUXMR,AUXGTS);
            }
            break ;
        }
        octet = *pniport->nextByteCmd;
        writeRegister(pniport,CDOR,(epicsUInt8)octet);
        --(pniport->bytesRemainingCmd); ++(pniport->nextByteCmd);
        break;
    case transferStateWrite:
        if(!isr1&DO)
            goto exit;
        if(pniport->bytesRemainingWrite == 0) {
            pniport->transferState = transferStateIdle;
            writeRegister(pniport,AUXMR,AUXTCA);
            epicsEventSignal(pniport->waitForInterrupt);
            break ;
        }
        if(pniport->bytesRemainingWrite==1) writeRegister(pniport,AUXMR,AUXEOI);
        octet = *pniport->nextByteWrite;
        writeRegister(pniport,CDOR,(epicsUInt8)octet);
        --(pniport->bytesRemainingWrite); ++(pniport->nextByteWrite);
        break;
    case transferStateRead:
        if(!isr1&DI) break;
        octet = readRegister(pniport,DIR);
        *pniport->nextByteRead = octet;
        --(pniport->bytesRemainingRead); ++(pniport->nextByteRead);
        if((pniport->eos != -1 ) && (octet == pniport->eos))
            pniport->eomReason |= ASYN_EOM_EOS;
        if(ENDRX&isr1) pniport->eomReason |= ASYN_EOM_END;
        if(pniport->bytesRemainingRead == 0) pniport->eomReason |= ASYN_EOM_CNT;
        if(pniport->eomReason) {
            pniport->transferState = transferStateIdle;
            writeRegister(pniport,AUXMR,AUXTCS);
            epicsEventSignal(pniport->waitForInterrupt);
            break;
        }
        writeRegister(pniport,AUXMR,AUXFH);
        break;
    case transferStateIdle:
        if(!isr1&DI)
            goto exit;
        octet = readRegister(pniport,DIR);
        sprintf(message,"%s ni1014IH transferStateIdle received %2.2x\n",
             pniport->portName,octet);
        epicsInterruptContextMessage(message);
    }

exit:
    /* Force synchronization of VMEbus writes on PPC CPU boards. */
    readRegister(pniport,ADSR);
}

static asynStatus writeCmd(niport *pniport,const char *buf, int cnt,
    double timeout,transferState_t nextState)
{
    epicsUInt8 isr2 = pniport->isr2;

    writeRegister(pniport,AUXMR,AUXTCA);
    if(!isr2&CO) {
        printStatus(pniport,"writeCmd !isr2&CO\n");
        return asynTimeout;
    }
    pniport->bytesRemainingCmd = cnt-1;
    pniport->nextByteCmd = (buf+1);
    pniport->transferState = transferStateCmd;
    pniport->nextTransferState = nextState;
    pniport->status = asynSuccess;
    writeRegister(pniport,CDOR,buf[0]);
    waitTimeout(pniport,timeout);
    return pniport->status;
}

static asynStatus writeAddr(niport *pniport,int talk, int listen,
    double timeout,transferState_t nextState)
{
    char cmdbuf[4];
    int        lenCmd = 0;
    int        primary,secondary;

    if(talk<0) {
        ; /*do nothing*/
    } else if(talk<100) {
        cmdbuf[lenCmd++] = talk + TADBASE;
    } else {
        primary = talk / 100; secondary = talk % 100;
        cmdbuf[lenCmd++] = primary + TADBASE;
        cmdbuf[lenCmd++] = secondary + SADBASE;
    }
    if(listen<0) {
        ; /*do nothing*/
    } else if(listen<100) {
        cmdbuf[lenCmd++] = listen + LADBASE;
    } else {
        primary = listen / 100; secondary = listen % 100;
        cmdbuf[lenCmd++] = primary + LADBASE;
        cmdbuf[lenCmd++] = secondary + SADBASE;
    }
    return writeCmd(pniport,cmdbuf,lenCmd,timeout,nextState);
}

static asynStatus writeGpib(niport *pniport,const char *buf, int cnt,
    int *actual, int addr, double timeout)
{
    char cmdbuf[2] = {IBUNT,IBUNL};
    asynStatus status;

    *actual=0;
    pniport->bytesRemainingWrite = cnt;
    pniport->nextByteWrite = buf;
    pniport->status = asynSuccess;
    status = writeAddr(pniport,0,addr,timeout,transferStateWrite);
    if(status!=asynSuccess) return status;
    *actual = cnt - pniport->bytesRemainingWrite;
    status = pniport->status;
    if(status!=asynSuccess) return status;
    writeCmd(pniport,cmdbuf,2,timeout,transferStateIdle);
    return status;
}

asynStatus readGpib(niport *pniport,char *buf, int cnt, int *actual,
    int addr, double timeout,int *eomReason)
{
    char cmdbuf[2] = {IBUNT,IBUNL};
    asynStatus status;
    epicsUInt8 isr1 = pniport->isr1;

    *actual=0; *buf=0;
    pniport->bytesRemainingRead = cnt;
    pniport->nextByteRead = buf;
    pniport->eomReason = 0;
    if(isr1&DI) {
        pniport->bytesRemainingRead--;
        pniport->nextByteRead++;
        buf[0] = readRegister(pniport,DIR);
    }
    writeRegister(pniport,AUXMR,AUXFH);
    pniport->status = asynSuccess;
    status = writeAddr(pniport,addr,0,timeout,transferStateRead);
    if(status!=asynSuccess) return status;
    *actual = cnt - pniport->bytesRemainingRead;
    if(eomReason) *eomReason = pniport->eomReason;
    writeCmd(pniport,cmdbuf,2,timeout,transferStateIdle);
    return status;
}

static void gpibPortReport(void *pdrvPvt,FILE *fd,int details)
{
    niport *pniport = (niport *)pdrvPvt;

    fprintf(fd,"    gpibPort port %s vector %d base %x registers %p\n",
        pniport->portName,pniport->vector,pniport->base,pniport->registers);
}

static asynStatus gpibPortConnect(void *pdrvPvt,asynUser *pasynUser)
{
    niport *pniport = (niport *)pdrvPvt;
    int addr = 0;
    asynStatus status;

    status = pasynManager->getAddr(pasynUser,&addr);
    if(status!=asynSuccess) return status;
    asynPrint(pasynUser, ASYN_TRACE_FLOW,
        "%s addr %d gpibPortConnect\n",pniport->portName,addr);
    if(addr>=0) {
        pasynManager->exceptionConnect(pasynUser);
        return asynSuccess;
    }
    if(pniport->isPortA) {
        writeRegister(pniport,CFG2,LMR);    /*Local Master Reset*/
        epicsThreadSleep(.01);    /* Wait at least 10 msec before continuing */
        writeRegister(pniport,CFG2,0);    /*Clear Local Master Reset*/
        epicsThreadSleep(.01);
    }
    writeRegister(pniport,AUXMR,AUXCR); /*Chip Reset*/
    epicsThreadSleep(.01);
    writeRegister(pniport,CFG2,SFL|SC);    /*Set System Controller*/
    writeRegister(pniport,CFG1,(pniport->level<<5));
    writeRegister(pniport,IMR1,0);
    writeRegister(pniport,IMR2,0);
    readRegister(pniport,CPTR);
    readRegister(pniport,ISR1);
    readRegister(pniport,ISR2);
    /*DMAC setup*/
    writeRegister(pniport,CCR0,0);
    writeRegister(pniport,CCR1,0);
    writeRegister(pniport,NIVR1,pniport->vector);
    writeRegister(pniport,EIVR1,pniport->vector+1);
    /* 7210 TLC setup */
    writeRegister(pniport,ADMR,(MODE1|TRM));
    writeRegister(pniport,ADR,0);
    writeRegister(pniport,ADR,ARS|DT|DL);
    writeRegister(pniport,EOSR,0);
    writeRegister(pniport,AUXMR,AUXPON);
    writeRegister(pniport,AUXMR,AUXICR|0x8);
    writeRegister(pniport,AUXMR,AUXPPR|PPRU);
    writeRegister(pniport,AUXMR,AUXSIFC);
    epicsThreadSleep(.01);
    writeRegister(pniport,AUXMR,AUXCIFC);
    writeRegister(pniport,AUXMR,AUXRA|RABIN|RABHLDA);
    /*Enable TLC interrupts*/
    writeRegister(pniport,AUXMR,AUXSREN);
    writeRegister(pniport,DCR1,1);
    writeRegister(pniport,CCR1,EINT);
    writeRegister(pniport,IMR1,ERRIE|DOIE|DIIE);
    writeRegister(pniport,IMR2,COIE|(pniport->srqEnabled ? SRQIE : 0));
    writeRegister(pniport,AUXMR,AUXTCA);
    pasynManager->exceptionConnect(pasynUser);
    return asynSuccess;
}

static asynStatus gpibPortDisconnect(void *pdrvPvt,asynUser *pasynUser)
{
    niport *pniport = (niport *)pdrvPvt;
    int addr = 0;
    asynStatus status;

    status = pasynManager->getAddr(pasynUser,&addr);
    if(status!=asynSuccess) return status;
    asynPrint(pasynUser, ASYN_TRACE_FLOW,
        "%s addr %d gpibPortDisconnect\n",pniport->portName,addr);
    if(addr>=0) {
        pasynManager->exceptionDisconnect(pasynUser);
        return asynSuccess;
    }
    writeRegister(pniport,IMR1,0);
    writeRegister(pniport,IMR2,0);
    pasynManager->exceptionDisconnect(pasynUser);
    return asynSuccess;
}

static asynStatus gpibPortRead(void *pdrvPvt,asynUser *pasynUser,
    char *data,int maxchars,int *nbytesTransfered,int *eomReason)
{
    niport *pniport = (niport *)pdrvPvt;
    int        actual = 0;
    double     timeout = pasynUser->timeout;
    int        addr = 0;
    asynStatus status;

    status = pasynManager->getAddr(pasynUser,&addr);
    if(status!=asynSuccess) return status;
    asynPrint(pasynUser,ASYN_TRACE_FLOW,"%s addr %d gpibPortRead\n",
        pniport->portName,addr);
    pniport->errorMessage[0] = 0;
    status = readGpib(pniport,data,maxchars,&actual,addr,timeout,eomReason);
    if(status!=asynSuccess) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
            "%s readGpib failed %s",pniport->portName,pniport->errorMessage);
    }
    asynPrintIO(pasynUser,ASYN_TRACEIO_DRIVER,
        data,actual,"%s addr %d gpibPortRead\n",pniport->portName,addr);
    *nbytesTransfered = actual;
    return status;
}

static asynStatus gpibPortWrite(void *pdrvPvt,asynUser *pasynUser,
    const char *data,int numchars,int *nbytesTransfered)
{
    niport *pniport = (niport *)pdrvPvt;
    int        actual = 0;
    double     timeout = pasynUser->timeout;
    int        addr = 0;
    asynStatus status;

    status = pasynManager->getAddr(pasynUser,&addr);
    if(status!=asynSuccess) return status;
    asynPrint(pasynUser,ASYN_TRACE_FLOW,"%s addr %d gpibPortWrite nchar %d\n",
        pniport->portName,addr,numchars);
    pniport->errorMessage[0] = 0;
    status = writeGpib(pniport,data,numchars,&actual,addr,timeout);
    if(status!=asynSuccess) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
            "%s writeGpib failed %s",pniport->portName,pniport->errorMessage);
    } else if(actual!=numchars) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
            "%s requested %d but sent %d bytes",pniport->portName,numchars,actual);
        status = asynError;
    }
    asynPrintIO(pasynUser,ASYN_TRACEIO_DRIVER,
        data,actual,"%s addr %d gpibPortWrite\n",pniport->portName,addr);
    *nbytesTransfered = actual;
    return status;
}

static asynStatus gpibPortFlush(void *pdrvPvt,asynUser *pasynUser)
{
    /*Nothing to do */
    return asynSuccess;
}

static asynStatus gpibPortSetEos(void *pdrvPvt,asynUser *pasynUser,
    const char *eos,int eoslen)
{
    niport     *pniport = (niport *)pdrvPvt;
    int        addr = 0;
    asynStatus status;

    status = pasynManager->getAddr(pasynUser,&addr);
    if(status!=asynSuccess) return status;
    asynPrint(pasynUser, ASYN_TRACE_FLOW,
        "%s addr %d gpibPortSetEos eoslen %d\n",pniport->portName,addr,eoslen);
    if(eoslen>1 || eoslen<0) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
           "%s addr %d gpibTi9914SetEos illegal eoslen %d\n",
           pniport->portName,addr,eoslen);
        return asynError;
    }
    pniport->eos = (eoslen==0) ? -1 : (int)(unsigned int)eos[0] ;
    return asynSuccess;
}
static asynStatus gpibPortGetEos(void *pdrvPvt,asynUser *pasynUser,
    char *eos, int eossize, int *eoslen)
{
    niport *pniport = (niport *)pdrvPvt;
    int        addr = 0;
    asynStatus status;

    status = pasynManager->getAddr(pasynUser,&addr);
    if(status!=asynSuccess) return status;
    if(eossize<1) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "%s addr %d gpibPortGetEos eossize %d too small\n",
            pniport->portName,addr,eossize);
        *eoslen = 0;
        return asynError;
    }
    if(pniport->eos==-1) {
        *eoslen = 0;
    } else {
        eos[0] = (unsigned int)pniport->eos;
        *eoslen = 1;
    }
    asynPrintIO(pasynUser, ASYN_TRACE_FLOW, eos, *eoslen,
        "%s addr %d gpibPortGetEos eoslen %p\n",pniport->portName,addr,eoslen);
    return asynSuccess;
}

static asynStatus gpibPortAddressedCmd(void *pdrvPvt,asynUser *pasynUser,
    const char *data, int length)
{
    niport *pniport = (niport *)pdrvPvt;
    double     timeout = pasynUser->timeout;
    int        addr = 0;
    int        actual;
    asynStatus status;
    char cmdbuf[2] = {IBUNT,IBUNL};

    status = pasynManager->getAddr(pasynUser,&addr);
    if(status!=asynSuccess) return status;
    asynPrint(pasynUser,ASYN_TRACE_FLOW,
       "%s addr %d gpibPortAddressedCmd nchar %d\n",
        pniport->portName,addr,length);
    pniport->errorMessage[0] = 0;
    status = writeAddr(pniport,0,addr,timeout,transferStateIdle);
    if(status==asynSuccess) {
        status=writeCmd(pniport,data,length,timeout,transferStateIdle);
    }
    if(status!=asynSuccess) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
            "%s writeGpib failed %s",pniport->portName,pniport->errorMessage);
    }
    actual = length - pniport->bytesRemainingCmd;
    asynPrintIO(pasynUser,ASYN_TRACEIO_DRIVER,
        data,actual,"%s gpibPortAddressedCmd\n",pniport->portName);
    if(status!=asynSuccess) return status;
    writeCmd(pniport,cmdbuf,2,timeout,transferStateIdle);
    return status;
}

static asynStatus gpibPortUniversalCmd(void *pdrvPvt, asynUser *pasynUser, int cmd)
{
    niport *pniport = (niport *)pdrvPvt;
    double     timeout = pasynUser->timeout;
    asynStatus status;
    char   buffer[1];

    asynPrint(pasynUser,ASYN_TRACE_FLOW,"%s gpibPortUniversalCmd %2.2x\n",
        pniport->portName,cmd);
    pniport->errorMessage[0] = 0;
    buffer[0] = cmd;
    status = writeCmd(pniport,buffer,1,timeout,transferStateIdle);
    if(status!=asynSuccess) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
            "%s writeGpib failed %s",pniport->portName,pniport->errorMessage);
    }
    asynPrintIO(pasynUser,ASYN_TRACEIO_DRIVER,
        buffer,1,"%s gpibPortUniversalCmd\n",pniport->portName);
    return status;
}

static asynStatus gpibPortIfc(void *pdrvPvt, asynUser *pasynUser)
{
    niport *pniport = (niport *)pdrvPvt;

    asynPrint(pasynUser, ASYN_TRACE_FLOW,
        "%s gpibPortIfc\n",pniport->portName);
    writeRegister(pniport,AUXMR,AUXSIFC);
    epicsThreadSleep(.01);
    writeRegister(pniport,AUXMR,AUXCIFC);
    return asynSuccess;
}

static asynStatus gpibPortRen(void *pdrvPvt,asynUser *pasynUser, int onOff)
{
    niport *pniport = (niport *)pdrvPvt;

    asynPrint(pasynUser, ASYN_TRACE_FLOW,
        "%s gpibPortRen %s\n",pniport->portName,(onOff ? "On" : "Off"));
    writeRegister(pniport,AUXMR,(onOff ? AUXSREN : AUXCREN));
    return asynSuccess;
}

static asynStatus gpibPortSrqStatus(void *pdrvPvt,int *isSet)
{
    niport *pniport = (niport *)pdrvPvt;

    *isSet = ((readRegister(pniport,GPIBSR)&GPIBSRSRQ)==0) ? 0 : 1;
    return asynSuccess;
}

static asynStatus gpibPortSrqEnable(void *pdrvPvt, int onOff)
{
    niport *pniport = (niport *)pdrvPvt;

    pniport->srqEnabled = (onOff != 0);
    writeRegister(pniport,IMR2,COIE|(pniport->srqEnabled ? SRQIE : 0));
    return asynSuccess;
}

static asynStatus gpibPortSerialPollBegin(void *pdrvPvt)
{
    niport     *pniport = (niport *)pdrvPvt;
    double     timeout = 1.0;
    asynStatus status;
    char       cmd[3];

    cmd[0] = IBUNL;  cmd[1] = LADBASE; cmd[2] = IBSPE;
    status = writeCmd(pniport,cmd,3,timeout,transferStateIdle);
    return status;
}

static asynStatus gpibPortSerialPoll(void *pdrvPvt, int addr,
    double timeout,int *statusByte)
{
    niport     *pniport = (niport *)pdrvPvt;
    char buffer[1];

    buffer[0] = 0;
    pniport->bytesRemainingRead = 1;
    pniport->nextByteRead = buffer;
    pniport->status = asynSuccess;
    writeRegister(pniport,AUXMR,AUXFH);
    writeAddr(pniport,addr,-1,timeout,transferStateRead);
    *statusByte = buffer[0];
    return asynSuccess;
}

static asynStatus gpibPortSerialPollEnd(void *pdrvPvt)
{
    niport     *pniport = (niport *)pdrvPvt;
    double     timeout = 1.0;
    asynStatus status;
    char       cmd[2];

    cmd[0] = IBSPD; cmd[1] = IBUNT;
    status = writeCmd(pniport,cmd,2,timeout,transferStateIdle);
    return status;
}

int ni1014Config(char *portNameA,char *portNameB,
    int base, int vector, int level, int priority, int noAutoConnect)
{
    niport *pniportArray[2] = {0,0};
    int    size;
    int    indPort,nports;
    long   status;

    if(nloopsPerMicrosecond==0)
        initAuxmrWait();

    nports = (portNameB==0 || strlen(portNameB)==0) ? 1 : 2;
    for(indPort=0; indPort<nports; indPort++) {
        niport *pniport;

        size = sizeof(niport);
        size += (indPort==0) ? strlen(portNameA) : strlen(portNameB);
        size += 1;
        pniport = callocMustSucceed(size,sizeof(char),"ni1014Config");
        pniportArray[indPort] = pniport;
        pniport->portName = (char *)(pniport+1);
        strcpy(pniport->portName,((indPort==0) ? portNameA : portNameB));
        pniport->base = base;
        pniport->vector = vector;
        pniport->level = level;
        pniport->eos = -1;
        pniport->waitForInterrupt = epicsEventMustCreate(epicsEventEmpty);
        callbackSetCallback(srqCallback,&pniport->callback);
        callbackSetUser(pniport,&pniport->callback);
        status = devRegisterAddress("drvNi1014", atVMEA16, pniport->base,
            PORT_REGISTER_SIZE/2,(volatile void **)&pniport->registers);
        if(status) {
            printf("%s ni1014Config devRegisterAddress failed\n",pniport->portName);
            return(-1);
        }
        /* attach the interrupt handler routines */
        status = devConnectInterrupt(intVME,pniport->vector,
            ni1014,(void *)pniport);
        if(status) {
            errMessage(status,"ni1014: devConnectInterrupt");
            return -1;
        }
        status = devConnectInterrupt(intVME,pniport->vector+1,
            ni1014Err,(void *)pniport);
        if(status) {
            errMessage(status,"ni1014: devConnectInterrupt");
            return -1;
        }
        status = devEnableInterruptLevel(intVME,pniport->level);
        if(status) {
            errMessage(status,"ni1014: devEnableInterruptLevel");
            return -1;
        }
        pniport->isPortA = (indPort==0) ? 1 : 0;
        pniport->asynGpibPvt = pasynGpib->registerPort(pniport->portName,
            ASYN_MULTIDEVICE|ASYN_CANBLOCK,
            !noAutoConnect,&gpibPort,pniport,priority,0);
        base = base + PORT_REGISTER_SIZE;
        vector += 2;
    }
    return 0;
}

static const iocshArg ni1014ConfigArg0 = { "portNameA",iocshArgString};
static const iocshArg ni1014ConfigArg1 = { "portNameB",iocshArgString};
static const iocshArg ni1014ConfigArg2 = { "base",iocshArgInt};
static const iocshArg ni1014ConfigArg3 = { "vector",iocshArgInt};
static const iocshArg ni1014ConfigArg4 = { "level",iocshArgInt};
static const iocshArg ni1014ConfigArg5 = { "priority",iocshArgInt};
static const iocshArg ni1014ConfigArg6 = { "disable auto-connect",iocshArgInt};
static const iocshArg *ni1014ConfigArgs[] = {&ni1014ConfigArg0,
    &ni1014ConfigArg1, &ni1014ConfigArg2, &ni1014ConfigArg3,
    &ni1014ConfigArg4, &ni1014ConfigArg5, &ni1014ConfigArg6};
static const iocshFuncDef ni1014ConfigFuncDef =
    {"ni1014Config",7,ni1014ConfigArgs};
static void ni1014ConfigCallFunc(const iocshArgBuf *args)
{
    ni1014Config(args[0].sval,args[1].sval,
        args[2].ival,args[3].ival,args[4].ival,
        args[5].ival,args[6].ival);
}

void ni1014RegisterCommands(void)
{
    static int firstTime = 1;
    if (firstTime) {
        firstTime = 0;
        iocshRegister(&ni1014ConfigFuncDef,ni1014ConfigCallFunc);
    }
}
epicsExportRegistrar(ni1014RegisterCommands);
