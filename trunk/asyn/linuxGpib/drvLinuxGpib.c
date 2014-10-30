/* drvLinuxGpib.c */
/***********************************************************************
* Copyright (c) 2004 by Danfysik and Cosylab (Danfysik has funded the work
* performed by Cosylab).
* linux-gpib port driver is distributed subject to a Software License Agreement 
* found in file LICENSE that is included with this distribution.
***********************************************************************/

/* Author: Gasper Jansa, Cosylab */
/* date:  18AUG2006
   date:  19OCT2006 - changed get/set options to check the option key and value
   date:  07APR2008 - added support for secondary address, address 30 bugfix
   date:  20JUN2008 - modified time of start of the poll_worker thread - after iocInit*/

#include <signal.h>
#include <errno.h>
#include <osiUnistd.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <string.h>


#include <epicsTypes.h>
#include <epicsEvent.h>
#include <epicsThread.h>
#include <epicsInterrupt.h>
#include <iocsh.h>
#include <callback.h>
#include <cantProceed.h>
#include <epicsTime.h>
#include <devLib.h>
#include <taskwd.h>
#include <initHooks.h>
#include <gpib/ib.h>

#include <epicsExport.h>
#include "asynDriver.h"
#include "asynOctet.h"
#include "asynOption.h"
#include "asynGpibDriver.h"

#define DEBUG 0

typedef struct GpibBoardPvt {
    char *portName;
    void  *asynGpibPvt;
    int ud;
    int uddev[31][31];
    int ibsta;
    int iberr;
    int sad;
    int boardIndex;
    int timeout;
    int srqEnabled;
    CALLBACK    callback;
    int srqHappened;
    pid_t worker_pid;
    asynInterface option;
}GpibBoardPvt;

GpibBoardPvt *pGlobalGpibBoardPvt;

static void report(void *pdrvPvt,FILE *fd,int details);
static asynStatus connect(void *pdrvPvt,asynUser *pasynUser);
static asynStatus disconnect(void *pdrvPvt,asynUser *pasynUser);
/*asynOctet methods */
static asynStatus gpibRead(void *pdrvPvt,asynUser *pasynUser,char 
*data,int maxchars,int *nbytesTransfered,int *eomReason);
static asynStatus gpibWrite(void *pdrvPvt,asynUser *pasynUser,
                    const char *data,int numchars,int *nbytesTransfered);
static asynStatus gpibFlush(void *pdrvPvt,asynUser *pasynUser);
static asynStatus setEos(void *pdrvPvt,asynUser *pasynUser,const char *eos,int eoslen);
static asynStatus getEos(void *pdrvPvt,asynUser *pasynUser,
    char *eos, int eossize, int *eoslen);
/*asynGpib methods*/
static asynStatus addressedCmd (void *pdrvPvt,asynUser *pasynUser,
    const char *data, int length);
static asynStatus universalCmd (void *pdrvPvt, asynUser *pasynUser, int cmd);
static asynStatus ifc (void *pdrvPvt,asynUser *pasynUser);
static asynStatus ren (void *pdrvPvt,asynUser *pasynUser, int onOff);
static asynStatus srqStatus (void *pdrvPvt,int *isSet);
static asynStatus srqEnable (void *pdrvPvt, int onOff);
static asynStatus serialPollBegin (void *pdrvPvt);
static asynStatus serialPoll (void *pdrvPvt, int addr, double timeout,int *status);
static asynStatus serialPollEnd (void *pdrvPvt);
/*local methods*/
static asynStatus checkError(void *pdrvPvt,asynUser *pasynUser,int addr);
unsigned int sec_to_timeout( double sec );
/*interrupt handlers*/
static void srqCallback(CALLBACK *pcallback);
/*EPICSTHREADFUNC poll_worker(GpibBoardPvt *pGpibBoardPvt);*/

void getAddr(int addr,int *primAddr,int *secAddr);


static asynGpibPort GpibBoardDriver = {
    report,
    connect,
    disconnect,
    gpibRead,
    gpibWrite,
    gpibFlush,
    setEos,
    getEos,
    addressedCmd,
    universalCmd,
    ifc,
    ren,
    srqStatus,
    srqEnable,
    serialPollBegin,
    serialPoll,
    serialPollEnd
};

static asynStatus gpibPortSetPortOptions(void *pdrvPvt,asynUser *pasynUser,
		const char *key, const char *val);
static asynStatus gpibPortGetPortOptions(void *pdrvPvt,asynUser *pasynUser,
		    const char *key, char *val, int sizeval);

static asynOption GpibBoardOption = {
      gpibPortSetPortOptions,
      gpibPortGetPortOptions
};

volatile int work;

void signal_handler(int signum){
	if(signum==SIGUSR1)
		work=0;
}

static void poll_worker(GpibBoardPvt *pGpibBoardPvt)
{
       	/*mask for srq bit*/
       	int mask=SRQI;
	struct sigaction new_action,old_action;
	work = 1;
	new_action.sa_handler=signal_handler;
	sigemptyset(&new_action.sa_mask);
	new_action.sa_flags=0;
	
	pGpibBoardPvt->worker_pid=getpid();
	
	sigaction(SIGUSR1,&new_action,&old_action);
	
	while(work){
	       	ibwait(pGpibBoardPvt->ud,mask);
		callbackRequest(&pGpibBoardPvt->callback);
		epicsThreadSleep(0.001);
	}
}


/* poll_worker is started after iocInit finishes */
void poll_worker_starter(initHookState state)
{

	if(state == initHookAtEnd)
	{
		/*create thread only if not yet created*/
		if((!epicsThreadGetId("PollThread")))
			epicsThreadCreate("PollThread", epicsThreadPriorityMedium,
				epicsThreadGetStackSize(epicsThreadStackSmall),
                       	        (EPICSTHREADFUNC)poll_worker,pGlobalGpibBoardPvt);	
	}
}


static void report(void *pdrvPvt,FILE *fd,int details)
{ 
	GpibBoardPvt *pGpibBoardPvt = (GpibBoardPvt *)pdrvPvt;
	
	if(DEBUG) printf("drvGpibBoard:report!!\n");
	fprintf(fd,"GpibBoard port %s, boardIndex %d,timeout %d.\n",
				   pGpibBoardPvt->portName,pGpibBoardPvt->boardIndex,pGpibBoardPvt->timeout);
		
}


static asynStatus connect(void *pdrvPvt,asynUser *pasynUser)
{
	GpibBoardPvt *pGpibBoardPvt = (GpibBoardPvt *)pdrvPvt;
	int addr = 0;
	int primaryAddr = 0;
	int secondaryAddr = 0;	
	asynStatus status;

	
	if(DEBUG) printf("drvGpibBoard:connect!!\n");

	status = pasynManager->getAddr(pasynUser,&addr);
	if(status!=asynSuccess) return status;
	

	asynPrint(pasynUser, ASYN_TRACE_FLOW,
			        "%s addr %d connect\n",pGpibBoardPvt->portName,addr);

	if(DEBUG) printf("ADDR: %d\n", addr);
         
	 if(addr==-1){
		 pGpibBoardPvt->ud=ibfind(pGpibBoardPvt->portName);
		 if(DEBUG) printf("DESCRIPTOR: %d\n",pGpibBoardPvt->ud);

	         if(pGpibBoardPvt->ud<0){
			status=checkError(pdrvPvt,pasynUser,addr);
        	        if(status!=asynSuccess)return status;
        	}
		
				 
		/*disable autopolling*/
		ibconfig(pGpibBoardPvt->ud,IbcAUTOPOLL,0);

		status=checkError(pdrvPvt,pasynUser,addr);
                if(status!=asynSuccess)return status;
					
		/*set general timeout for board*/
       		ibtmo(pGpibBoardPvt->ud,pGpibBoardPvt->timeout);

                status=checkError(pdrvPvt,pasynUser,addr);
                if(status!=asynSuccess)return status;
								
		
	}
	else{
		getAddr(addr,&primaryAddr,&secondaryAddr);
		/*third argument:secondary address,last two arguments are:set EOI line and set eos*/
		if((primaryAddr>=0 && primaryAddr<=30) && (secondaryAddr>=0 && secondaryAddr<=30)){
			pGpibBoardPvt->uddev[primaryAddr][secondaryAddr]=ibdev(pGpibBoardPvt->boardIndex,primaryAddr,secondaryAddr + 96,pGpibBoardPvt->timeout,1,0);
			
			if(DEBUG) printf("DESCRIPTOR: %d\n",pGpibBoardPvt->uddev[primaryAddr][secondaryAddr]);
			
			if(pGpibBoardPvt->uddev[primaryAddr][secondaryAddr]<0){ 
	                        status=checkError(pdrvPvt,pasynUser,addr);
		                if(status!=asynSuccess)return status;
			}
		}
		else{
			asynPrint(pasynUser,ASYN_TRACE_ERROR,
				           "%s addr %d connect illegal primary or secondary address (primary:%d,secondary %d)\n",
					              pGpibBoardPvt->portName,addr,primaryAddr,secondaryAddr);
			return asynError;			
		}	
	}
	
	
	pasynManager->exceptionConnect(pasynUser);	

 	return asynSuccess;
}

static asynStatus disconnect(void *pdrvPvt,asynUser *pasynUser)
{
	GpibBoardPvt *pGpibBoardPvt = (GpibBoardPvt *)pdrvPvt;
	int addr=0;
	int primaryAddr = 0;
	int secondaryAddr = 0;	;	
	asynStatus status;
	
	if(DEBUG) printf("drvGpibBoard:disconnect!!\n");
	
	status = pasynManager->getAddr(pasynUser,&addr);
	if(status!=asynSuccess) return status;
	
	getAddr(addr,&primaryAddr,&secondaryAddr);
		
	asynPrint(pasynUser, ASYN_TRACE_FLOW,
			        "%s addr %d disconnect\n",pGpibBoardPvt->portName,addr);
	
	/*disconnect device or board*/
	if(addr==-1){

		ibonl(pGpibBoardPvt->ud,0);	
	 
                status=checkError(pdrvPvt,pasynUser,addr);
                if(status!=asynSuccess)return status;
								
	 	/*destroy poll_worker*/
	 	kill(pGpibBoardPvt->worker_pid,SIGUSR1);
	}
	else{
		ibonl(pGpibBoardPvt->uddev[primaryAddr][secondaryAddr],0);
	
                status=checkError(pdrvPvt,pasynUser,addr);
                if(status!=asynSuccess)return status;
								
	}

	pasynManager->exceptionDisconnect(pasynUser);
	return asynSuccess;
}

static asynStatus gpibPortSetPortOptions(void *pdrvPvt,asynUser *pasynUser,
const char *key, const char *val)
{
	GpibBoardPvt *pGpibBoardPvt = (GpibBoardPvt *)pdrvPvt;
	int option,value;
        int addr=0,j=0;
	int primaryAddr = 0;
	int secondaryAddr = 0;	
	char cset[] = "1234567890ABCDEFabcdefxX";	
        asynStatus status;
	int parseStatus;
	
		       
        status = pasynManager->getAddr(pasynUser,&addr);
        if(status!=asynSuccess) return status;
	
	getAddr(addr,&primaryAddr,&secondaryAddr);
			
	if(DEBUG) printf("drvGpibBoard:gpibPortSetPortOptions!!\n");
	
	asynPrint(pasynUser,ASYN_TRACE_FLOW,"%s gpibPortSetPortOptions\n",
			        pGpibBoardPvt->portName);


	j = strspn (key,cset);
	
	if((*key != '0') || ((*(key+1) != 'x') && (*(key+1) != 'X')) || ( j != strlen(key))){
		asynPrint(pasynUser,ASYN_TRACE_ERROR,
				           "%s addr %d gpibPortSetPortOptions illegal option: %s\n",
					              pGpibBoardPvt->portName,addr,key);
		return asynError;
	}
	
	parseStatus = sscanf(key,"%x",&option);
	
	if(parseStatus != 1){
		asynPrint(pasynUser,ASYN_TRACE_ERROR,
				           "%s addr %d gpibPortSetPortOptions illegal option: %s\n",
					              pGpibBoardPvt->portName,addr,key);
		return asynError;
	}	
	
	parseStatus = sscanf(val,"%d",&value);
	
	if(parseStatus != 1){
		asynPrint(pasynUser,ASYN_TRACE_ERROR,
				           "%s addr %d gpibPortSetPortOptions illegal option value: %s\n",
					              pGpibBoardPvt->portName,addr,val);
		return asynError;
	}		

	if(DEBUG) printf("option %d set with value %d\n",option,value); 
	
	
	if(addr==-1){
		ibconfig(pGpibBoardPvt->ud,option,value);
	}
	else{
		ibconfig(pGpibBoardPvt->uddev[primaryAddr][secondaryAddr],option,value);
	}
	
        status=checkError(pdrvPvt,pasynUser,addr);
        if(status!=asynSuccess)return status;
	
 	return asynSuccess;
}

static asynStatus gpibPortGetPortOptions(void *pdrvPvt,asynUser *pasynUser,
    const char *key, char *val, int sizeval)
{
	GpibBoardPvt *pGpibBoardPvt = (GpibBoardPvt *)pdrvPvt;
	int value,option;
	int addr=0,j=0;
	int primaryAddr = 0;
	int secondaryAddr = 0;		
	asynStatus status;
	char cset[] = "1234567890ABCDEFabcdefxX";
	int parseStatus = 0;
	
	

        status = pasynManager->getAddr(pasynUser,&addr);
        if(status!=asynSuccess) return status;
	
	getAddr(addr,&primaryAddr,&secondaryAddr);
			
	if(DEBUG) printf("drvGpibBoard:gpibPortGetPortOptions!!\n");
	
	
	asynPrint(pasynUser,ASYN_TRACE_FLOW,"%s gpibPortGetPortOptions\n",
			        *pGpibBoardPvt->portName);

	
	j = strspn (key,cset);
	
	if((*key != '0') || ((*(key+1) != 'x') && (*(key+1) != 'X')) || ( j != strlen(key))){
		asynPrint(pasynUser,ASYN_TRACE_ERROR,
				           "%s addr %d gpibPortGetPortOptions illegal option: %s\n",
					              pGpibBoardPvt->portName,addr,key);
		return asynError;
	}
	
	parseStatus=sscanf(key,"%x",&option);	
	
	if(parseStatus != 1){
		asynPrint(pasynUser,ASYN_TRACE_ERROR,
				           "%s addr %d gpibPortGetPortOptions illegal option: %s\n",
					              pGpibBoardPvt->portName,addr,key);
		return asynError;
	}
	
	if(addr==-1){
		ibask(pGpibBoardPvt->ud,option,&value);
	}
	else{
		ibask(pGpibBoardPvt->uddev[primaryAddr][secondaryAddr],option,&value);
	}
	
        status=checkError(pdrvPvt,pasynUser,addr);
        if(status!=asynSuccess)return status;
	
	sprintf(val,"%d",value);

	if(DEBUG) printf("option %d get with value %d\n",option,value);
	
 	return asynSuccess;
}

/*asynOctet methods */
static asynStatus gpibRead(void *pdrvPvt,asynUser *pasynUser,char
*data,int maxchars,int *nbytesTransfered,int *eomReason)
{
	GpibBoardPvt *pGpibBoardPvt = (GpibBoardPvt *)pdrvPvt;
 	int addr=0;
	int primaryAddr = 0;
	int secondaryAddr = 0;		
        asynStatus status=asynSuccess;
        double timeout = pasynUser->timeout;
        int ibsta;

	if(DEBUG) printf("drvGpibBoard:gpibRead!!\n");
	epicsThreadSleep(epicsThreadSleepQuantum());
	
	status = pasynManager->getAddr(pasynUser,&addr);
	if(status!=asynSuccess) return status;
	
	getAddr(addr,&primaryAddr,&secondaryAddr);
	
	asynPrint(pasynUser,ASYN_TRACE_FLOW,"%s addr %d gpibRead\n",
			        pGpibBoardPvt->portName,addr);
	
	/*set timeout*/
	ibtmo(pGpibBoardPvt->uddev[primaryAddr][secondaryAddr],sec_to_timeout(timeout));
	
        status=checkError(pdrvPvt,pasynUser,addr);
        if(status!=asynSuccess)return status;
							
	
	/*read data*/
	ibsta = ibrd(pGpibBoardPvt->uddev[primaryAddr][secondaryAddr],data,maxchars);

	/*ibcnt holds number of bytes transfered*/
	*nbytesTransfered=ibcnt;
	
	if (eomReason != NULL)
        {
            *eomReason = 0;
            if (ibsta & END) *eomReason = ASYN_EOM_END;
            if (ibcnt == maxchars) *eomReason = ASYN_EOM_CNT;
        }
	
	status=checkError(pdrvPvt,pasynUser,addr);
	if(status!=asynSuccess){
		epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
				            "%s readGpib failed %s",pGpibBoardPvt->portName,gpib_error_string(pGpibBoardPvt->iberr));
	
		return status;
	}	
	
	asynPrintIO(pasynUser,ASYN_TRACEIO_DRIVER,data,ibcnt,"%s addr %d gpibPortRead\n",pGpibBoardPvt->portName,addr);
	
	/*if last response is shorter than previous*/
	if(ibcnt<maxchars) data[ibcnt] = 0;

        if(DEBUG) printf("DATA READ: %s\n",data);
	
	return status;
}

static asynStatus gpibWrite(void *pdrvPvt,asynUser *pasynUser,
                    const char *data,int numchars,int *nbytesTransfered)
{
	GpibBoardPvt *pGpibBoardPvt = (GpibBoardPvt *)pdrvPvt;
	int addr=0;
	int primaryAddr = 0;
	int secondaryAddr = 0;		
	asynStatus status=asynSuccess;
        double timeout = pasynUser->timeout;

	status = pasynManager->getAddr(pasynUser,&addr);
	if(status!=asynSuccess) return status;
	
	getAddr(addr,&primaryAddr,&secondaryAddr);
	
	if(DEBUG){
		printf("drvGpibBoard:gpibWrite!!\n");
		printf("WRITING STRING: %s \n",data);
	}
	
	asynPrint(pasynUser,ASYN_TRACE_FLOW,"%s addr %d gpibWrite nchar %d\n",
			        pGpibBoardPvt->portName,addr,numchars);
	
        /*set timeout*/
        ibtmo(pGpibBoardPvt->uddev[primaryAddr][secondaryAddr],sec_to_timeout(timeout));

        status=checkError(pdrvPvt,pasynUser,addr);
        if(status!=asynSuccess)return status;
							
	/*write data*/
	ibwrt(pGpibBoardPvt->uddev[primaryAddr][secondaryAddr],data,numchars);
		
        status=checkError(pdrvPvt,pasynUser,addr);
        if(status!=asynSuccess){
	        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
	            "%s writeGpib failed %s",pGpibBoardPvt->portName,gpib_error_string(pGpibBoardPvt->iberr));
		
	            return status;
	}
	
	
	/*ibcnt holds number of bytes transfered*/
	*nbytesTransfered=ibcnt;

	asynPrintIO(pasynUser,ASYN_TRACEIO_DRIVER,
			        data,ibcnt,"%s addr %d gpibPortWrite\n",pGpibBoardPvt->portName,addr);
	
	return status;
}

static asynStatus gpibFlush(void *pdrvPvt,asynUser *pasynUser)
{
	if(DEBUG)
		printf("drvGpibBoard:gpibFlush!!\n");
	/*nothing to do*/
	return asynSuccess;
}

static asynStatus setEos(void *pdrvPvt,asynUser *pasynUser,const char *eos,int eoslen)
{
	GpibBoardPvt *pGpibBoardPvt = (GpibBoardPvt *)pdrvPvt;
	int addr = 0;
	int primaryAddr = 0;
	int secondaryAddr = 0;	      
        asynStatus status;
	
	if(DEBUG) printf("drvGpibBoard:setEos!!\n");

	status = pasynManager->getAddr(pasynUser,&addr);
	if(status!=asynSuccess) return status;
	
	getAddr(addr,&primaryAddr,&secondaryAddr);

	asynPrint(pasynUser, ASYN_TRACE_FLOW,
			        "%s addr %d setEos eoslen %d\n",pGpibBoardPvt->portName,addr,eoslen);

	if(eoslen>1 || eoslen<0){
		asynPrint(pasynUser,ASYN_TRACE_ERROR,
				           "%s addr %d gpibBoard:setEos illegal eoslen %d\n",
					              pGpibBoardPvt->portName,addr,eoslen);
	
	         return asynError;
	}
	
	if(eoslen==1){
                ibconfig(pGpibBoardPvt->uddev[primaryAddr][secondaryAddr],IbcEOScmp,1);
		ibconfig(pGpibBoardPvt->uddev[primaryAddr][secondaryAddr],IbcEOSchar,*eos);

                status=checkError(pdrvPvt,pasynUser,addr);
                if(status!=asynSuccess)return status;
								
		ibconfig(pGpibBoardPvt->uddev[primaryAddr][secondaryAddr],IbcEOSrd,1);
		
		if(DEBUG)
			printf("Seting EOS: %u\n",*eos);

                status=checkError(pdrvPvt,pasynUser,addr);
                if(status!=asynSuccess)return status;
								 
	}
	else{
		pGpibBoardPvt->ibsta=ibconfig(pGpibBoardPvt->uddev[primaryAddr][secondaryAddr],IbcEOSrd,0);
		if(DEBUG) printf("Disabling EOS\n");
	        status=checkError(pdrvPvt,pasynUser,addr);
        	if(status!=asynSuccess)return status;
	}
	return asynSuccess;
}


static asynStatus getEos(void *pdrvPvt,asynUser *pasynUser,
    char *eos, int eossize, int *eoslen)
{
	GpibBoardPvt *pGpibBoardPvt = (GpibBoardPvt *)pdrvPvt;
	int addr = 0;
	int primaryAddr = 0;
	int secondaryAddr = 0;	
	asynStatus status;
	int ieos;
		
	status = pasynManager->getAddr(pasynUser,&addr);
        if(status!=asynSuccess) return status;
	
	getAddr(addr,&primaryAddr,&secondaryAddr);

	asynPrint(pasynUser, ASYN_TRACE_FLOW,
	             "%s addr %d gpibPortGetEos\n",pGpibBoardPvt->portName,addr);
	
	if(DEBUG) printf("drvGpibBoard:getEos!!\n");
			
	
	if(eossize<1) {
	        asynPrint(pasynUser,ASYN_TRACE_ERROR,
		            "%s addr %d getEos eossize %d too small\n",
		                pGpibBoardPvt->portName,eossize);
		*eoslen = 0;
		return asynError;
	}
	
	ibask(pGpibBoardPvt->uddev[primaryAddr][secondaryAddr],IbaEOSrd,&ieos);

        status=checkError(pdrvPvt,pasynUser,addr);
        if(status!=asynSuccess)return status;
							
	
	if(ieos==0){
		*eoslen = 0;
		if(DEBUG) printf("EOS disabled\n");
	}	
	else {
		*eoslen = 1;
		ibask(pGpibBoardPvt->uddev[primaryAddr][secondaryAddr],IbaEOSchar,&ieos);

                status=checkError(pdrvPvt,pasynUser,addr);
                if(status!=asynSuccess)return status;
								
		if(DEBUG) printf("EOS: %d\n",ieos);
		eos[0]=ieos;
	}
		
	return asynSuccess;
} 
   
static asynStatus addressedCmd (void *pdrvPvt,asynUser *pasynUser,
     const char *data, int length)
{
	GpibBoardPvt *pGpibBoardPvt = (GpibBoardPvt *)pdrvPvt;
	int addr = 0;
	int primaryAddr = 0;
	int secondaryAddr = 0;		
	asynStatus status;
	double timeout = pasynUser->timeout;
	
	if(DEBUG) printf("drvGpibBoard:addressedCmd!!\n");
	status = pasynManager->getAddr(pasynUser,&addr);
	if(status!=asynSuccess) return status;
	
	getAddr(addr,&primaryAddr,&secondaryAddr);
	
	asynPrint(pasynUser,ASYN_TRACE_FLOW,"%s addr %d addressedCmd nchar %d\n",
			         pGpibBoardPvt->portName,addr,length);

        /*set timeout*/
        ibtmo(pGpibBoardPvt->uddev[primaryAddr][secondaryAddr],sec_to_timeout(timeout));

        status=checkError(pdrvPvt,pasynUser,addr);
        if(status!=asynSuccess)return status;
	
	if(DEBUG) printf("DATA: %s\n", data);
	
 	Send(pGpibBoardPvt->ud,MakeAddr(primaryAddr,secondaryAddr),data,strlen(data),1);
	
        status=checkError(pdrvPvt,pasynUser,addr);
        if(status!=asynSuccess)return status;
							
        return asynSuccess;
}

static asynStatus universalCmd(void *pdrvPvt, asynUser *pasynUser, int cmd)
{
	GpibBoardPvt *pGpibBoardPvt = (GpibBoardPvt *)pdrvPvt;
	int addr=0;
	int primaryAddr = 0;
	int secondaryAddr = 0;		
	asynStatus status; 
	char chcmd[20];
	
        status = pasynManager->getAddr(pasynUser,&addr);
        if(status!=asynSuccess) return status;
	
	getAddr(addr,&primaryAddr,&secondaryAddr);			
	
	asynPrint(pasynUser,ASYN_TRACE_FLOW,"%s universalCmd %2.2x\n",
			        pGpibBoardPvt->portName,cmd);
	
	if(DEBUG){
		printf("drvGpibBoard:universalCmd!!\n");
		printf("CMD integer: %d\n",cmd);
	}	
	sprintf(chcmd,"\\%x",cmd); 
	
	if(DEBUG)printf("CMD string: %s\n",chcmd);
	
	ibcmd(pGpibBoardPvt->ud,chcmd,strlen(chcmd));
	
        status=checkError(pdrvPvt,pasynUser,addr);
        if(status!=asynSuccess)return status;
	
	return asynSuccess;
}

static asynStatus ifc(void *pdrvPvt,asynUser *pasynUser)
{
	GpibBoardPvt *pGpibBoardPvt = (GpibBoardPvt *)pdrvPvt;
	int addr=0;
	int primaryAddr = 0;
	int secondaryAddr = 0;		
	asynStatus status;

        status = pasynManager->getAddr(pasynUser,&addr);
        if(status!=asynSuccess) return status;
	
	getAddr(addr,&primaryAddr,&secondaryAddr);		       
	
	if(DEBUG)printf("drvGpibBoard:ifc!!\n");
	
	asynPrint(pasynUser, ASYN_TRACE_FLOW,
			        "%s ifc\n",pGpibBoardPvt->portName);
	
	ibsic(pGpibBoardPvt->ud);
	
        status=checkError(pdrvPvt,pasynUser,addr);
        if(status!=asynSuccess)return status;
							
	return asynSuccess;
}


static asynStatus ren(void *pdrvPvt,asynUser *pasynUser, int onOff)
{	
	GpibBoardPvt *pGpibBoardPvt = (GpibBoardPvt *)pdrvPvt;
	int addr=0;
	int primaryAddr = 0;
	int secondaryAddr = 0;		
	asynStatus status;
	
        status = pasynManager->getAddr(pasynUser,&addr);
        if(status!=asynSuccess) return status;
			
	getAddr(addr,&primaryAddr,&secondaryAddr);			
	
	if(DEBUG)printf("drvGpibBoard:ren!!\n");
	
	asynPrint(pasynUser, ASYN_TRACE_FLOW,
			        "%s ren %s\n",pGpibBoardPvt->portName,(onOff ? "On" : "Off"));
	
	ibsre(pGpibBoardPvt->ud,onOff);

        status=checkError(pdrvPvt,pasynUser,addr);
        if(status!=asynSuccess)return status;
							
	return asynSuccess;
}



static asynStatus srqStatus (void *pdrvPvt,int *isSet)
{

	GpibBoardPvt *pGpibBoardPvt = (GpibBoardPvt *)pdrvPvt;
	
	if(DEBUG)printf("drvGpibBoard:srqStatus!!\n");
	
	*isSet=pGpibBoardPvt->srqHappened;
	
	pGpibBoardPvt->srqHappened = 0;
	
	return asynSuccess;
}

static asynStatus srqEnable (void *pdrvPvt, int onOff)
{

	GpibBoardPvt *pGpibBoardPvt = (GpibBoardPvt *)pdrvPvt;
	if(DEBUG)printf("drvGpibBoard:srqEnable\n");

	pGpibBoardPvt->srqEnabled = (onOff != 0);
	
	return  asynSuccess;
}

static asynStatus serialPollBegin (void *pdrvPvt)
{
	/*not used*/			
	return asynSuccess;
}

static asynStatus serialPoll (void *pdrvPvt, int addr, double timeout,int *statusByte)
{
	GpibBoardPvt *pGpibBoardPvt = (GpibBoardPvt *)pdrvPvt;
        char serialPollByte = 0;
	int ibsta;
	int primaryAddr = 0;
	int secondaryAddr = 0;	
	
	getAddr(addr,&primaryAddr,&secondaryAddr);	

	if(DEBUG){
		printf("drvGpibBoard:serialPoll!!\n");
        	printf("ADDR: %d\n",addr);
		printf("uddev: %d\n",pGpibBoardPvt->uddev[primaryAddr][secondaryAddr]);
	}	

        /*set timeout*/
        ibsta=ibtmo(pGpibBoardPvt->uddev[primaryAddr][secondaryAddr],sec_to_timeout(timeout));
	if(ibsta&TIMO)
		return asynTimeout;
	if(ibsta&ERR)
		return asynError;


	/*serial poll*/
	ibsta=ibrsp(pGpibBoardPvt->uddev[primaryAddr][secondaryAddr],&serialPollByte);
	if(ibsta&TIMO)
		return asynTimeout;
	if(ibsta&ERR)	
		return asynError;
	
	*statusByte = (int)serialPollByte;
	
	return asynSuccess;
}

static asynStatus serialPollEnd (void *pdrvPvt)
{
	/*not used*/
	return asynSuccess;
}

asynStatus checkError(void *pdrvPvt,asynUser *pasynUser,int addr)
{
	 GpibBoardPvt *pGpibBoardPvt = (GpibBoardPvt *)pdrvPvt;
	 
         pGpibBoardPvt->ibsta=ThreadIbsta();
         pGpibBoardPvt->iberr=ThreadIberr();
	
	 if(DEBUG){
	 	printf("drvGpibBoard:checkIberr\n");
	 	printf("ibsta: %d\n",pGpibBoardPvt->ibsta);
		
		if(pGpibBoardPvt->ibsta & DCAS)
			printf("IBSTA bit 0\n");
		if(pGpibBoardPvt->ibsta & DTAS)
			printf("IBSTA bit 1\n");
                if(pGpibBoardPvt->ibsta & LACS)
                        printf("IBSTA bit 2\n");
                if(pGpibBoardPvt->ibsta & TACS)
                        printf("IBSTA bit 3\n");
                if(pGpibBoardPvt->ibsta & ATN)
                        printf("IBSTA bit 4\n");
                if(pGpibBoardPvt->ibsta & CIC)
                        printf("IBSTA bit 5\n");
                if(pGpibBoardPvt->ibsta & REM)
                        printf("IBSTA bit 6\n");
                if(pGpibBoardPvt->ibsta & LOK)
                        printf("IBSTA bit 7\n");
                if(pGpibBoardPvt->ibsta & CMPL)
                	printf("IBSTA bit 8\n");		              
		if(pGpibBoardPvt->ibsta & EVENT)
			printf("IBSTA bit 9\n");
		if(pGpibBoardPvt->ibsta & SPOLL)
			printf("IBSTA bit 10\n");
		if(pGpibBoardPvt->ibsta & RQS)
		        printf("IBSTA bit 11\n");
		if(pGpibBoardPvt->ibsta & SRQI)
		        printf("IBSTA bit 12\n");
		if(pGpibBoardPvt->ibsta & END)
		        printf("IBSTA bit 13\n");
		if(pGpibBoardPvt->ibsta & TIMO)
		        printf("IBSTA bit 14\n");
		if(pGpibBoardPvt->ibsta & ERR)
		        printf("IBSTA bit 15\n");
				
	 	printf("iberr: %d\n",pGpibBoardPvt->iberr);
		
	 }	
	
	 if(pGpibBoardPvt->ibsta & TIMO){
	         asynPrint(pasynUser,ASYN_TRACE_ERROR,"%s addr %d : device timed out.\n",pGpibBoardPvt->portName,addr);
		 return asynTimeout;
	}
		  
	 if(pGpibBoardPvt->ibsta & ERR){
		 asynPrint(pasynUser,ASYN_TRACE_ERROR,"%s addr %d : %s.\n",pGpibBoardPvt->portName,addr,gpib_error_string(pGpibBoardPvt->iberr));
		 
		return asynError;
	 }
	 
	return asynSuccess;
}

unsigned int sec_to_timeout( double sec )
{
       if( sec <= 0 ) return TNONE;
       else if( sec <= .00001 ) return T10us;
       else if( sec <= .00003 ) return T30us;
       else if( sec <= .0001 ) return T100us;
       else if( sec <= .0003 ) return T300us;
       else if( sec <= .001 ) return T1ms;
       else if( sec <= .003 ) return T3ms;
       else if( sec <= .01 ) return T10ms;
       else if( sec <= .03 ) return T30ms;
       else if( sec <= .1 ) return T100ms;
       else if( sec <= .3 ) return T300ms;
       else if( sec <= 1 ) return T1s;
       else if( sec <= 3 ) return T3s;
       else if( sec <= 10 ) return T10s;
       else if( sec <= 30 ) return T30s;
       else if( sec <= 100 ) return T100s;
       else if( sec <= 300 ) return T300s;
       else if( sec <= 1000 ) return T1000s;
       return TNONE;
}

static void srqCallback(CALLBACK *pcallback)
{
	GpibBoardPvt *pGpibBoardPvt;

	callbackGetUser(pGpibBoardPvt,pcallback);
	if(!pGpibBoardPvt->srqEnabled) return;
	pGpibBoardPvt->srqHappened = 1;	
	pasynGpib->srqHappened(pGpibBoardPvt->asynGpibPvt);
}

void getAddr(int addr,int *primAddr,int *secAddr)
{
	if(addr < 100){
		*primAddr = addr;
		return;
	}
	*primAddr = addr/100;
	*secAddr = addr%100;
	return;
}



int GpibBoardDriverConfig(char *name,int autoConnect,int boardIndex,double timeout,int priority)
{
    GpibBoardPvt *pGpibBoardPvt;
    int size;
    asynStatus status;
	    
    if(DEBUG) printf("GpibBoardDriverConfig\n");
    	    
    
    size=sizeof(GpibBoardPvt)+strlen(name)+1;
    pGpibBoardPvt=callocMustSucceed(size,sizeof(char),"GpibBoardDriverConfig");
    pGpibBoardPvt->portName =(char *) (pGpibBoardPvt+1);
    strcpy(pGpibBoardPvt->portName,name);
    pGpibBoardPvt->boardIndex=boardIndex;
    pGpibBoardPvt->timeout=sec_to_timeout(timeout);
    callbackSetCallback(srqCallback,&pGpibBoardPvt->callback);
    callbackSetUser(pGpibBoardPvt,&pGpibBoardPvt->callback);
    
    pGpibBoardPvt->asynGpibPvt =
    pasynGpib->registerPort(pGpibBoardPvt->portName,
       ASYN_MULTIDEVICE|ASYN_CANBLOCK,autoConnect,&GpibBoardDriver,pGpibBoardPvt,priority,0);

    pGpibBoardPvt->option.interfaceType=asynOptionType;
    pGpibBoardPvt->option.pinterface=(void *) &GpibBoardOption;
    pGpibBoardPvt->option.drvPvt= pGpibBoardPvt;
    status = pasynManager->registerInterface(pGpibBoardPvt->portName,&pGpibBoardPvt->option);
    
    if(status==asynError){
	    errlogPrintf("GpibBoardDriverConfig: Can't register option.\n");
	    return -1;
    }
    
    pGlobalGpibBoardPvt = pGpibBoardPvt;
    
    /*register for init */
    initHookRegister(poll_worker_starter);
    
    return 0;
}

/*register GpibBoardDriverConfig*/
static const iocshArg GpibBoardDriverConfigArg0 =
    { "portName", iocshArgString };
static const iocshArg GpibBoardDriverConfigArg1 =
    { "autoConnect", iocshArgInt };
static const iocshArg GpibBoardDriverConfigArg2 =
    { "boardIndex", iocshArgInt };
static const iocshArg GpibBoardDriverConfigArg3 =
    { "timeout", iocshArgDouble };
static const iocshArg GpibBoardDriverConfigArg4 =
    { "priority", iocshArgInt };

static const iocshArg *GpibBoardDriverConfigArgs[] = {
    &GpibBoardDriverConfigArg0,&GpibBoardDriverConfigArg1,&GpibBoardDriverConfigArg2,&GpibBoardDriverConfigArg3,&GpibBoardDriverConfigArg4};
static const iocshFuncDef GpibBoardDriverConfigFuncDef = {
    "GpibBoardDriverConfig",5, GpibBoardDriverConfigArgs};
static void GpibBoardDriverConfigCallFunc(const iocshArgBuf *args)
{
     GpibBoardDriverConfig(args[0].sval,args[1].ival,args[2].ival,args[3].dval,args[4].ival);
}

static void GpibBoardDriverRegister(void)
{
    static int firstTime = 1;
    if (firstTime) {
        firstTime = 0;
        iocshRegister(&GpibBoardDriverConfigFuncDef, GpibBoardDriverConfigCallFunc);
    }
}
epicsExportRegistrar(GpibBoardDriverRegister);
