/*
 * asynPortDriver.cpp
 * 
 * Base class that implements methods for asynStandardInterfaces with a parameter library.
 *
 * Author: Mark Rivers
 *
 * Created April 27, 2008
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

#include <epicsString.h>
#include <epicsMutex.h>
#include <epicsThread.h>
#include <cantProceed.h>
/* NOTE: This is needed only for interruptAccept, which is hopefully a temporary workaround 
 * until EPICS supports PINI after interruptAccept */
#include <dbAccess.h>

#include <asynStandardInterfaces.h>

#include "asynPortDriver.h"

static const char *driverName = "asynPortDriver";

/* This thread waits for interruptAccept and then does all the callbacks once.
   THIS SHOULD BE A TEMPORARY FIX until EPICS supports PINI after interruptAccept */
static void callbackTaskC(void *drvPvt)
{
    asynPortDriver *pPvt = (asynPortDriver *)drvPvt;
    
    pPvt->callbackTask();
}

void asynPortDriver::callbackTask()
{
    int addr;
    
    while(!interruptAccept) epicsThreadSleep(0.1);
    epicsMutexLock(this->mutexId);
    for (addr=0; addr<this->maxAddr; addr++) {
        callParamCallbacks(addr, addr);
    }
    epicsMutexUnlock(this->mutexId);
}


paramList::paramList(int startVal, int nValsIn, asynStandardInterfaces *pasynInterfaces)
    : startVal(startVal), nVals(nValsIn), nFlags(0), pasynInterfaces(pasynInterfaces)
{
     vals = (paramVal *) calloc(nVals, sizeof(paramVal));
     flags = (int *) calloc(nVals, sizeof(int));
}

paramList::~paramList()
{
    free(vals);
    free(flags);
}

asynStatus paramList::setFlag(int index)
{
    asynStatus status = asynError;

    if (index >= 0 && index < this->nVals)
    {
        int i;
        /* See if we have already set the flag for this parameter */
        for (i=0; i<this->nFlags; i++) if (this->flags[i] == index) break;
        /* If not found add a flag */
        if (i == this->nFlags) this->flags[this->nFlags++] = index;
        status = asynSuccess;
    }
    return status;
}

asynStatus paramList::setInteger(int index, int value)
{
    asynStatus status = asynError;

    index -= this->startVal;
    if (index >= 0 && index < this->nVals)
    {
        if ( this->vals[index].type != paramInt ||
             this->vals[index].data.ival != value )
        {
            setFlag(index);
            this->vals[index].type = paramInt;
            this->vals[index].data.ival = value;
        }
        status = asynSuccess;
    }
    return status;
}

asynStatus paramList::setDouble(int index, double value)
{
    asynStatus status = asynError;

    index -= this->startVal;
    if (index >=0 && index < this->nVals)
    {
        if ( this->vals[index].type != paramDouble ||
             this->vals[index].data.dval != value )
        {
            setFlag(index);
            this->vals[index].type = paramDouble;
            this->vals[index].data.dval = value;
        }
        status = asynSuccess;
    }
    return status;
}

asynStatus paramList::setString(int index, const char *value)
{
    asynStatus status = asynError;

    index -= this->startVal;
    if (index >=0 && index < this->nVals)
    {
        if ( this->vals[index].type != paramString ||
             strcmp(this->vals[index].data.sval, value))
        {
            setFlag(index);
            this->vals[index].type = paramString;
            free(this->vals[index].data.sval);
            this->vals[index].data.sval = epicsStrDup(value);
        }
        status = asynSuccess;
    }
    return status;
}

asynStatus paramList::getInteger(int index, int *value)
{
    asynStatus status = asynError;

    index -= this->startVal;
    *value = 0;
    if (index >= 0 && index < this->nVals)
    {
        if (this->vals[index].type == paramInt) {
            *value = this->vals[index].data.ival;
            status = asynSuccess;
        }
    }
    return status;
}

asynStatus paramList::getDouble(int index, double *value)
{
    asynStatus status = asynError;

    index -= this->startVal;
    *value = 0.;
    if (index >= 0 && index < this->nVals)
    {
        if (this->vals[index].type == paramDouble) {
            *value = this->vals[index].data.dval;
            status = asynSuccess;
        }
    }
    return status;
}

asynStatus paramList::getString(int index, int maxChars, char *value)
{
    asynStatus status = asynError;

    index -= this->startVal;
    value[0]=0;
    if (index >= 0 && index < this->nVals)
    {
        if (this->vals[index].type == paramString) {
            if (maxChars > 0) {
                strncpy(value, this->vals[index].data.sval, maxChars-1);
                value[maxChars-1] = '\0';
            }
            status = asynSuccess;
        }
    }
    return status;
}

asynStatus paramList::intCallback(int command, int addr, int value)
{
    ELLLIST *pclientList;
    interruptNode *pnode;
    asynStandardInterfaces *pInterfaces = this->pasynInterfaces;
    int address;

    /* Pass int32 interrupts */
    if (!pInterfaces->int32InterruptPvt) return(asynError);
    pasynManager->interruptStart(pInterfaces->int32InterruptPvt, &pclientList);
    pnode = (interruptNode *)ellFirst(pclientList);
    while (pnode) {
        asynInt32Interrupt *pInterrupt = (asynInt32Interrupt *) pnode->drvPvt;
        pasynManager->getAddr(pInterrupt->pasynUser, &address);
        /* If this is not a multi-device then address is -1, change to 0 */
        if (address == -1) address = 0;
        if ((command == pInterrupt->pasynUser->reason) &&
            (address == addr)) {
            pInterrupt->callback(pInterrupt->userPvt, 
                                 pInterrupt->pasynUser,
                                 value);
        }
        pnode = (interruptNode *)ellNext(&pnode->node);
    }
    pasynManager->interruptEnd(pInterfaces->int32InterruptPvt);
    return(asynSuccess);
}

asynStatus paramList::doubleCallback(int command, int addr, double value)
{
    ELLLIST *pclientList;
    interruptNode *pnode;
    asynStandardInterfaces *pInterfaces = this->pasynInterfaces;
    int address;

    /* Pass float64 interrupts */
    if (!pInterfaces->float64InterruptPvt) return(asynError);
    pasynManager->interruptStart(pInterfaces->float64InterruptPvt, &pclientList);
    pnode = (interruptNode *)ellFirst(pclientList);
    while (pnode) {
        asynFloat64Interrupt *pInterrupt = (asynFloat64Interrupt *) pnode->drvPvt;
        pasynManager->getAddr(pInterrupt->pasynUser, &address);
        /* If this is not a multi-device then address is -1, change to 0 */
        if (address == -1) address = 0;
        if ((command == pInterrupt->pasynUser->reason) &&
            (address == addr)) {
            pInterrupt->callback(pInterrupt->userPvt, 
                                 pInterrupt->pasynUser,
                                 value);
        }
        pnode = (interruptNode *)ellNext(&pnode->node);
    }
    pasynManager->interruptEnd(pInterfaces->float64InterruptPvt);
    return(asynSuccess);
}

asynStatus paramList::stringCallback(int command, int addr, char *value)
{
    ELLLIST *pclientList;
    interruptNode *pnode;
    asynStandardInterfaces *pInterfaces = this->pasynInterfaces;
    int address;

    /* Pass octet interrupts */
    if (!pInterfaces->octetInterruptPvt) return(asynError);
    pasynManager->interruptStart(pInterfaces->octetInterruptPvt, &pclientList);
    pnode = (interruptNode *)ellFirst(pclientList);
    while (pnode) {
        asynOctetInterrupt *pInterrupt = (asynOctetInterrupt *) pnode->drvPvt;
        pasynManager->getAddr(pInterrupt->pasynUser, &address);
        /* If this is not a multi-device then address is -1, change to 0 */
        if (address == -1) address = 0;
        if ((command == pInterrupt->pasynUser->reason) &&
            (address == addr)) {
            pInterrupt->callback(pInterrupt->userPvt, 
                                 pInterrupt->pasynUser,
                                 value, strlen(value), ASYN_EOM_END);
        }
        pnode = (interruptNode *)ellNext(&pnode->node);
    }
    pasynManager->interruptEnd(pInterfaces->octetInterruptPvt);
    return(asynSuccess);
}

asynStatus paramList::callCallbacks(int addr)
{
    int i, index;
    int command;
    asynStatus status = asynSuccess;

    /* TEMPORARY FIX.  Dont do anything if interruptAccept=0.  There is now a thread that will
     * do all callbacks once when interruptAccept goes to 1.
     * THIS SHOULD BE A TEMPORARY FIX until EPICS supports PINI after interruptAccept */
    if (!interruptAccept) return(status);
    
    for (i = 0; i < this->nFlags; i++)
    {
        index = this->flags[i];
        command = index + this->startVal;
        switch(this->vals[index].type) {
            case paramUndef:
                break;
            case paramInt:
                status = intCallback(command, addr, this->vals[index].data.ival);
                break;
            case paramDouble:
                status = doubleCallback(command, addr, this->vals[index].data.dval);
                break;
            case paramString:
                status = stringCallback(command, addr, this->vals[index].data.sval);
                break;
        }
    }
    this->nFlags=0;
    return(status);
}

asynStatus paramList::callCallbacks()
{
    return(callCallbacks(0));
}

void paramList::report()
{
    int i;

    printf( "Number of parameters is: %d\n", this->nVals );
    for (i=0; i<this->nVals; i++)
    {
        switch (this->vals[i].type)
        {
            case paramDouble:
                printf( "Parameter %d is a double, value %f\n", i+this->startVal, this->vals[i].data.dval );
                break;
            case paramInt:
                printf( "Parameter %d is an integer, value %d\n", i+this->startVal, this->vals[i].data.ival );
                break;
            case paramString:
                printf( "Parameter %d is a string, value %s\n", i+this->startVal, this->vals[i].data.sval );
                break;
            default:
                printf( "Parameter %d is undefined\n", i+this->startVal );
                break;
        }
    }
}

asynStatus asynPortDriver::setIntegerParam(int index, int value)
{
    return this->params[0]->setInteger(index, value);
}

asynStatus asynPortDriver::setIntegerParam(int list, int index, int value)
{
    return this->params[list]->setInteger(index, value);
}

asynStatus asynPortDriver::setDoubleParam(int index, double value)
{
    return this->params[0]->setDouble(index, value);
}

asynStatus asynPortDriver::setDoubleParam(int list, int index, double value)
{
    return this->params[list]->setDouble(index, value);
}

asynStatus asynPortDriver::setStringParam(int index, const char *value)
{
    return this->params[0]->setString(index, value);
}

asynStatus asynPortDriver::setStringParam(int list, int index, const char *value)
{
    return this->params[list]->setString(index, value);
}


asynStatus asynPortDriver::getIntegerParam(int index, int *value)
{
    return this->params[0]->getInteger(index, value);
}

asynStatus asynPortDriver::getIntegerParam(int list, int index, int *value)
{
    return this->params[list]->getInteger(index, value);
}

asynStatus asynPortDriver::getDoubleParam(int index, double *value)
{
    return this->params[0]->getDouble(index, value);
}

asynStatus asynPortDriver::getDoubleParam(int list, int index, double *value)
{
    return this->params[list]->getDouble(index, value);
}

asynStatus asynPortDriver::getStringParam(int index, int maxChars, char *value)
{
    return this->params[0]->getString(index, maxChars, value);
}

asynStatus asynPortDriver::getStringParam(int list, int index, int maxChars, char *value)
{
    return this->params[list]->getString(index, maxChars, value);
}

asynStatus asynPortDriver::callParamCallbacks()
{
    return this->params[0]->callCallbacks();
}

asynStatus asynPortDriver::callParamCallbacks(int list, int addr)
{
    return this->params[list]->callCallbacks(addr);
}

void asynPortDriver::reportParams()
{
    int i;
    for (i=0; i<this->maxAddr; i++) this->params[i]->report();
}


template <typename epicsType> 
asynStatus readArray(asynUser *pasynUser, epicsType *value, size_t nElements, size_t *nIn)
{
    epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize, 
                "%s:readArray not implemented", driverName);
    return(asynError);
}

template <typename epicsType> 
asynStatus writeArray(asynUser *pasynUser, epicsType *value, size_t nElements)
{
    epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize, 
                "%s:writeArray not implemented", driverName);
    return(asynError);
}


template <typename epicsType, typename interruptType> 
asynStatus doCallbacksArray(epicsType *value, size_t nElements,
                            int reason, int address, void *interruptPvt)
{
    ELLLIST *pclientList;
    interruptNode *pnode;
    int addr;

    pasynManager->interruptStart(interruptPvt, &pclientList);
    pnode = (interruptNode *)ellFirst(pclientList);
    while (pnode) {
        interruptType *pInterrupt = (interruptType *)pnode->drvPvt;
        pasynManager->getAddr(pInterrupt->pasynUser, &addr);
        /* If this is not a multi-device then address is -1, change to 0 */
        if (addr == -1) addr = 0;
        if ((pInterrupt->pasynUser->reason == reason) &&
            (address == addr)) {
            pInterrupt->callback(pInterrupt->userPvt,
                                 pInterrupt->pasynUser,
                                 value, nElements);
        }
        pnode = (interruptNode *)ellNext(&pnode->node);
    }
    pasynManager->interruptEnd(interruptPvt);
    return(asynSuccess);
}

template <typename interruptType> 
void reportInterrupt(FILE *fp, void *interruptPvt, const char *interruptTypeString)
{
    ELLLIST *pclientList;
    interruptNode *pnode;
    
    if (interruptPvt) {
        pasynManager->interruptStart(interruptPvt, &pclientList);
        pnode = (interruptNode *)ellFirst(pclientList);
        while (pnode) {
            interruptType *pInterrupt = (interruptType *)pnode->drvPvt;
            fprintf(fp, "    %s callback client address=%p, addr=%d, reason=%d, userPvt=%p\n",
                    interruptTypeString, pInterrupt->callback, pInterrupt->addr,
                    pInterrupt->pasynUser->reason, pInterrupt->userPvt);
            pnode = (interruptNode *)ellNext(&pnode->node);
        }
        pasynManager->interruptEnd(interruptPvt);
    }
}

asynStatus asynPortDriver::getAddress(asynUser *pasynUser, const char *functionName, int *address) 
{
    pasynManager->getAddr(pasynUser, address);
    /* If this is not a multi-device then address is -1, change to 0 */
    if (*address == -1) *address = 0;
    if (*address > this->maxAddr-1) {
        epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize,
            "%s:%s: invalid address=%d, max=%d",
            driverName, functionName, *address, this->maxAddr-1);
        return(asynError);
    }
    return(asynSuccess);
}


/* asynInt32 interface methods */
extern "C" {static asynStatus readInt32(void *drvPvt, asynUser *pasynUser, 
                            epicsInt32 *value)
{
    asynPortDriver *pPvt = (asynPortDriver *)drvPvt;
    asynStatus status;
    
    epicsMutexLock(pPvt->mutexId);
    status = pPvt->readInt32(pasynUser, value);
    epicsMutexUnlock(pPvt->mutexId);
    return(status);
}}

asynStatus asynPortDriver::readInt32(asynUser *pasynUser, epicsInt32 *value)
{
    int function = pasynUser->reason;
    int addr=0;
    asynStatus status = asynSuccess;
    const char *functionName = "readInt32";
    
    status = getAddress(pasynUser, functionName, &addr); if (status != asynSuccess) return(status);
    /* We just read the current value of the parameter from the parameter library.
     * Those values are updated whenever anything could cause them to change */
    status = (asynStatus) getIntegerParam(addr, function, value);
    if (status) 
        epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize, 
                  "%s:%s: status=%d, function=%d, value=%d", 
                  driverName, functionName, status, function, *value);
    else        
        asynPrint(pasynUser, ASYN_TRACEIO_DRIVER, 
              "%s:%s: function=%d, value=%d\n", 
              driverName, functionName, function, *value);
    return(status);
}

extern "C" {static asynStatus writeInt32(void *drvPvt, asynUser *pasynUser, 
                            epicsInt32 value)
{
    asynPortDriver *pPvt = (asynPortDriver *)drvPvt;
    asynStatus status;
    
    epicsMutexLock(pPvt->mutexId);
    status = pPvt->writeInt32(pasynUser, value);
    epicsMutexUnlock(pPvt->mutexId);
    return(status);
}}

asynStatus asynPortDriver::writeInt32(asynUser *pasynUser, epicsInt32 value)
{
    int function = pasynUser->reason;
    int addr=0;
    asynStatus status = asynSuccess;
    const char* functionName = "writeInt32";

    status = getAddress(pasynUser, functionName, &addr); if (status != asynSuccess) return(status);

    /* Set the parameter in the parameter library. */
    status = (asynStatus) setIntegerParam(addr, function, value);

    /* Do callbacks so higher layers see any changes */
    status = (asynStatus) callParamCallbacks(addr, addr);
    
    if (status) 
        epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize, 
                  "%s:%s: status=%d, function=%d, value=%d", 
                  driverName, functionName, status, function, value);
    else        
        asynPrint(pasynUser, ASYN_TRACEIO_DRIVER, 
              "%s:%s: function=%d, value=%d\n", 
              driverName, functionName, function, value);
    return status;
}

extern "C" {static asynStatus getBounds(void *drvPvt, asynUser *pasynUser,
                            epicsInt32 *low, epicsInt32 *high)
{
    asynPortDriver *pPvt = (asynPortDriver *)drvPvt;
    asynStatus status;
    
    epicsMutexLock(pPvt->mutexId);
    status = pPvt->getBounds(pasynUser, low, high);
    epicsMutexUnlock(pPvt->mutexId);
    return(status);
}}

asynStatus asynPortDriver::getBounds(asynUser *pasynUser,
                                   epicsInt32 *low, epicsInt32 *high)
{
    /* This is only needed for the asynInt32 interface when the device uses raw units.
       Our interface is using engineering units. */
    *low = 0;
    *high = 65535;
    asynPrint(pasynUser, ASYN_TRACEIO_DRIVER,
              "%s::getBounds,low=%d, high=%d\n", 
              driverName, *low, *high);
    return(asynSuccess);
}


/* asynFloat64 interface methods */
extern "C" {static asynStatus readFloat64(void *drvPvt, asynUser *pasynUser,
                              epicsFloat64 *value)
{
    asynPortDriver *pPvt = (asynPortDriver *)drvPvt;
    asynStatus status;
    
    epicsMutexLock(pPvt->mutexId);
    status = pPvt->readFloat64(pasynUser, value);
    epicsMutexUnlock(pPvt->mutexId);
    return(status);
}}

asynStatus asynPortDriver::readFloat64(asynUser *pasynUser, epicsFloat64 *value)
{
    int function = pasynUser->reason;
    int addr=0;
    asynStatus status = asynSuccess;
    const char *functionName = "readFloat64";
    
    status = getAddress(pasynUser, functionName, &addr); if (status != asynSuccess) return(status);
    /* We just read the current value of the parameter from the parameter library.
     * Those values are updated whenever anything could cause them to change */
    status = (asynStatus) getDoubleParam(addr, function, value);
    if (status) 
        epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize, 
                  "%s:%s: status=%d, function=%d, value=%f", 
                  driverName, functionName, status, function, *value);
    else        
        asynPrint(pasynUser, ASYN_TRACEIO_DRIVER, 
              "%s:%s: function=%d, value=%f\n", 
              driverName, functionName, function, *value);
    return(status);
}

extern "C" {static asynStatus writeFloat64(void *drvPvt, asynUser *pasynUser,
                              epicsFloat64 value)
{
    asynPortDriver *pPvt = (asynPortDriver *)drvPvt;
    asynStatus status;
    
    epicsMutexLock(pPvt->mutexId);
    status = pPvt->writeFloat64(pasynUser, value);
    epicsMutexUnlock(pPvt->mutexId);
    return(status);
}}

asynStatus asynPortDriver::writeFloat64(asynUser *pasynUser, epicsFloat64 value)
{
    int function = pasynUser->reason;
    asynStatus status = asynSuccess;
    int addr=0;
    const char *functionName = "writeFloat64";

    status = getAddress(pasynUser, functionName, &addr); if (status != asynSuccess) return(status);
 
    /* Set the parameter and readback in the parameter library. */
    status = setDoubleParam(addr, function, value);

    /* Do callbacks so higher layers see any changes */
    callParamCallbacks(addr, addr);
    if (status) 
        asynPrint(pasynUser, ASYN_TRACE_ERROR, 
              "%s:%s: error, status=%d function=%d, value=%f\n", 
              driverName, functionName, status, function, value);
    else        
        asynPrint(pasynUser, ASYN_TRACEIO_DRIVER, 
              "%s:%s: function=%d, value=%f\n", 
              driverName, functionName, function, value);
    return status;
}



/* asynOctet interface methods */
extern "C" {static asynStatus readOctet(void *drvPvt, asynUser *pasynUser,
                            char *value, size_t maxChars, size_t *nActual,
                            int *eomReason)
{
    asynPortDriver *pPvt = (asynPortDriver *)drvPvt;
    asynStatus status;
    
    epicsMutexLock(pPvt->mutexId);
    status = pPvt->readOctet(pasynUser, value, maxChars, nActual, eomReason);
    epicsMutexUnlock(pPvt->mutexId);
    return(status);
}}

asynStatus asynPortDriver::readOctet(asynUser *pasynUser,
                            char *value, size_t maxChars, size_t *nActual,
                            int *eomReason)
{
    int function = pasynUser->reason;
    int addr=0;
    asynStatus status = asynSuccess;
    const char *functionName = "readOctet";
   
    status = getAddress(pasynUser, functionName, &addr); if (status != asynSuccess) return(status);
    /* We just read the current value of the parameter from the parameter library.
     * Those values are updated whenever anything could cause them to change */
    status = (asynStatus)getStringParam(addr, function, maxChars, value);
    if (status) 
        epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize, 
                  "%s:%s: status=%d, function=%d, value=%s", 
                  driverName, functionName, status, function, value);
    else        
        asynPrint(pasynUser, ASYN_TRACEIO_DRIVER, 
              "%s:%s: function=%d, value=%s\n", 
              driverName, functionName, function, value);
    *eomReason = ASYN_EOM_END;
    *nActual = strlen(value);
    return(status);
}

extern "C" {static asynStatus writeOctet(void *drvPvt, asynUser *pasynUser,
                              const char *value, size_t maxChars, size_t *nActual)
{
    asynPortDriver *pPvt = (asynPortDriver *)drvPvt;
    asynStatus status;
    
    epicsMutexLock(pPvt->mutexId);
    status = pPvt->writeOctet(pasynUser, value, maxChars, nActual);
    epicsMutexUnlock(pPvt->mutexId);
    return(status);
}}

asynStatus asynPortDriver::writeOctet(asynUser *pasynUser, const char *value, 
                                    size_t nChars, size_t *nActual)
{
    int addr=0;
    int function = pasynUser->reason;
    asynStatus status = asynSuccess;
    const char *functionName = "writeOctet";

    status = getAddress(pasynUser, functionName, &addr); if (status != asynSuccess) return(status);

    /* Set the parameter in the parameter library. */
    status = (asynStatus)setStringParam(addr, function, (char *)value);

     /* Do callbacks so higher layers see any changes */
    status = (asynStatus)callParamCallbacks(addr, addr);

    if (status) 
        epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize, 
                  "%s:%s: status=%d, function=%d, value=%s", 
                  driverName, functionName, status, function, value);
    else        
        asynPrint(pasynUser, ASYN_TRACEIO_DRIVER, 
              "%s:%s: function=%d, value=%s\n", 
              driverName, functionName, function, value);
    *nActual = nChars;
    return status;
}




/* asynInt8Array interface methods */
extern "C" {static asynStatus readInt8Array(void *drvPvt, asynUser *pasynUser, epicsInt8 *value,
                                size_t nElements, size_t *nIn)
{
    asynPortDriver *pPvt = (asynPortDriver *)drvPvt;
    asynStatus status;
    
    epicsMutexLock(pPvt->mutexId);
    status = pPvt->readInt8Array(pasynUser, value, nElements, nIn);
    epicsMutexUnlock(pPvt->mutexId);
    return(status);
}}

asynStatus asynPortDriver::readInt8Array(asynUser *pasynUser, epicsInt8 *value,
                                size_t nElements, size_t *nIn)
{
    return(readArray<epicsInt8>(pasynUser, value, nElements, nIn));
}

extern "C" {static asynStatus writeInt8Array(void *drvPvt, asynUser *pasynUser, epicsInt8 *value,
                                size_t nElements)
{
    asynPortDriver *pPvt = (asynPortDriver *)drvPvt;
    asynStatus status;
    
    epicsMutexLock(pPvt->mutexId);
    status = pPvt->writeInt8Array(pasynUser, value, nElements);
    epicsMutexUnlock(pPvt->mutexId);
    return(status);    
}}

asynStatus asynPortDriver::writeInt8Array(asynUser *pasynUser, epicsInt8 *value,
                                size_t nElements)
{
    return(writeArray<epicsInt8>(pasynUser, value, nElements));
}

asynStatus asynPortDriver::doCallbacksInt8Array(epicsInt8 *value,
                                size_t nElements, int reason, int addr)
{
    return(doCallbacksArray<epicsInt8, asynInt8ArrayInterrupt>(value, nElements, reason, addr,
                                        this->asynStdInterfaces.int8ArrayInterruptPvt));
}


/* asynInt16Array interface methods */
extern "C" {static asynStatus readInt16Array(void *drvPvt, asynUser *pasynUser, epicsInt16 *value,
                                size_t nElements, size_t *nIn)
{
    asynPortDriver *pPvt = (asynPortDriver *)drvPvt;
    asynStatus status;
    
    epicsMutexLock(pPvt->mutexId);
    status = pPvt->readInt16Array(pasynUser, value, nElements, nIn);
    epicsMutexUnlock(pPvt->mutexId);
    return(status);    
}}

asynStatus asynPortDriver::readInt16Array(asynUser *pasynUser, epicsInt16 *value,
                                size_t nElements, size_t *nIn)
{
    return(readArray<epicsInt16>(pasynUser, value, nElements, nIn));
}

extern "C" {static asynStatus writeInt16Array(void *drvPvt, asynUser *pasynUser, epicsInt16 *value,
                                size_t nElements)
{
    asynPortDriver *pPvt = (asynPortDriver *)drvPvt;
    asynStatus status;
    
    epicsMutexLock(pPvt->mutexId);
    status = pPvt->writeInt16Array(pasynUser, value, nElements);
    epicsMutexUnlock(pPvt->mutexId);
    return(status);    
}}

asynStatus asynPortDriver::writeInt16Array(asynUser *pasynUser, epicsInt16 *value,
                                size_t nElements)
{
    return(writeArray<epicsInt16>(pasynUser, value, nElements));
}

asynStatus asynPortDriver::doCallbacksInt16Array(epicsInt16 *value,
                                size_t nElements, int reason, int addr)
{
    return(doCallbacksArray<epicsInt16, asynInt16ArrayInterrupt>(value, nElements, reason, addr,
                                        this->asynStdInterfaces.int16ArrayInterruptPvt));
}


/* asynInt32Array interface methods */
extern "C" {static asynStatus readInt32Array(void *drvPvt, asynUser *pasynUser, epicsInt32 *value,
                                size_t nElements, size_t *nIn)
{
    asynPortDriver *pPvt = (asynPortDriver *)drvPvt;
    
    return(pPvt->readInt32Array(pasynUser, value, nElements, nIn));
}}

asynStatus asynPortDriver::readInt32Array(asynUser *pasynUser, epicsInt32 *value,
                                size_t nElements, size_t *nIn)
{
    return(readArray<epicsInt32>(pasynUser, value, nElements, nIn));
}

extern "C" {static asynStatus writeInt32Array(void *drvPvt, asynUser *pasynUser, epicsInt32 *value,
                                size_t nElements)
{
    asynPortDriver *pPvt = (asynPortDriver *)drvPvt;
    asynStatus status;
    
    epicsMutexLock(pPvt->mutexId);
    status = pPvt->writeInt32Array(pasynUser, value, nElements);
    epicsMutexUnlock(pPvt->mutexId);
    return(status);    
}}

asynStatus asynPortDriver::writeInt32Array(asynUser *pasynUser, epicsInt32 *value,
                                size_t nElements)
{
    return(writeArray<epicsInt32>(pasynUser, value, nElements));
}

asynStatus asynPortDriver::doCallbacksInt32Array(epicsInt32 *value,
                                size_t nElements, int reason, int addr)
{
    return(doCallbacksArray<epicsInt32, asynInt32ArrayInterrupt>(value, nElements, reason, addr,
                                        this->asynStdInterfaces.int32ArrayInterruptPvt));
}


/* asynFloat32Array interface methods */
extern "C" {static asynStatus readFloat32Array(void *drvPvt, asynUser *pasynUser, epicsFloat32 *value,
                                size_t nElements, size_t *nIn)
{
    asynPortDriver *pPvt = (asynPortDriver *)drvPvt;
    asynStatus status;
    
    epicsMutexLock(pPvt->mutexId);
    status = pPvt->readFloat32Array(pasynUser, value, nElements, nIn);
    epicsMutexUnlock(pPvt->mutexId);
    return(status);    
}}

asynStatus asynPortDriver::readFloat32Array(asynUser *pasynUser, epicsFloat32 *value,
                                size_t nElements, size_t *nIn)
{
    return(readArray<epicsFloat32>(pasynUser, value, nElements, nIn));
}

extern "C" {static asynStatus writeFloat32Array(void *drvPvt, asynUser *pasynUser, epicsFloat32 *value,
                                size_t nElements)
{
    asynPortDriver *pPvt = (asynPortDriver *)drvPvt;
    asynStatus status;
    
    epicsMutexLock(pPvt->mutexId);
    status = pPvt->writeFloat32Array(pasynUser, value, nElements);
    epicsMutexUnlock(pPvt->mutexId);
    return(status);    
}}

asynStatus asynPortDriver::writeFloat32Array(asynUser *pasynUser, epicsFloat32 *value,
                                size_t nElements)
{
    return(writeArray<epicsFloat32>(pasynUser, value, nElements));
}

asynStatus asynPortDriver::doCallbacksFloat32Array(epicsFloat32 *value,
                                size_t nElements, int reason, int addr)
{
    return(doCallbacksArray<epicsFloat32, asynFloat32ArrayInterrupt>(value, nElements, reason, addr,
                                        this->asynStdInterfaces.float32ArrayInterruptPvt));
}


/* asynFloat64Array interface methods */
extern "C" {static asynStatus readFloat64Array(void *drvPvt, asynUser *pasynUser, epicsFloat64 *value,
                                size_t nElements, size_t *nIn)
{
    asynPortDriver *pPvt = (asynPortDriver *)drvPvt;
    asynStatus status;
    
    epicsMutexLock(pPvt->mutexId);
    status = pPvt->readFloat64Array(pasynUser, value, nElements, nIn);
    epicsMutexUnlock(pPvt->mutexId);
    return(status);    
}}

asynStatus asynPortDriver::readFloat64Array(asynUser *pasynUser, epicsFloat64 *value,
                                size_t nElements, size_t *nIn)
{
    return(readArray<epicsFloat64>(pasynUser, value, nElements, nIn));
}

extern "C" {static asynStatus writeFloat64Array(void *drvPvt, asynUser *pasynUser, epicsFloat64 *value,
                                size_t nElements)
{
    asynPortDriver *pPvt = (asynPortDriver *)drvPvt;
    asynStatus status;
    
    epicsMutexLock(pPvt->mutexId);
    status = pPvt->writeFloat64Array(pasynUser, value, nElements);
    epicsMutexUnlock(pPvt->mutexId);
    return(status);    
}}

asynStatus asynPortDriver::writeFloat64Array(asynUser *pasynUser, epicsFloat64 *value,
                                size_t nElements)
{
    return(writeArray<epicsFloat64>(pasynUser, value, nElements));
}

asynStatus asynPortDriver::doCallbacksFloat64Array(epicsFloat64 *value,
                                size_t nElements, int reason, int addr)
{
    return(doCallbacksArray<epicsFloat64, asynFloat64ArrayInterrupt>(value, nElements, reason, addr,
                                        this->asynStdInterfaces.float64ArrayInterruptPvt));
}

/* asynGenericPointer interface methods */
extern "C" {static asynStatus readGenericPointer(void *drvPvt, asynUser *pasynUser, void *genericPointer)
{
    asynPortDriver *pPvt = (asynPortDriver *)drvPvt;
    asynStatus status;
    
    epicsMutexLock(pPvt->mutexId);
    status =pPvt->readGenericPointer(pasynUser, genericPointer);
    epicsMutexUnlock(pPvt->mutexId);
    return(status);    
}}

asynStatus asynPortDriver::readGenericPointer(asynUser *pasynUser, void *genericPointer)
{
    epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize, 
                "%s:readGenericPointer not implemented", driverName);
    return(asynError);
}

extern "C" {static asynStatus writeGenericPointer(void *drvPvt, asynUser *pasynUser, void *genericPointer)
{
    asynPortDriver *pPvt = (asynPortDriver *)drvPvt;
    asynStatus status;
    
    epicsMutexLock(pPvt->mutexId);
    status = pPvt->writeGenericPointer(pasynUser, genericPointer);
    epicsMutexUnlock(pPvt->mutexId);
    return(status);    
}}

asynStatus asynPortDriver::writeGenericPointer(asynUser *pasynUser, void *genericPointer)
{
    epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize, 
                "%s:writeGenericPointer not implemented", driverName);
    return(asynError);
}


asynStatus asynPortDriver::doCallbacksGenericPointer(void *genericPointer, int reason, int address)
{
    ELLLIST *pclientList;
    interruptNode *pnode;
    int addr;

    pasynManager->interruptStart(this->asynStdInterfaces.genericPointerInterruptPvt, &pclientList);
    pnode = (interruptNode *)ellFirst(pclientList);
    while (pnode) {
        asynGenericPointerInterrupt *pInterrupt = (asynGenericPointerInterrupt *)pnode->drvPvt;
        pasynManager->getAddr(pInterrupt->pasynUser, &addr);
        /* If this is not a multi-device then address is -1, change to 0 */
        if (addr == -1) addr = 0;
        if ((pInterrupt->pasynUser->reason == reason) &&
            (address == addr)) {
            pInterrupt->callback(pInterrupt->userPvt,
                                 pInterrupt->pasynUser,
                                 genericPointer);
        }
        pnode = (interruptNode *)ellNext(&pnode->node);
    }
    pasynManager->interruptEnd(this->asynStdInterfaces.genericPointerInterruptPvt);
    return(asynSuccess);
}



asynStatus asynPortDriver::findParam(asynParamString_t *paramTable, int numParams, 
                                    const char *paramName, int *param)
{
    int i;
    for (i=0; i < numParams; i++) {
        if (epicsStrCaseCmp(paramName, paramTable[i].paramString) == 0) {
            *param = paramTable[i].param;
            return(asynSuccess);
        }
    }
    return(asynError);
}

/* asynDrvUser interface methods */
extern "C" {static asynStatus drvUserCreate(void *drvPvt, asynUser *pasynUser,
                                 const char *drvInfo, 
                                 const char **pptypeName, size_t *psize)
{ 
    asynPortDriver *pPvt = (asynPortDriver *)drvPvt;
    asynStatus status;
    
    epicsMutexLock(pPvt->mutexId);
    status = pPvt->drvUserCreate(pasynUser, drvInfo, pptypeName, psize);
    epicsMutexUnlock(pPvt->mutexId);
    return(status);    
}}

asynStatus asynPortDriver::drvUserCreate(asynUser *pasynUser,
                                       const char *drvInfo, 
                                       const char **pptypeName, size_t *psize)
{
    const char *functionName = "drvUserCreate";
    
    asynPrint(pasynUser, ASYN_TRACE_FLOW,
              "%s:%s: entered", driverName, functionName);

    return(asynSuccess);

}

    
extern "C" {static asynStatus drvUserGetType(void *drvPvt, asynUser *pasynUser,
                                 const char **pptypeName, size_t *psize)
{ 
    asynPortDriver *pPvt = (asynPortDriver *)drvPvt;
    asynStatus status;
    
    epicsMutexLock(pPvt->mutexId);
    status = pPvt->drvUserGetType(pasynUser, pptypeName, psize);
    epicsMutexUnlock(pPvt->mutexId);
    return(status);    
}}

asynStatus asynPortDriver::drvUserGetType(asynUser *pasynUser,
                                        const char **pptypeName, size_t *psize)
{
    /* This is not currently supported, because we can't get the strings for driver-specific commands */
    const char *functionName = "drvUserGetType";

    asynPrint(pasynUser, ASYN_TRACE_FLOW,
              "%s:%s: entered", driverName, functionName);

    *pptypeName = NULL;
    *psize = 0;
    return(asynError);
}

extern "C" {static asynStatus drvUserDestroy(void *drvPvt, asynUser *pasynUser)
{ 
    asynPortDriver *pPvt = (asynPortDriver *)drvPvt;
    asynStatus status;
    
    epicsMutexLock(pPvt->mutexId);
    status = pPvt->drvUserDestroy(pasynUser);
    epicsMutexUnlock(pPvt->mutexId);
    return(status);    
}}

asynStatus asynPortDriver::drvUserDestroy(asynUser *pasynUser)
{
    const char *functionName = "drvUserDestroy";

    asynPrint(pasynUser, ASYN_TRACE_FLOW,
              "%s:%s: this=%p, pasynUser=%p\n",
              driverName, functionName, this, pasynUser);

    return(asynSuccess);
}


/* asynCommon interface methods */

extern "C" {static void report(void *drvPvt, FILE *fp, int details)
{
    asynPortDriver *pPvt = (asynPortDriver *)drvPvt;
    
    epicsMutexLock(pPvt->mutexId);
    pPvt->report(fp, details);
    epicsMutexUnlock(pPvt->mutexId);
}}


void asynPortDriver::report(FILE *fp, int details)
{
    int addr;
    asynStandardInterfaces *pInterfaces = &this->asynStdInterfaces;

    fprintf(fp, "Port: %s\n", this->portName);
    if (details >= 1) {
        /* Report interrupt clients */
        reportInterrupt<asynInt32Interrupt>         (fp, pInterfaces->int32InterruptPvt,        "int32");
        reportInterrupt<asynFloat64Interrupt>       (fp, pInterfaces->float64InterruptPvt,      "float64");
        reportInterrupt<asynOctetInterrupt>         (fp, pInterfaces->octetInterruptPvt,        "octet");
        reportInterrupt<asynInt8ArrayInterrupt>     (fp, pInterfaces->int8ArrayInterruptPvt,    "int8Array");
        reportInterrupt<asynInt16ArrayInterrupt>    (fp, pInterfaces->int16ArrayInterruptPvt,   "int16Array");
        reportInterrupt<asynInt32ArrayInterrupt>    (fp, pInterfaces->int32ArrayInterruptPvt,   "int32Array");
        reportInterrupt<asynFloat32ArrayInterrupt>  (fp, pInterfaces->float32ArrayInterruptPvt, "float32Array");
        reportInterrupt<asynFloat64ArrayInterrupt>  (fp, pInterfaces->float64ArrayInterruptPvt, "float64Array");
        reportInterrupt<asynGenericPointerInterrupt>(fp, pInterfaces->genericPointerInterruptPvt, "genericPointer");
    }
    if (details > 5) {
        for (addr=0; addr<this->maxAddr; addr++) {
            this->params[addr]->report();
        }
    }
}

extern "C" {static asynStatus connect(void *drvPvt, asynUser *pasynUser)
{
    asynPortDriver *pPvt = (asynPortDriver *)drvPvt;
    asynStatus status;
    
    epicsMutexLock(pPvt->mutexId);
    status = pPvt->connect(pasynUser);
    epicsMutexUnlock(pPvt->mutexId);
    return(status);    
}}

asynStatus asynPortDriver::connect(asynUser *pasynUser)
{
    const char *functionName = "connect";
    
    pasynManager->exceptionConnect(pasynUser);
    asynPrint(pasynUser, ASYN_TRACE_FLOW,
              "%s:%s:, pasynUser=%p\n", 
              driverName, functionName, pasynUser);
    return(asynSuccess);
}


extern "C" {static asynStatus disconnect(void *drvPvt, asynUser *pasynUser)
{
    asynPortDriver *pPvt = (asynPortDriver *)drvPvt;
    asynStatus status;
    
    epicsMutexLock(pPvt->mutexId);
    status = pPvt->disconnect(pasynUser);
    epicsMutexUnlock(pPvt->mutexId);
    return(status);    
}}

asynStatus asynPortDriver::disconnect(asynUser *pasynUser)
{
    const char *functionName = "disconnect";
    
    pasynManager->exceptionDisconnect(pasynUser);
    asynPrint(pasynUser, ASYN_TRACE_FLOW,
              "%s:%s:, pasynUser=%p\n", 
              driverName, functionName, pasynUser);
    return(asynSuccess);
}


/* Structures with function pointers for each of the asyn interfaces */
static asynCommon ifaceCommon = {
    report,
    connect,
    disconnect
};

static asynInt32 ifaceInt32 = {
    writeInt32,
    readInt32,
    getBounds
};

static asynFloat64 ifaceFloat64 = {
    writeFloat64,
    readFloat64
};

static asynOctet ifaceOctet = {
    writeOctet,
    readOctet,
};

static asynInt8Array ifaceInt8Array = {
    writeInt8Array,
    readInt8Array
};

static asynInt16Array ifaceInt16Array = {
    writeInt16Array,
    readInt16Array
};

static asynInt32Array ifaceInt32Array = {
    writeInt32Array,
    readInt32Array
};

static asynFloat32Array ifaceFloat32Array = {
    writeFloat32Array,
    readFloat32Array
};

static asynFloat64Array ifaceFloat64Array = {
    writeFloat64Array,
    readFloat64Array
};

static asynGenericPointer ifaceGenericPointer = {
    writeGenericPointer,
    readGenericPointer
};

static asynDrvUser ifaceDrvUser = {
    drvUserCreate,
    drvUserGetType,
    drvUserDestroy
};


/* Constructor */

asynPortDriver::asynPortDriver(const char *portNameIn, int maxAddrIn, int paramTableSize, int interfaceMask, int interruptMask,
                               int asynFlags, int autoConnect, int priority, int stackSize)
{
    asynStatus status;
    const char *functionName = "asynPortDriver";
    asynStandardInterfaces *pInterfaces;
    int addr;

    /* Initialize some members to 0 */
    pInterfaces = &this->asynStdInterfaces;
    memset(pInterfaces, 0, sizeof(asynStdInterfaces));
       
    this->portName = epicsStrDup(portNameIn);
    this->maxAddr = maxAddrIn;

    status = pasynManager->registerPort(portName,
                                        asynFlags,    /* multidevice and canblock flags */
                                        autoConnect,  /* autoconnect flag */
                                        priority,     /* priority */
                                        stackSize);   /* stack size */
    if (status != asynSuccess) {
        printf("%s:%s: ERROR: Can't register port\n", driverName, functionName);
    }

    /* Create asynUser for debugging and for standardInterfacesBase */
    this->pasynUserSelf = pasynManager->createAsynUser(0, 0);

    interfaceMask |= asynCommonMask;  /* Always need the asynCommon interface */
     /* Set addresses of asyn interfaces */
    if (interfaceMask & asynCommonMask)         pInterfaces->common.pinterface        = (void *)&ifaceCommon;
    if (interfaceMask & asynDrvUserMask)        pInterfaces->drvUser.pinterface       = (void *)&ifaceDrvUser;
    if (interfaceMask & asynInt32Mask)          pInterfaces->int32.pinterface         = (void *)&ifaceInt32;
    if (interfaceMask & asynFloat64Mask)        pInterfaces->float64.pinterface       = (void *)&ifaceFloat64;
    if (interfaceMask & asynOctetMask)          pInterfaces->octet.pinterface         = (void *)&ifaceOctet;
    if (interfaceMask & asynInt8ArrayMask)      pInterfaces->int8Array.pinterface     = (void *)&ifaceInt8Array;
    if (interfaceMask & asynInt16ArrayMask)     pInterfaces->int16Array.pinterface    = (void *)&ifaceInt16Array;
    if (interfaceMask & asynInt32ArrayMask)     pInterfaces->int32Array.pinterface    = (void *)&ifaceInt32Array;
    if (interfaceMask & asynFloat32ArrayMask)   pInterfaces->float32Array.pinterface  = (void *)&ifaceFloat32Array;
    if (interfaceMask & asynFloat64ArrayMask)   pInterfaces->float64Array.pinterface  = (void *)&ifaceFloat64Array;
    if (interfaceMask & asynGenericPointerMask) pInterfaces->genericPointer.pinterface= (void *)&ifaceGenericPointer;

    /* Define which interfaces can generate interrupts */
    if (interruptMask & asynInt32Mask)          pInterfaces->int32CanInterrupt        = 1;
    if (interruptMask & asynFloat64Mask)        pInterfaces->float64CanInterrupt      = 1;
    if (interruptMask & asynOctetMask)          pInterfaces->octetCanInterrupt        = 1;
    if (interruptMask & asynInt8ArrayMask)      pInterfaces->int8ArrayCanInterrupt    = 1;
    if (interruptMask & asynInt16ArrayMask)     pInterfaces->int16ArrayCanInterrupt   = 1;
    if (interruptMask & asynInt32ArrayMask)     pInterfaces->int32ArrayCanInterrupt   = 1;
    if (interruptMask & asynFloat32ArrayMask)   pInterfaces->float32ArrayCanInterrupt = 1;
    if (interruptMask & asynFloat64ArrayMask)   pInterfaces->float64ArrayCanInterrupt = 1;
    if (interruptMask & asynGenericPointerMask) pInterfaces->genericPointerCanInterrupt       = 1;

    status = pasynStandardInterfacesBase->initialize(portName, pInterfaces,
                                                     this->pasynUserSelf, this);
    if (status != asynSuccess) {
        printf("%s:%s ERROR: Can't register interfaces: %s.\n",
               driverName, functionName, this->pasynUserSelf->errorMessage);
        return;
    }

    /* Create the epicsMutex for locking access to data structures from other threads */
    this->mutexId = epicsMutexCreate();
    if (!this->mutexId) {
        printf("%s::%s epicsMutexCreate failure\n", driverName, functionName);
        return;
    }

    /* Allocate space for the parameter objects */
    this->params = (paramList **) calloc(maxAddr, sizeof(paramList *));    
    /* Initialize the parameter library */
    for (addr=0; addr<maxAddr; addr++) {
        this->params[addr] = new paramList(0, paramTableSize, &this->asynStdInterfaces);
    }

    /* Connect to our device for asynTrace */
    status = pasynManager->connectDevice(this->pasynUserSelf, portName, 0);
    if (status != asynSuccess) {
        printf("%s:%s:, connectDevice failed\n", driverName, functionName);
        return;
    }

    /* Create a thread that waits for interruptAccept and then does all the callbacks once.
       THIS SHOULD BE A TEMPORARY FIX until epics supports PII after interruptAccept */
    status = (asynStatus)(epicsThreadCreate("asynPortDriverCallback",
                                epicsThreadPriorityMedium,
                                epicsThreadGetStackSize(epicsThreadStackMedium),
                                (EPICSTHREADFUNC)callbackTaskC,
                                this) == NULL);
    if (status) {
        printf("%s:%s epicsThreadCreate failure for callback task\n", 
            driverName, functionName);
        return;
    }

}

asynPortDriver::~asynPortDriver()
{
    int addr;

    /* This is the destructor */
    epicsMutexDestroy(this->mutexId);
    for (addr=0; addr<this->maxAddr; addr++) {
        delete this->params[addr];
    }
    free(this->params);
}
