/* drvGsIP488.c */

/*************************************************************************
* Copyright (c) 2003 The University of Chicago, as Operator of Argonne
* National Laboratory.
* This code is distributed subject to a Software License Agreement
* found in file LICENSE that is included with this distribution.
*************************************************************************/

/* Author: Marty Kraimer */
/*   date: 18DEC2003 */

/* Support for the IP-488 (Green Springs GPIB IP using the ti9914) */
/* John Winans wrote the original IP488 support that worked with EPICS*/

/* MARTY TO DO 
    add rebootHook. Must keep list of modules.
    implement disconnect
*/

#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <string.h>

/* From vxWorks */
#include <logLib.h>

#include <drvIpac.h>        /* IP management (from drvIpac) */

/* From EPICS base */
#include <epicsTypes.h>
#include <epicsStdio.h>
#include <devLib.h>
#include <epicsMutex.h>
#include <epicsThread.h>
#include <epicsEvent.h>
#include <epicsTimer.h>
#include <epicsExport.h>
#include <asynDriver.h>
#include <cantProceed.h>

#include "asynGpibDriver.h"

static char *untalkUnlisten = "_?";

#define DEFAULT_BUFFERSIZE 1024
#define ERROR_MESSAGE_BUFFER_SIZE 160
#define DEFAULT_TIMEOUT 5.0

#define IP488_CIC_ADDR  0       /* Primary address of the TI9914 controller*/
#define GREEN_SPRING_ID  0xf0
#define GSIP_488  0x14

typedef enum {
    transferStateIdle, transferStateRead, transferStateWrite, transferStateCntl
} transferState_t;

static epicsUInt16 readw(volatile epicsUInt16 *addr) { return *addr;}
static void writew(epicsUInt16 value,volatile epicsUInt16 *addr) {*addr = value;}
#define setTimeout(pasynUser) \
    (((pasynUser)->timeout<=0.0) ? DEFAULT_TIMEOUT : (pasynUser)->timeout)

typedef struct gpib {
    int         intVec;
    int         carrier;   /* Which IP carrier board*/
    int         module;    /* module number on carrier*/
    char        *portName;
    void        *asynGpibPvt;
    epicsUInt16 copyIsr0; 
    epicsUInt16 copyIsr1;
    /*bytesRemaining and nextByte are used by gpibInterruptHandler*/
    int	        bytesRemaining;
    epicsUInt8  *nextByte;
    /*buffer is holds data sent/received from user*/
    int         bufferSize;
    epicsUInt8  *buffer;
    epicsBoolean transferEoiOnLast;
    transferState_t transferState;
    int         eos;
    volatile struct ip488RegisterMap	*regs;	/* hardware registers*/
    asynStatus status; /*status of ip transfer*/
    epicsEventId waitForInterrupt;
    char errorMessage[ERROR_MESSAGE_BUFFER_SIZE];
}gpib;

static void printStatus(gpib *pgpib,const char *source);
static epicsBoolean auxCmd(volatile struct ip488RegisterMap *regs,
    epicsUInt16 outval, volatile epicsUInt16 *regin,
    epicsUInt16 mask, epicsUInt16 inval);
/*auxCmdIH is called from interrupt handler*/
static epicsBoolean auxCmdIH(volatile struct ip488RegisterMap *regs,
    epicsUInt16 outval, volatile epicsUInt16 *regin,
    epicsUInt16 mask, epicsUInt16 inval);
static void sicIpGpib(gpib *pgpib);
/* routine to wait for io completion*/
static void waitTimeout(gpib *pgpib,double seconds);
/* Interrupt routine */
static void gpibInterruptHandler(int v);
/* routines for ip I/O */
static asynStatus cmdIpGpib(gpib *pgpib,
    const char *buf, int cnt, double timeout);
static asynStatus readIpGpib(gpib *pgpib,
    char *buf, int cnt, int *actual, double timeout);
static asynStatus writeIpGpib(gpib *pgpib,
    const char *buf, int cnt, epicsBoolean raw_flag, double timeout);
/* Routines called by asynGpibPort methods */
static asynStatus writeGpib(gpib *pgpib,const char *buf, int cnt,
    int addr, double timeout);
static asynStatus readGpib(gpib *pgpib,char *buf, int cnt, int *actual,
    int addr, double timeout);

/* asynGpibPort methods */
static void gsTi9914Report(void *pdrvPvt,FILE *fd,int details);
static asynStatus gsTi9914Connect(void *pdrvPvt,asynUser *pasynUser);
static asynStatus gsTi9914Disconnect(void *pdrvPvt,asynUser *pasynUser);
static asynStatus gsTi9914SetPortOptions(void *pdrvPvt,asynUser *pasynUser,
    const char *key, const char *val);
static asynStatus gsTi9914GetPortOptions(void *pdrvPvt,asynUser *pasynUser,
    const char *key, char *val, int sizeval);
static int gsTi9914Read(void *pdrvPvt,asynUser *pasynUser,char *data,int maxchars);
static int gsTi9914Write(void *pdrvPvt,asynUser *pasynUser,const char *data,int numchars);
static asynStatus gsTi9914Flush(void *pdrvPvt,asynUser *pasynUser);
static asynStatus gsTi9914SetEos(void *pdrvPvt,asynUser *pasynUser,
    const char *eos,int eoslen);
static asynStatus gsTi9914AddressedCmd(void *pdrvPvt,asynUser *pasynUser,
    const char *data, int length);
static asynStatus gsTi9914UniversalCmd(void *pdrvPvt, asynUser *pasynUser, int cmd);
static asynStatus gsTi9914Ifc(void *pdrvPvt, asynUser *pasynUser);
static asynStatus gsTi9914Ren(void *pdrvPvt,asynUser *pasynUser, int onOff);
static int gsTi9914SrqStatus(void *pdrvPvt);
static asynStatus gsTi9914SrqEnable(void *pdrvPvt, int onOff);
static asynStatus gsTi9914SerialPollBegin(void *pdrvPvt);
static int gsTi9914SerialPoll(void *pdrvPvt, int addr, double timeout);
static asynStatus gsTi9914SerialPollEnd(void *pdrvPvt);

typedef volatile struct ip488RegisterMap {
  epicsUInt16 intStatusMask0;
  epicsUInt16 intStatusMask1;
  epicsUInt16 addressStatus;
  epicsUInt16 busStatusAuxCmd;
  epicsUInt16 address;
  epicsUInt16 serialPoll;
  epicsUInt16 cmdPassThruParallelPoll;
  epicsUInt16 data;
  epicsUInt16 addressSwitch;
  epicsUInt16 vectorRegister;
} ip488RegisterMap;

/* intStatusMask0 */
#define isr0MAC    0x01   /*My Address Change*/
#define isr0RLC    0x02   /*Remote/Local Change*/
#define isr0SPAS   0x04   /*Serial Poll or Aux command*/
#define isr0END    0x08   /* EOI present on bus*/
#define isr0BO     0x10	  /* Ready to write a byte*/
#define isr0BI     0x20	  /* Ready to read byte*/
#define isr0INT1   0x40	  /* Unmasked bit in ISR1 is set*/
#define isr0INT0   0x80	  /* Unmasked bit 2-7 in ISR0 is set*/

/* intStatusMask1 */
#define isr1IFC    0x01   /*Interface Clear*/
#define isr1SRQ    0x02   /* SRQ on bus is active*/
#define isr1MA     0x04   /* My address */
#define isr1DCAS   0x80   /*Device Clear Active Status*/
#define isr1APT    0x10   /*Address Pass Through*/
#define isr1UNC    0x20   /*Unrecognized command*/
#define isr1ERR	   0x40   /* handshake error (no listener)*/
#define isr1GET    0x80   /*Group Execute Trigger*/

/* addressStatus */
#define asrulpa    0x01   /*LSB of last address*/
#define asrTADS    0x02   /*Device is addressed to talk*/
#define asrLADS    0x04   /*Device is addressed to listen*/
#define asrTPAS    0x08   /*talker primary addressed state*/
#define asrLPAS    0x10   /*listener primary addressed state*/
#define asrATN     0x20   /*State of attention line*/
#define asrLLO     0x40   /*local lockout present*/
#define asrREM     0x80   /*device in remote state*/
 
/* busStatusAuxCmd */
#define busStatREN  0x01
#define busStatIFC  0x02
#define busStatSRQ  0x04
#define busStatEOI  0x08
#define busStatNRFD 0x10
#define busStatNDAC 0x20
#define busStatDAV  0x40
#define busStatATN  0x80
#define auxCmdswrstC    0x00    /* Software reset CLEAR              */
#define auxCmdswrstS    0x80    /* Software reset SET                */
#define auxCmdrhdf      0x02    /* Release RFD holdoff               */
#define auxCmdhdfeS     0x84    /* Holdoff on EOI only SET           */
#define auxCmdnbaf      0x05    /* New byte available false          */
#define auxCmdfeoi      0x08    /* Send EOI with next byte           */
#define auxCmdlonC      0x09    /* Listen only CLEAR                 */
#define auxCmdlonS      0x89    /* Listen only SET                   */
#define auxCmdtonC      0x0A    /* Talk only CLEAR                   */
#define auxCmdtonS      0x8A    /* Talk only SET                     */
#define auxCmdgts       0x0B    /* Go to standby                     */
#define auxCmdtca       0x0C    /* Take control asynchronously       */
#define auxCmdsicC      0x0F    /* Send interface clear CLEAR        */
#define auxCmdsicS      0x8F    /* Send interface clear SET          */
#define auxCmdsreC      0x10    /* Send remote enable CLEAR          */
#define auxCmdsreS      0x90    /* Send remote enable SET            */
#define auxCmddaiC      0x13    /* Disable all interrupts CLEAR      */
#define auxCmddaiS      0x93    /* Disable all interrupts SET        */

static void printStatus(gpib *pgpib, const char *source)
{
    sprintf(pgpib->errorMessage, "%s auxCmd failed "
        "intStatus0 %x intStatus1 %x addressStatus %x busStatus %x\n",
        source, (pgpib->copyIsr0&0xff),(pgpib->copyIsr1&0xff),
        (readw(&pgpib->regs->addressStatus)&0xff),
        (readw(&pgpib->regs->busStatusAuxCmd)&0xff));
}

static epicsBoolean auxCmd(volatile struct ip488RegisterMap *regs,
    epicsUInt16 outval, volatile epicsUInt16 *regin,
    epicsUInt16 mask, epicsUInt16 inval)
{
    volatile epicsUInt16 value = 0;
    int i;

    writew(outval,&regs->busStatusAuxCmd);
    /* Must wait 5 clock cycles. Lets just read busStatus 6 times*/
    for(i=0; i<6; i++) value = readw(&regs->busStatusAuxCmd);
    if(!regin) return(epicsTrue);
    value = readw(regin);
    if((value & mask) == inval) return(epicsTrue);
    printf("auxCmd failed outval %2.2x regin %p mask %2.2x "
        "inval %2.2x value %2.2x\n",outval,regin,mask,inval,value);
    return(epicsFalse);
}

static epicsBoolean auxCmdIH(volatile struct ip488RegisterMap *regs,
    epicsUInt16 outval, volatile epicsUInt16 *regin,
    epicsUInt16 mask, epicsUInt16 inval)
{
    volatile epicsUInt16 value = 0;
    int i;

    writew(outval,&regs->busStatusAuxCmd);
    /* Must wait 5 clock cycles. Lets just read busStatus 6 times*/
    for(i=0; i<6; i++) value = readw(&regs->busStatusAuxCmd);
    if(!regin) return(epicsTrue);
    value = readw(regin);
    if((value & mask) == inval) return(epicsTrue);
    logMsg("auxCmdIH failed outval %2.2x regin %p mask %2.2x "
        "inval %2.2x value %2.2x\n",outval,(int)regin,mask,inval,value,0);
    return(epicsFalse);
}

static void sicIpGpib(gpib *pgpib) /* set Interface Clear */
{
    pgpib->transferState = transferStateIdle;
    writew(auxCmdsicS,&pgpib->regs->busStatusAuxCmd);
    /* Must wait at least 100 microseconds*/
    epicsThreadSleep(.01);
    writew(auxCmdsicC,&pgpib->regs->busStatusAuxCmd);
}

static void waitTimeout(gpib *pgpib,double seconds)
{
    ip488RegisterMap *regs = pgpib->regs;
    epicsEventWaitStatus status;

    /* No need to check return status from epicsEventWaitWithTimeout    */
    /* If interrupt handler completes it changes transferState from one */
    /* of the states checked below before it signals completion         */
    status = epicsEventWaitWithTimeout(pgpib->waitForInterrupt,seconds);
    switch(pgpib->transferState) {
    case transferStateRead:
        pgpib->status=asynTimeout;
        writew(0,&regs->intStatusMask0);
        auxCmd(regs,auxCmdlonC,&regs->addressStatus,asrLADS,0);
        break;
    case transferStateWrite:
        pgpib->status=asynTimeout;
        writew(0,&regs->intStatusMask0);
        auxCmd(regs,auxCmdtonC,&regs->addressStatus,asrTADS,0);
    case transferStateCntl:
        pgpib->status=asynTimeout;
        writew(0,&regs->intStatusMask0);
        auxCmd(regs,auxCmdgts,&regs->addressStatus,asrATN,0);
        auxCmd(regs,auxCmdnbaf,0,0,0);
    default: break;
    }
    pgpib->transferState = transferStateIdle;
}

static void gpibInterruptHandler(int v)
{
    gpib *pgpib = (gpib *)v;
    ip488RegisterMap *regs = pgpib->regs;
    int message_complete = 0;
    epicsUInt16 copyIsr0,copyIsr1;

    copyIsr0 = readw(&regs->intStatusMask0);
    copyIsr1 = readw(&regs->intStatusMask1);
    if(copyIsr0==0 && copyIsr1==0) {
        return;
    }
    pgpib->copyIsr0 = copyIsr0;
    pgpib->copyIsr1 = copyIsr1;
    switch (pgpib->transferState) {
    case transferStateRead:
        if(copyIsr0 & isr0BI) {
            /* We are ready to read a byte now */
            epicsUInt8 data;
            data = readw(&regs->data);
            *pgpib->nextByte = data;
            if (pgpib->eos != -1 ) {
                /* Check for EOS character*/
                if (*pgpib->nextByte == pgpib->eos) {
                    /* This is EOS character, message complete */
                    message_complete = 1;
                }
            }
            --(pgpib->bytesRemaining);
            ++(pgpib->nextByte);
        }
        if(copyIsr0 & isr0END) {
            /* EOI was asserted for this character */
            message_complete = 1;
        }
        if(!message_complete && pgpib->bytesRemaining == 0) {
            pgpib->status = asynOverflow;
            message_complete = 1;
        }
        if(message_complete) {
            writew(0,&regs->intStatusMask0);
            if(!auxCmdIH(regs,auxCmdlonC,&regs->addressStatus,asrLADS,0))
                pgpib->status = asynError;
            pgpib->transferState = transferStateIdle;
            epicsEventSignal(pgpib->waitForInterrupt);
            return;
        }
        break;

    case transferStateWrite:
        if (copyIsr1 & isr1ERR) {
            /* Handshake failure -- we ain't got a listener */
            writew(0,&regs->intStatusMask0);
            ++(pgpib->bytesRemaining);
            --(pgpib->nextByte);
            logMsg("drvGsIP488::interruptHandler Handshake failure "
                "ISR0 %x ISR1 %x\n",
                copyIsr0&0xff,copyIsr1&0xff,0,0,0,0);
            pgpib->transferState = transferStateIdle;
    	    pgpib->status = asynError;
            epicsEventSignal(pgpib->waitForInterrupt);
            return ;
        }
        if (copyIsr0 & isr0BO) { /* We are ready to send a byte now*/
            epicsUInt8 data;
            if (pgpib->bytesRemaining == 0) {
                writew(0,&regs->intStatusMask0);
                if(!auxCmdIH(regs,auxCmdtonC,&regs->addressStatus,asrTADS,0))
                    pgpib->status = asynError;
                pgpib->transferState = transferStateIdle;
                epicsEventSignal(pgpib->waitForInterrupt);
                return ;
            }
            if ((pgpib->bytesRemaining == 1) && pgpib->transferEoiOnLast) {
                auxCmdIH(regs,auxCmdfeoi,0,0,0);
            }
            data = *pgpib->nextByte;
            writew((epicsUInt16)data,&regs->data);
            --(pgpib->bytesRemaining);
            ++(pgpib->nextByte);
        }
        break;
    case transferStateCntl:
        if (copyIsr0 & isr0BO) { /* We are ready to send a byte now*/
            epicsUInt8 data;
            if (pgpib->bytesRemaining == 0) {
                writew(0,&regs->intStatusMask0);
                if(!auxCmdIH(regs,auxCmdgts,&regs->addressStatus,asrATN,0))
                    pgpib->status = asynError;
                pgpib->transferState = transferStateIdle;
                epicsEventSignal(pgpib->waitForInterrupt);
                return ;
            }
            data = *pgpib->nextByte;
            writew((epicsUInt16)data,&regs->data);
            --(pgpib->bytesRemaining);
            ++(pgpib->nextByte);
        }
        break;
    case transferStateIdle:
        writew(0,&regs->intStatusMask0);
        logMsg("drvGsIP488::interrupt transferStateIdle isr0BI %d isr0BO %d\n",
            ((copyIsr0 & isr0BI) ? 1 : 0),
            ((copyIsr0 & isr0BO) ? 1 : 0),0,0,0,0);
        return;
    }
}

/* Write a string with the ATN line active */
static asynStatus cmdIpGpib(gpib *pgpib,
    const char *buf, int cnt, double timeout)
{
    ip488RegisterMap *regs = pgpib->regs;

    if (cnt == 0) {
        sprintf(pgpib->errorMessage,"cmdIpGpib called with cnt=0\n");
        return(asynError);
    }
    if(!auxCmd(regs,auxCmdtca,&regs->addressStatus,asrATN,asrATN)) {
        printStatus(pgpib,"cmdIpGpib");
        return(asynError);
    }
    auxCmd(regs,auxCmdnbaf,0,0,0);
    pgpib->bytesRemaining = cnt;
    pgpib->nextByte = (epicsUInt8 *)buf;
    pgpib->transferState = transferStateCntl;
    pgpib->status = asynSuccess;
    writew(isr0BO,&regs->intStatusMask0);
    waitTimeout(pgpib,timeout);
    auxCmd(regs,auxCmdgts,&regs->addressStatus,asrATN,0);
    return(pgpib->status);
}

static asynStatus readIpGpib(gpib *pgpib,
    char *buf, int cnt, int *actual, double timeout)
{
    ip488RegisterMap *regs = pgpib->regs;

    if (cnt == 0) {
        sprintf(pgpib->errorMessage,"readIpGpib called with cnt=0\n");
        return(asynError);
    }
    auxCmd(regs,auxCmdrhdf,0,0,0);
    auxCmd(regs,auxCmdhdfeS,0,0,0);
    if(!auxCmd(regs,auxCmdlonS,&regs->addressStatus,asrLADS,asrLADS)) {
        printStatus(pgpib,"readIpGpib");
        return(asynError);
    }
    pgpib->bytesRemaining = cnt;
    pgpib->nextByte = buf;
    pgpib->transferState = transferStateRead;
    pgpib->status = asynSuccess;
    writew(isr0BI,&regs->intStatusMask0);
    waitTimeout(pgpib,timeout);
    auxCmd(regs,auxCmdlonC,&regs->addressStatus,asrLADS,0);
    *actual = cnt - pgpib->bytesRemaining;
    return(pgpib->status);
}

static asynStatus writeIpGpib(gpib *pgpib,const char *buf,
    int cnt, epicsBoolean raw_flag, double timeout)
{
    ip488RegisterMap *regs = pgpib->regs;

    if (cnt == 0) {
        sprintf(pgpib->errorMessage,"writeIpGpib called with cnt=0\n");
        return (asynError);
    }
    if(!auxCmd(regs,auxCmdtonS,&regs->addressStatus,asrTADS,asrTADS)) {
        printStatus(pgpib,"writeIpGpib");
        return(asynError);
    }
    auxCmd(regs,auxCmdnbaf,0,0,0);
    if(raw_flag) 
	pgpib->transferEoiOnLast = epicsFalse;
    else
	pgpib->transferEoiOnLast = epicsTrue;
    pgpib->bytesRemaining = cnt;
    pgpib->nextByte = (epicsUInt8 *)buf;
    pgpib->transferState = transferStateWrite;
    pgpib->status = asynSuccess;
    writew(isr0BO,&regs->intStatusMask0);
    waitTimeout(pgpib,timeout);
    auxCmd(regs,auxCmdtonC,&regs->addressStatus,asrTADS,0);
    return(pgpib->status);

}

static asynStatus writeGpib(gpib *pgpib,const char *buf, int cnt,
     int addr, double timeout)
{
    char cmdbuf[3];
    asynStatus status;

    strcpy(cmdbuf,untalkUnlisten);
    cmdbuf[2] = (char)(addr + 0x20);
    status = cmdIpGpib(pgpib,cmdbuf,3,timeout);
    if(status!=asynSuccess) return(status);
    status = writeIpGpib(pgpib,buf,cnt,epicsFalse,timeout);
    if(status!=asynSuccess) return(status);
    status = cmdIpGpib(pgpib,untalkUnlisten, 2, timeout);
    return(status);
}

static asynStatus readGpib(gpib *pgpib,char *buf, int cnt, int *actual,
    int addr, double timeout)
{
    char cmdbuf[3];
    asynStatus status;

    *actual=0; *buf=0;
    strcpy(cmdbuf,untalkUnlisten);
    cmdbuf[2] = (char)(addr + 0x40);
    status = cmdIpGpib(pgpib,cmdbuf,3,timeout);
    if(status!=asynSuccess) return(status);
    status = readIpGpib(pgpib,buf,cnt,actual,timeout);
    if(status!=asynSuccess) return(status);
    status = cmdIpGpib(pgpib,untalkUnlisten, 2, timeout);
    return(status);
}

static void gsTi9914Report(void *pdrvPvt,FILE *fd,int details)
{
    gpib *pgpib = (gpib *)pdrvPvt;

    fprintf(fd,"    gpibGsTI9914 port %s intVec %d carrier %d module %d\n",
        pgpib->portName,pgpib->intVec,pgpib->carrier,pgpib->module);
}

static asynStatus gsTi9914Connect(void *pdrvPvt,asynUser *pasynUser)
{
    gpib *pgpib = (gpib *)pdrvPvt;
    int carrier = pgpib->carrier;
    int module = pgpib->module;
    int intVec = pgpib->intVec;
    ip488RegisterMap *regs = pgpib->regs;
    int ipstatus;

    ipstatus = ipmIntConnect(carrier,module,intVec,
        gpibInterruptHandler,(int)pgpib);
    if(ipstatus) {
        printf("gsIP488Configure ipmIntConnect failed\n");
        return(0);
    }
    ipmIrqCmd(carrier, module, 0, ipac_irqEnable);
    writew((epicsUInt16)pgpib->intVec,&regs->vectorRegister);
    /* Issue a software reset to the GPIB controller chip*/
    auxCmd(regs,auxCmdswrstS,0,0,0);
    epicsThreadSleep(.01);
    /* Set the ti9914's address to use while it is CIC */
    writew(IP488_CIC_ADDR,&regs->address);
    /* Toss interrupt status values before we release the reset */
    pgpib->copyIsr0 = readw(&regs->intStatusMask0);
    pgpib->copyIsr1 = readw(&regs->intStatusMask1);
    writew(0,&regs->intStatusMask0);
    writew(0,&regs->intStatusMask1);
    epicsThreadSleep(.01);
    auxCmd(regs,auxCmdswrstC,0,0,0);
    sicIpGpib(pgpib); /* assert IFC */
    auxCmd(regs,auxCmdsreS,0,0,0);
    return(asynSuccess);
}

static asynStatus gsTi9914Disconnect(void *pdrvPvt,asynUser *pasynUser)
{
    gpib *pgpib = (gpib *)pdrvPvt;
    int carrier = pgpib->carrier;
    int module = pgpib->module;
    ip488RegisterMap *regs = pgpib->regs;
    /* Issue a software reset to the GPIB controller chip*/
    auxCmd(regs,auxCmdswrstS,0,0,0);
    epicsThreadSleep(.01);
    ipmIrqCmd(carrier, module, 0, ipac_irqDisable);
    return(asynSuccess);
}

static asynStatus gsTi9914SetPortOptions(void *pdrvPvt,asynUser *pasynUser,
    const char *key, const char *val)
{
    gpib *pgpib = (gpib *)pdrvPvt;
    epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
        "%s gsTi9914 does not have any options\n",pgpib->portName);
    return(asynError);
}

static asynStatus gsTi9914GetPortOptions(void *pdrvPvt,asynUser *pasynUser,
    const char *key, char *val,int sizeval)
{
    gpib *pgpib = (gpib *)pdrvPvt;
    epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
        "%s gsTi9914 does not have any options\n",pgpib->portName);
    return(asynError);
}

static int gsTi9914Read(void *pdrvPvt,asynUser *pasynUser,char *data,int maxchars)
{
    gpib *pgpib = (gpib *)pdrvPvt;
    asynStatus status;
    int        actual;
    double     timeout = setTimeout(pasynUser);
    int        addr = pasynManager->getAddr(pasynUser);

    asynPrint(pasynUser,ASYN_TRACE_FLOW,"%s gsTi9914Read nchar %d",
        pgpib->portName,actual);
    status = readGpib(pgpib,data,maxchars,&actual,addr,timeout);
    if(status!=asynSuccess) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,"%s readGpib error %s\n",
            pgpib->portName,pgpib->errorMessage);
        return(-1);
    }
    asynPrintIO(pasynUser,ASYN_TRACEIO_DRIVER,
        data,actual,"%s gsTi9914Read\n",pgpib->portName);
    return(actual);
}

static int gsTi9914Write(void *pdrvPvt,asynUser *pasynUser,
    const char *data,int numchars)
{
    gpib *pgpib = (gpib *)pdrvPvt;
    asynStatus status;
    double     timeout = setTimeout(pasynUser);
    int        addr = pasynManager->getAddr(pasynUser);

    asynPrint(pasynUser,ASYN_TRACE_FLOW,"%s gsTi9914Write nchar %d",
        pgpib->portName,numchars);
    status = writeGpib(pgpib,data,numchars,addr,timeout);
    if(status!=asynSuccess) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,"%s writeGpib error %s\n",
            pgpib->portName,pgpib->errorMessage);
        return(-1);
    }
    asynPrintIO(pasynUser,ASYN_TRACEIO_DRIVER,
        data,numchars,"%s gsTi9914Write\n",pgpib->portName);
    return(numchars);
}

static asynStatus gsTi9914Flush(void *pdrvPvt,asynUser *pasynUser)
{
    /*Nothing to do */
    return(asynSuccess);
}

static asynStatus gsTi9914SetEos(void *pdrvPvt,asynUser *pasynUser,
    const char *eos,int eoslen)
{
    gpib *pgpib = (gpib *)pdrvPvt;

    asynPrint(pasynUser, ASYN_TRACE_FLOW,
        "%s gsTi9914SetEos eoslen %d\n",pgpib->portName,eoslen);
    if(eoslen>1 || eoslen<0) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
           "%s gpibTi9914SetEos illegal eoslen %d\n",pgpib->portName,eoslen);
        return(asynError);
    }
    pgpib->eos = (eoslen==0) ? -1 : (int)(unsigned int)eos[0] ;
    return(asynSuccess);
}

static asynStatus gsTi9914AddressedCmd(void *pdrvPvt,asynUser *pasynUser,
    const char *data, int length)
{
    gpib *pgpib = (gpib *)pdrvPvt;

    asynPrint(pasynUser, ASYN_TRACE_ERROR,
        "%s gsTi9914AddressedCmd not implemented\n",pgpib->portName);
    return(asynError);
}

static asynStatus gsTi9914UniversalCmd(void *pdrvPvt, asynUser *pasynUser, int cmd)
{
    gpib *pgpib = (gpib *)pdrvPvt;
    asynPrint(pasynUser, ASYN_TRACE_ERROR,
        "%s gsTi9914UniversalCmd not implemented\n",pgpib->portName);
    return(asynError);
}
 
static asynStatus gsTi9914Ifc(void *pdrvPvt, asynUser *pasynUser)
{
    gpib *pgpib = (gpib *)pdrvPvt;
    asynPrint(pasynUser, ASYN_TRACE_ERROR,
        "%s gsTi9914Ifc not implemented\n",pgpib->portName);
    return(asynError);
}

static asynStatus gsTi9914Ren(void *pdrvPvt,asynUser *pasynUser, int onOff)
{
    gpib *pgpib = (gpib *)pdrvPvt;
    asynPrint(pasynUser, ASYN_TRACE_ERROR,
        "%s gsTi9914Ren not implemented\n",pgpib->portName);
    return(asynError);
}

static int gsTi9914SrqStatus(void *pdrvPvt)
{
    gpib *pgpib = (gpib *)pdrvPvt;
    printf("%s gsTi9914SrqStatus not implemented\n",pgpib->portName);
    return(0);
}

static asynStatus gsTi9914SrqEnable(void *pdrvPvt, int onOff)
{
    gpib *pgpib = (gpib *)pdrvPvt;
    printf("%s gsTi9914SrqEnable not implemented\n",pgpib->portName);
    return(asynError);
}

static asynStatus gsTi9914SerialPollBegin(void *pdrvPvt)
{
    gpib *pgpib = (gpib *)pdrvPvt;
    printf("%s gsTi9914SerialPollBegin not implemented\n",pgpib->portName);
    return(asynError);
}

static int gsTi9914SerialPoll(void *pdrvPvt, int addr, double timeout)
{
    gpib *pgpib = (gpib *)pdrvPvt;
    printf("%s gsTi9914SerialPoll not implemented\n",pgpib->portName);
    return(asynError);
}

static asynStatus gsTi9914SerialPollEnd(void *pdrvPvt)
{
    gpib *pgpib = (gpib *)pdrvPvt;
    printf("%s gsTi9914SerialPollEnd not implemented\n",pgpib->portName);
    return(asynError);
}

static asynGpibPort gsTi9914 = {
    gsTi9914Report,
    gsTi9914Connect,
    gsTi9914Disconnect,
    gsTi9914SetPortOptions,
    gsTi9914GetPortOptions,
    gsTi9914Read,
    gsTi9914Write,
    gsTi9914Flush,
    gsTi9914SetEos,
    gsTi9914AddressedCmd,
    gsTi9914UniversalCmd,
    gsTi9914Ifc,
    gsTi9914Ren,
    gsTi9914SrqStatus,
    gsTi9914SrqEnable,
    gsTi9914SerialPollBegin,
    gsTi9914SerialPoll,
    gsTi9914SerialPollEnd
};

int gsIP488Configure(char *portName,int carrier, int module, int intVec,
    int priority)
{
    gpib *pgpib;
    int ipstatus;
    volatile struct ip488RegisterMap *regs;
    int size;

    ipstatus = ipmValidate(carrier,module,GREEN_SPRING_ID,GSIP_488);
    if(ipstatus) {
        printf("gsIP488Configure Unable to validate IP module");
        return(0);
    }
    regs = (volatile struct ip488RegisterMap*)ipmBaseAddr(
        carrier, module, ipac_addrIO);
    if(!regs) {
        printf("gsIP488Configure no memory allocated "
            "for carrier %d module %d\n", carrier,module);
        return(0);
    }
    if((intVec&1)==0) {
        printf("gsIP488Configure intVec MUST be odd. "
            "It is changed from %d to %d\n", intVec, intVec|1);
        intVec |= 1;
    }
    size = sizeof(gpib) + strlen(portName)+1;
    pgpib = callocMustSucceed(size,sizeof(char),"gsIP488Configure");
    pgpib->carrier = carrier;
    pgpib->module = module;
    pgpib->intVec = intVec;
    pgpib->portName = (char *)(pgpib+1);
    strcpy(pgpib->portName,portName);
    pgpib->waitForInterrupt = epicsEventMustCreate(epicsEventEmpty);
    pgpib->regs = regs;
    pgpib->asynGpibPvt = pasynGpib->registerPort(pgpib->portName,
        &gsTi9914,pgpib,priority,0);
    return(0);
}

/* IOC shell command registration */
#include <iocsh.h>
static const iocshArg gsIP488ConfigureArg0 = { "portName",iocshArgString};
static const iocshArg gsIP488ConfigureArg1 = { "carrier",iocshArgInt};
static const iocshArg gsIP488ConfigureArg2 = { "module",iocshArgInt};
static const iocshArg gsIP488ConfigureArg3 = { "intVec",iocshArgInt};
static const iocshArg gsIP488ConfigureArg4 = { "priority",iocshArgInt};
static const iocshArg *gsIP488ConfigureArgs[] = {&gsIP488ConfigureArg0,
    &gsIP488ConfigureArg1, &gsIP488ConfigureArg2, &gsIP488ConfigureArg3,
    &gsIP488ConfigureArg4};
static const iocshFuncDef gsIP488ConfigureFuncDef = {"gsIP488Configure",5,gsIP488ConfigureArgs};
static void gsIP488ConfigureCallFunc(const iocshArgBuf *args)
{
    char *portName = args[0].sval;
    int    carrier = args[1].ival;
    int     module = args[2].ival;
    int     intVec = args[3].ival;
    int   priority = args[4].ival;

    gsIP488Configure (portName, carrier, module, intVec, priority);
}


/*
 * This routine is called before multitasking has started, so there's
 * no race condition in the test/set of firstTime.
 */
static void gsIP488RegisterCommands (void)
{
    static int firstTime = 1;
    if (firstTime) {
        firstTime = 0;
        iocshRegister(&gsIP488ConfigureFuncDef,gsIP488ConfigureCallFunc);
    }
}
epicsExportRegistrar(gsIP488RegisterCommands);
