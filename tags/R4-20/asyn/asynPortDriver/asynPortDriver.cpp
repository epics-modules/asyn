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
/* NOTE: This is needed for interruptAccept */
#include <dbAccess.h>

#define epicsExportSharedSymbols
#include <shareLib.h>
#include "asynPortDriver.h"
#include "paramList.h"
#include "paramErrors.h"

static const char *driverName = "asynPortDriver";

/* I thought this would be a temporary fix until EPICS supported PINI after interruptAccept, which would then be used
 * for input records that need callbacks after output records that also have PINI and that could affect them. But this
 * does not work with asyn device support because of the ring buffer.  Records with SCAN=I/O Intr must not processed
 * for any other reason, including PINI, or the ring buffer can get out of sync. */
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


/** Locks the driver to prevent multiple threads from accessing memory at the same time.
  * This function is called whenever asyn clients call the functions on the asyn interfaces.
  * Drivers with their own background threads must call lock() to protect conflicts with
  * asyn clients.  They can call unlock() to permit asyn clients to run during times that the driver
  * thread is idle or is performing compute bound work that does not access memory also accessible by clients. */
asynStatus asynPortDriver::lock()
{
    int status;
    status = epicsMutexLock(this->mutexId);
    if (status) return(asynError);
    else return(asynSuccess);
}

/** Unocks the driver; called when an asyn client or driver is done accessing common memory. */
asynStatus asynPortDriver::unlock()
{
    epicsMutexUnlock(this->mutexId);
    return(asynSuccess);
}

/** Creates a parameter in the parameter library.
  * Calls paramList::createParam (list, name, index) for all parameters lists.
  * \param[in] name Parameter name
  * \param[in] type Parameter type
  * \param[out] index Parameter number */
asynStatus asynPortDriver::createParam(const char *name, asynParamType type, int *index)
{
    int list;
    asynStatus status;
    
    /* All parameters lists support the same parameters, so add the parameter name to all lists */
    for (list=0; list<this->maxAddr; list++) {
        status = createParam(list, name, type, index);
        if (status) return asynError;
    }
    return asynSuccess;
}

/** Creates a parameter in the parameter library.
  * Calls paramList::addParam (name, index) for the parameter list indexed by list.
  * \param[in] list The parameter list number.  Must be < maxAddr passed to asynPortDriver::asynPortDriver.
  * \param[in] name Parameter name
  * \param[in] type Parameter type
  * \param[out] index Parameter number */
asynStatus asynPortDriver::createParam(int list, const char *name, asynParamType type, int *index)
{
    asynStatus status;
    static const char *functionName = "createParam";
    
    status = this->params[list]->createParam(name, type, index);
    if (status == asynParamAlreadyExists) {
        asynPrint(this->pasynUserSelf, ASYN_TRACE_ERROR,
            "%s:%s: port=%s error adding parameter %s to list %d, parameter already exists.\n",
            driverName, functionName, portName, name, list);
        return(asynError);
    }
    if (status == asynParamBadIndex) {
        asynPrint(this->pasynUserSelf, ASYN_TRACE_ERROR,
            "%s:%s: port=%s error adding parameter %s to list %d, too many parameters\n",
            driverName, functionName, portName, name, list);
        return(asynError);
    }
    return asynSuccess;
}

/** Finds a parameter in the parameter library.
  * Calls findParam(0, name, index), i.e. for parameter list 0.
  * \param[in] name Parameter name
  * \param[out] index Parameter number */
asynStatus asynPortDriver::findParam(const char *name, int *index)
{
    return this->findParam(0, name, index);
}

/** Finds a parameter in the parameter library.
  * Calls paramList::findParam (name, index) for the parameter list indexed by list.
  * \param[in] list The parameter list number.  Must be < maxAddr passed to asynPortDriver::asynPortDriver.
  * \param[in] name Parameter name
  * \param[out] index Parameter number */
asynStatus asynPortDriver::findParam(int list, const char *name, int *index)
{
    return this->params[list]->findParam(name, index);
}

/** Returns the name of a parameter in the parameter library.
  * Calls getParamName(0, index, name) i.e. for parameter list 0.
  * \param[in] index Parameter number
  * \param[out] name Parameter name */
asynStatus asynPortDriver::getParamName(int index, const char **name)
{
    return this->getParamName(0, index, name);
}

/** Returns the name of a parameter in the parameter library.
  * Calls paramList::getName (index, name) for the parameter list indexed by list.
  * \param[in] list The parameter list number.  Must be < maxAddr passed to asynPortDriver::asynPortDriver.
  * \param[in] index Parameter number
  * \param[out] name Parameter name */
asynStatus asynPortDriver::getParamName(int list, int index, const char **name)
{
    return this->params[list]->getName(index, name);
}

/** Reports errors when setting parameters.  
  * \param[in] status The error status.
  * \param[in] index The parameter number
  * \param[in] list The parameter list number.  Must be < maxAddr passed to asynPortDriver::asynPortDriver.
  * \param[in] functionName The name of the function that generated the error  */
void asynPortDriver::reportSetParamErrors(asynStatus status, int index, int list, const char *functionName)
{
    if (status == asynParamBadIndex) {
        asynPrint(this->pasynUserSelf, ASYN_TRACE_ERROR,
            "%s:%s: port=%s error setting parameter %d in list %d, bad index\n",
            driverName, functionName, portName, index, list);
    }
    if (status == asynParamWrongType) {
        asynPrint(this->pasynUserSelf, ASYN_TRACE_ERROR,
            "%s:%s: port=%s error setting parameter %d in list %d, wrong type\n",
            driverName, functionName, portName, index, list);
    }
}

/** Sets the status for a parameter in the parameter library.
  * Calls setParamStatus(0, index, status) i.e. for parameter list 0.
  * \param[in] index The parameter number 
  * \param[in] status Status to set. */
asynStatus asynPortDriver::setParamStatus(int index, asynStatus status)
{
    return this->setParamStatus(0, index, status);
}

/** Sets the status for a parameter in the parameter library.
  * Calls paramList::setStatus(index, status) for the parameter list indexed by list.
  * \param[in] list The parameter list number.  Must be < maxAddr passed to asynPortDriver::asynPortDriver.
  * \param[in] index The parameter number 
  * \param[in] paramStatus Status to set. */
asynStatus asynPortDriver::setParamStatus(int list, int index, asynStatus paramStatus)
{
    asynStatus status;
    static const char *functionName = "setParamStatus";
    
    status = this->params[list]->setStatus(index, paramStatus);
    if (status) reportSetParamErrors(status, index, list, functionName);
    return(status);
}

/** Gets the status for a parameter in the parameter library.
  * Calls getParamStatus(0, index, status) i.e. for parameter list 0.
  * \param[in] index The parameter number 
  * \param[out] status Address of tatus to get. */
asynStatus asynPortDriver::getParamStatus(int index, asynStatus *status)
{
    return this->getParamStatus(0, index, status);
}

/** Gets the status for a parameter in the parameter library.
  * Calls paramList::setStatus(index, status) for the parameter list indexed by list.
  * \param[in] list The parameter list number.  Must be < maxAddr passed to asynPortDriver::asynPortDriver.
  * \param[in] index The parameter number 
  * \param[out] paramStatus Address of status to get. */
asynStatus asynPortDriver::getParamStatus(int list, int index, asynStatus *paramStatus)
{
    asynStatus status;
    static const char *functionName = "setParamStatus";
    
    status = this->params[list]->getStatus(index, paramStatus);
    if (status) reportSetParamErrors(status, index, list, functionName);
    return(status);
}

/** Sets the value for an integer in the parameter library.
  * Calls setIntegerParam(0, index, value) i.e. for parameter list 0.
  * \param[in] index The parameter number 
  * \param[in] value Value to set. */
asynStatus asynPortDriver::setIntegerParam(int index, int value)
{
    return this->setIntegerParam(0, index, value);
}

/** Sets the value for an integer in the parameter library.
  * Calls paramList::setInteger (index, value) for the parameter list indexed by list.
  * \param[in] list The parameter list number.  Must be < maxAddr passed to asynPortDriver::asynPortDriver.
  * \param[in] index The parameter number 
  * \param[in] value Value to set. */
asynStatus asynPortDriver::setIntegerParam(int list, int index, int value)
{
    asynStatus status;
    static const char *functionName = "setIntegerParam";
    
    status = this->params[list]->setInteger(index, value);
    if (status) reportSetParamErrors(status, index, list, functionName);
    return(status);
}

/** Sets the value for a UInt32Digital in the parameter library.
  * Calls setUIntDigitalParam(0, index, value, valueMask, 0) i.e. for parameter list 0.
  * \param[in] index The parameter number 
  * \param[in] value Value to set. 
  * \param[in] valueMask The mask to use when setting the value. */
asynStatus asynPortDriver::setUIntDigitalParam(int index, epicsUInt32 value, epicsUInt32 valueMask)
{
    return this->setUIntDigitalParam(0, index, value, valueMask, 0);
}

/** Sets the value for a UInt32Digital in the parameter library.
  * Calls paramList::setUIntDigitalParam(list, index, value, valueMask, 0) for the parameter list indexed by list.
  * \param[in] list The parameter list number.  Must be < maxAddr passed to asynPortDriver::asynPortDriver.
  * \param[in] index The parameter number 
  * \param[in] value Value to set. 
  * \param[in] valueMask The mask to use when setting the value. */
asynStatus asynPortDriver::setUIntDigitalParam(int list, int index, epicsUInt32 value, epicsUInt32 valueMask)
{
    return this->setUIntDigitalParam(list, index, value, valueMask, 0);
}

/** Sets the value for a UInt32Digital in the parameter library.
  * Calls setUIntDigitalParam(0, index, value, valueMask, interruptMask) i.e. for parameter list 0.
  * \param[in] index The parameter number 
  * \param[in] value Value to set. 
  * \param[in] valueMask The mask to use when setting the value. 
  * \param[in] interruptMask A mask that indicates which bits have changed even if the value is the same, so callbacks will be done */
asynStatus asynPortDriver::setUIntDigitalParam(int index, epicsUInt32 value, epicsUInt32 valueMask, epicsUInt32 interruptMask)
{
    return this->setUIntDigitalParam(0, index, value, valueMask, interruptMask);
}

/** Sets the value for a UInt32Digital in the parameter library.
  * Calls paramList::setInteger (index, value) for the parameter list indexed by list.
  * \param[in] list The parameter list number.  Must be < maxAddr passed to asynPortDriver::asynPortDriver.
  * \param[in] index The parameter number 
  * \param[in] value Value to set 
  * \param[in] valueMask The mask to use when setting the value. 
  * \param[in] interruptMask A mask that indicates which bits have changed even if the value is the same, so callbacks will be done */
asynStatus asynPortDriver::setUIntDigitalParam(int list, int index, epicsUInt32 value, epicsUInt32 valueMask, epicsUInt32 interruptMask)
{
    asynStatus status;
    static const char *functionName = "setUIntDigitalParam";
    
    status = this->params[list]->setUInt32(index, value, valueMask, interruptMask);
    if (status) reportSetParamErrors(status, index, list, functionName);
    return(status);
}

/** Sets the interrupt mask and reason in the parameter library
  * Calls paramList::setUInt32Interrupt (0, index, mask, reason) i.e. for parameter list 0.
  * \param[in] index The parameter number 
  * \param[in] mask Interrupt mask. 
  * \param[in] reason Interrupt reason. */
asynStatus asynPortDriver::setUInt32DigitalInterrupt(int index, epicsUInt32 mask, interruptReason reason)
{
    return this->setUInt32DigitalInterrupt(0, index, mask, reason);
}

/** Sets the interrupt mask and reason in the parameter library
  * Calls paramList::setUInt32Interrupt (index, mask, reason) for the parameter list indexed by list.
  * \param[in] list The parameter list number.  Must be < maxAddr passed to asynPortDriver::asynPortDriver.
  * \param[in] index The parameter number 
  * \param[in] mask Interrupt mask. 
  * \param[in] reason Interrupt reason. */
asynStatus asynPortDriver::setUInt32DigitalInterrupt(int list, int index, epicsUInt32 mask, interruptReason reason)
{
    asynStatus status;
    static const char *functionName = "setUIntDigitalInterrupt";
    
    status = this->params[list]->setUInt32Interrupt(index, mask, reason);
    if (status) reportSetParamErrors(status, index, list, functionName);
    return(status);
}

/** Clears the interrupt mask in the parameter library
  * Calls paramList::clearUInt32Interrupt (0, index, mask) i.e. for parameter list 0.
  * \param[in] index The parameter number 
  * \param[in] mask Interrupt mask. */
asynStatus asynPortDriver::clearUInt32DigitalInterrupt(int index, epicsUInt32 mask)
{
    return this->clearUInt32DigitalInterrupt(0, index, mask);
}

/** Clears the interrupt mask in the parameter library
  * Calls paramList::clearUInt32Interrupt (index, mask) for the parameter list indexed by list.
  * \param[in] list The parameter list number.  Must be < maxAddr passed to asynPortDriver::asynPortDriver.
  * \param[in] index The parameter number 
  * \param[in] mask Interrupt mask. */
asynStatus asynPortDriver::clearUInt32DigitalInterrupt(int list, int index, epicsUInt32 mask)
{
    asynStatus status;
    static const char *functionName = "clearUIntDigitalInterrupt";
    
    status = this->params[list]->clearUInt32Interrupt(index, mask);
    if (status) reportSetParamErrors(status, index, list, functionName);
    return(status);
}

/** Gets the interrupt mask and reason in the parameter library
  * Calls paramList::getUInt32Interrupt (0, index, mask, reason) i.e. for parameter list 0.
  * \param[in] index The parameter number 
  * \param[in] mask Interrupt mask. 
  * \param[in] reason Interrupt reason. */
asynStatus asynPortDriver::getUInt32DigitalInterrupt(int index, epicsUInt32 *mask, interruptReason reason)
{
    return this->getUInt32DigitalInterrupt(0, index, mask, reason);
}

/** Gets the interrupt mask and reason in the parameter library
  * Calls paramList::getUInt32Interrupt (index, mask, reason) for the parameter list indexed by list.
  * \param[in] list The parameter list number.  Must be < maxAddr passed to asynPortDriver::asynPortDriver.
  * \param[in] index The parameter number 
  * \param[in] mask Interrupt mask. 
  * \param[in] reason Interrupt reason. */
asynStatus asynPortDriver::getUInt32DigitalInterrupt(int list, int index, epicsUInt32 *mask, interruptReason reason)
{
    asynStatus status;
    static const char *functionName = "getUIntDigitalInterrupt";
    
    status = this->params[list]->getUInt32Interrupt(index, mask, reason);
    if (status) reportSetParamErrors(status, index, list, functionName);
    return(status);
}

/** Sets the value for a double in the parameter library.
  * Calls setDoubleParam(0, index, value) i.e. for parameter list 0.
  * \param[in] index The parameter number 
  * \param[in] value Value to set. */
asynStatus asynPortDriver::setDoubleParam(int index, double value)
{
    return this->setDoubleParam(0, index, value);
}

/** Sets the value for a double in the parameter library.
  * Calls paramList::setDouble (index, value) for the parameter list indexed by list.
  * \param[in] list The parameter list number.  Must be < maxAddr passed to asynPortDriver::asynPortDriver.
  * \param[in] index The parameter number 
  * \param[in] value Value to set. */
asynStatus asynPortDriver::setDoubleParam(int list, int index, double value)
{
    asynStatus status;
    static const char *functionName = "setDoubleParam";
    
    status = this->params[list]->setDouble(index, value);
    if (status) reportSetParamErrors(status, index, list, functionName);
    return(status);
}

/** Sets the value for a string in the parameter library.
  * Calls setStringParam(0, index, value) i.e. for parameter list 0.
  * \param[in] index The parameter number 
  * \param[in] value Address of value to set. */
asynStatus asynPortDriver::setStringParam(int index, const char *value)
{
    return this->setStringParam(0, index, value);
}

/** Sets the value for a string in the parameter library.
  * Calls paramList::setString (index, value) for the parameter list indexed by list.
  * \param[in] list The parameter list number.  Must be < maxAddr passed to asynPortDriver::asynPortDriver.
  * \param[in] index The parameter number 
  * \param[in] value Address of value to set. */
asynStatus asynPortDriver::setStringParam(int list, int index, const char *value)
{
    asynStatus status;
    static const char *functionName = "setStringParam";

    status = this->params[list]->setString(index, value);
    if (status) reportSetParamErrors(status, index, list, functionName);
    return(status);
}

/** Reports errors when getting parameters.  
  * asynParamBadIndex and asynParamWrongType are printed with ASYN_TRACE_ERROR because they should never happen.
  * asynParamUndefined is printed with ASYN_TRACE_FLOW because it is an expected error if the value is read before it
  * is defined, which device support can do.
  * \param[in] status The error status.
  * \param[in] index The parameter number
  * \param[in] list The parameter list number.  Must be < maxAddr passed to asynPortDriver::asynPortDriver.
  * \param[in] functionName The name of the function that generated the error  */
void asynPortDriver::reportGetParamErrors(asynStatus status, int index, int list, const char *functionName)
{
    if (status == asynParamBadIndex) {
        asynPrint(this->pasynUserSelf, ASYN_TRACE_ERROR,
            "%s:%s: port=%s error getting parameter %d in list %d, bad index\n",
            driverName, functionName, portName, index, list);
    }
    if (status == asynParamWrongType) {
        asynPrint(this->pasynUserSelf, ASYN_TRACE_ERROR,
            "%s:%s: port=%s error getting parameter %d in list %d, wrong type\n",
            driverName, functionName, portName, index, list);
    }
    if (status == asynParamUndefined) {
        asynPrint(this->pasynUserSelf, ASYN_TRACE_FLOW,
            "%s:%s: port=%s error getting parameter %d in list %d, value undefined\n",
            driverName, functionName, portName, index, list);
    }
}

/** Returns the value for an integer from the parameter library.
  * Calls getIntegerParam(0, index, value) i.e. for parameter list 0.
  * \param[in] index The parameter number 
  * \param[out] value Address of value to get. */
asynStatus asynPortDriver::getIntegerParam(int index, int *value)
{
    return this->getIntegerParam(0, index, value);
}

/** Returns the value for an integer from the parameter library.
  * Calls paramList::getInteger (index, value) for the parameter list indexed by list.
  * \param[in] list The parameter list number.  Must be < maxAddr passed to asynPortDriver::asynPortDriver.
  * \param[in] index The parameter number 
  * \param[out] value Address of value to get. */
asynStatus asynPortDriver::getIntegerParam(int list, int index, int *value)
{
    asynStatus status;
    static const char *functionName = "getIntegerParam";

    status = this->params[list]->getInteger(index, value);
    if (status) reportGetParamErrors(status, index, list, functionName);
    return(status);
}

/** Returns the value for an UInt32Digital parameter from the parameter library.
  * Calls getUIntDigitalParam(0, index, value, mask) i.e. for parameter list 0.
  * \param[in] index The parameter number 
  * \param[out] value Address of value to get.
  * \param[in] mask The mask to apply when getting the value */
asynStatus asynPortDriver::getUIntDigitalParam(int index, epicsUInt32 *value, epicsUInt32 mask)
{
    return this->getUIntDigitalParam(0, index, value, mask);
}

/** Returns the value for an UInt32Digital parameter from the parameter library.
  * Calls paramList::getUInt32 (index, value, mask) for the parameter list indexed by list.
  * \param[in] list The parameter list number.  Must be < maxAddr passed to asynPortDriver::asynPortDriver.
  * \param[in] index The parameter number 
  * \param[out] value Address of value to get.
  * \param[in] mask The mask to apply when getting the value. */
asynStatus asynPortDriver::getUIntDigitalParam(int list, int index, epicsUInt32 *value, epicsUInt32 mask)
{
    asynStatus status;
    static const char *functionName = "getUIntDigitalParam";

    status = this->params[list]->getUInt32(index, value, mask);
    if (status) reportGetParamErrors(status, index, list, functionName);
    return(status);
}

/** Returns the value for a double from the parameter library.
  * Calls getDoubleParam(0, index, value) i.e. for parameter list 0.
  * \param[in] index The parameter number 
  * \param[out] value Address of value to get. */
asynStatus asynPortDriver::getDoubleParam(int index, double *value)
{
    return this->getDoubleParam(0, index, value);
}

/** Returns the value for a double from the parameter library.
  * Calls paramList::getDouble (index, value) for the parameter list indexed by list.
  * \param[in] list The parameter list number.  Must be < maxAddr passed to asynPortDriver::asynPortDriver.
  * \param[in] index The parameter number 
  * \param[out] value Address of value to get. */
asynStatus asynPortDriver::getDoubleParam(int list, int index, double *value)
{
    asynStatus status;
    static const char *functionName = "getDoubleParam";

    status = this->params[list]->getDouble(index, value);
    if (status) reportGetParamErrors(status, index, list, functionName);
    return(status);
}

/** Returns the value for a string from the parameter library.
  * Calls getStringParam(0, index, maxChars, value) i.e. for parameter list 0.
  * \param[in] index The parameter number 
  * \param[in] maxChars Maximum number of characters to return.
  * \param[out] value Address of value to get. */
asynStatus asynPortDriver::getStringParam(int index, int maxChars, char *value)
{
    return this->getStringParam(0, index, maxChars, value);
}

/** Returns the value for a string from the parameter library.
  * Calls paramList::getString (index, maxChars, value) for the parameter list indexed by list.
  * \param[in] list The parameter list number.  Must be < maxAddr passed to asynPortDriver::asynPortDriver.
  * \param[in] index The parameter number 
  * \param[in] maxChars Maximum number of characters to return.
  * \param[out] value Address of value to get. */
asynStatus asynPortDriver::getStringParam(int list, int index, int maxChars, char *value)
{
    asynStatus status;
    static const char *functionName = "getStringParam";

    status = this->params[list]->getString(index, maxChars, value);
    if (status) reportGetParamErrors(status, index, list, functionName);
    return(status);
}

/** Calls callParamCallbacks(0, 0) i.e. with both list and asyn address. */
asynStatus asynPortDriver::callParamCallbacks()
{
    return this->callParamCallbacks(0, 0);
}

/** Calls callParamCallbacks(addr, addr) i.e. with list=addr, which is normal. */
asynStatus asynPortDriver::callParamCallbacks(int addr)
{
    return this->callParamCallbacks(addr, addr);
}

/** Calls paramList::callCallbacks(addr) for a specific parameter list.
  * \param[in] list The parameter list number.  Must be < maxAddr passed to asynPortDriver::asynPortDriver.
  * \param[in] addr The asyn address to be used in the callback.  Typically the same value as list. */
asynStatus asynPortDriver::callParamCallbacks(int list, int addr)
{
    return this->params[list]->callCallbacks(addr);
}

/** Calls paramList::report(fp, details) for each parameter list that the driver supports. 
  * \param[in] fp The file pointer on which report information will be written
  * \param[in] details The level of report detail desired. */
void asynPortDriver::reportParams(FILE *fp, int details)
{
    int i;
    for (i=0; i<this->maxAddr; i++) {
        fprintf(fp, "Parameter list %d\n", i);
        this->params[i]->report(fp, details);
    }
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
asynStatus asynPortDriver::doCallbacksArray(epicsType *value, size_t nElements,
                                            int reason, int address, void *interruptPvt)
{
    ELLLIST *pclientList;
    interruptNode *pnode;
    asynStatus status;
    int addr;

    pasynManager->interruptStart(interruptPvt, &pclientList);
    pnode = (interruptNode *)ellFirst(pclientList);
    getParamStatus(address, reason, &status);
    while (pnode) {
        interruptType *pInterrupt = (interruptType *)pnode->drvPvt;
        pasynManager->getAddr(pInterrupt->pasynUser, &addr);
        /* Set the status for the callback */
        pInterrupt->pasynUser->auxStatus = status;
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
            if (strcmp(interruptTypeString, "uint32") == 0) {
                asynUInt32DigitalInterrupt *pInt = (asynUInt32DigitalInterrupt *) pInterrupt;
                fprintf(fp, "    %s callback client address=%p, addr=%d, reason=%d, mask=0x%x, userPvt=%p\n",
                        interruptTypeString, pInt->callback, pInt->addr,
                        pInt->pasynUser->reason, pInt->mask, pInt->userPvt);
            } else {
                fprintf(fp, "    %s callback client address=%p, addr=%d, reason=%d, userPvt=%p\n",
                        interruptTypeString, pInterrupt->callback, pInterrupt->addr,
                        pInterrupt->pasynUser->reason, pInterrupt->userPvt);
            }
            pnode = (interruptNode *)ellNext(&pnode->node);
        }
        pasynManager->interruptEnd(interruptPvt);
    }
}

/** Returns the asyn address associated with a pasynUser structure.
  * Derived classes rarely need to reimplement this function.
  * \param[in] pasynUser pasynUser structure that encodes the reason and address.
  * \param[out] address Returned address. 
  * \return Returns asynError if the address is > maxAddr value passed to asynPortDriver::asynPortDriver. */
asynStatus asynPortDriver::getAddress(asynUser *pasynUser, int *address) 
{
    static const char *functionName = "getAddress";
    
    pasynManager->getAddr(pasynUser, address);
    /* If this is not a multi-device then address is -1, change to 0 */
    if (*address == -1) *address = 0;
    if (*address > this->maxAddr-1) {
        asynPrint(pasynUser, ASYN_TRACE_ERROR,
            "%s:%s: %s invalid address=%d, max=%d\n",
            driverName, functionName, portName, *address, this->maxAddr-1);
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
    
    pPvt->lock();
    status = pPvt->readInt32(pasynUser, value);
    pPvt->unlock();
    return(status);
}}

/** Called when asyn clients call pasynInt32->read().
  * The base class implementation simply returns the value from the parameter library.  
  * Derived classes rarely need to reimplement this function.
  * \param[in] pasynUser pasynUser structure that encodes the reason and address.
  * \param[out] value Address of the value to read. */
asynStatus asynPortDriver::readInt32(asynUser *pasynUser, epicsInt32 *value)
{
    int function = pasynUser->reason;
    int addr=0;
    asynStatus status = asynSuccess;
    const char *functionName = "readInt32";
    
    status = getAddress(pasynUser, &addr); if (status != asynSuccess) return(status);
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
    
    pPvt->lock();
    status = pPvt->writeInt32(pasynUser, value);
    pPvt->unlock();
    return(status);
}}

/** Called when asyn clients call pasynInt32->write().
  * The base class implementation simply sets the value in the parameter library and 
  * calls any registered callbacks for this pasynUser->reason and address.  
  * Derived classes will reimplement this function if they need to perform an action when an
  * asynInt32 value is written. 
  * \param[in] pasynUser pasynUser structure that encodes the reason and address.
  * \param[in] value Value to write. */
asynStatus asynPortDriver::writeInt32(asynUser *pasynUser, epicsInt32 value)
{
    int function = pasynUser->reason;
    int addr=0;
    asynStatus status = asynSuccess;
    const char* functionName = "writeInt32";

    status = getAddress(pasynUser, &addr); if (status != asynSuccess) return(status);

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
    
    pPvt->lock();
    status = pPvt->getBounds(pasynUser, low, high);
    pPvt->unlock();
    return(status);
}}

/** Called when asyn clients call pasynInt32->getBounds(), returning the bounds on the asynInt32 interface
  * for drivers that use raw units.
  * Device support uses these values for unit conversion.
  * The base class implementation simply returns low=0, high=65535.  
  * Derived classes can reimplement this function if they support raw units with different limits.
  * \param[in] pasynUser pasynUser structure that encodes the reason and address.
  * \param[out] low Address of the low limit.
  * \param[out] high Address of the high limit. */
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

/* asynUInt32Digital interface methods */
extern "C" {static asynStatus readUInt32Digital(void *drvPvt, asynUser *pasynUser, 
                            epicsUInt32 *value, epicsUInt32 mask)
{
    asynPortDriver *pPvt = (asynPortDriver *)drvPvt;
    asynStatus status;
    
    pPvt->lock();
    status = pPvt->readUInt32Digital(pasynUser, value, mask);
    pPvt->unlock();
    return(status);
}}

/** Called when asyn clients call pasynUInt32Digital->read().
  * The base class implementation simply returns the value from the parameter library.  
  * Derived classes rarely need to reimplement this function.
  * \param[in] pasynUser pasynUser structure that encodes the reason and address.
  * \param[out] value Address of the value to read.
  * \param[in] mask Mask value to use when reading the value. */
asynStatus asynPortDriver::readUInt32Digital(asynUser *pasynUser, epicsUInt32 *value, epicsUInt32 mask)
{
    int function = pasynUser->reason;
    int addr=0;
    asynStatus status = asynSuccess;
    const char *functionName = "readUInt32Digital";
    
    status = getAddress(pasynUser, &addr); if (status != asynSuccess) return(status);
    /* We just read the current value of the parameter from the parameter library.
     * Those values are updated whenever anything could cause them to change */
    status = (asynStatus) getUIntDigitalParam(addr, function, value, mask);
    if (status) 
        epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize, 
                  "%s:%s: status=%d, function=%d, value=%u mask=%u", 
                  driverName, functionName, status, function, *value, mask);
    else        
        asynPrint(pasynUser, ASYN_TRACEIO_DRIVER, 
              "%s:%s: function=%d, value=%u, mask=%u\n", 
              driverName, functionName, function, *value, mask);
    return(status);
}

extern "C" {static asynStatus writeUInt32Digital(void *drvPvt, asynUser *pasynUser, 
                            epicsUInt32 value, epicsUInt32 mask)
{
    asynPortDriver *pPvt = (asynPortDriver *)drvPvt;
    asynStatus status;
    
    pPvt->lock();
    status = pPvt->writeUInt32Digital(pasynUser, value, mask);
    pPvt->unlock();
    return(status);
}}

/** Called when asyn clients call pasynUInt32Digital->write().
  * The base class implementation simply sets the value in the parameter library and 
  * calls any registered callbacks for this pasynUser->reason and address.  
  * Derived classes will reimplement this function if they need to perform an action when an
  * asynInt32 value is written. 
  * \param[in] pasynUser pasynUser structure that encodes the reason and address.
  * \param[in] value Value to write.
  * \param[in] mask Mask value to use when writinging the value. */
asynStatus asynPortDriver::writeUInt32Digital(asynUser *pasynUser, epicsUInt32 value, epicsUInt32 mask)
{
    int function = pasynUser->reason;
    int addr=0;
    asynStatus status = asynSuccess;
    const char* functionName = "writeUInt32Digital";

    status = getAddress(pasynUser, &addr); if (status != asynSuccess) return(status);

    /* Set the parameter in the parameter library. */
    status = (asynStatus) setUIntDigitalParam(addr, function, value, mask);

    /* Do callbacks so higher layers see any changes */
    status = (asynStatus) callParamCallbacks(addr, addr);
    
    if (status) 
        epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize, 
                  "%s:%s: status=%d, function=%d, value=%u, mask=%u", 
                  driverName, functionName, status, function, value, mask);
    else        
        asynPrint(pasynUser, ASYN_TRACEIO_DRIVER, 
              "%s:%s: function=%d, value=%d, mask=%u\n", 
              driverName, functionName, function, value, mask);
    return status;
}

extern "C" {static asynStatus setInterruptUInt32Digital(void *drvPvt, asynUser *pasynUser, epicsUInt32 mask, interruptReason reason)
{
    asynPortDriver *pPvt = (asynPortDriver *)drvPvt;
    asynStatus status;
    
    pPvt->lock();
    status = pPvt->setInterruptUInt32Digital(pasynUser, mask, reason);
    pPvt->unlock();
    return(status);
}}

/** Called when asyn clients call pasynUInt32Digital->setInterrupt().
  * The base class implementation simply sets the value in the parameter library.  
  * Derived classes will reimplement this function if they need to perform an action when an
  * setInterrupt is called. 
  * \param[in] pasynUser pasynUser structure that encodes the reason and address.
  * \param[in] mask Interrupt mask. 
  * \param[in] reason Interrupt reason. */
asynStatus asynPortDriver::setInterruptUInt32Digital(asynUser *pasynUser, epicsUInt32 mask, interruptReason reason)
{
    int function = pasynUser->reason;
    int addr=0;
    asynStatus status = asynSuccess;
    const char* functionName = "setInterruptUInt32Digital";

    status = getAddress(pasynUser, &addr); if (status != asynSuccess) return(status);

    /* Set the parameters in the parameter library. */
    status = this->params[addr]->setUInt32Interrupt(function, mask, reason);

    if (status) 
        epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize, 
                  "%s:%s: status=%d, function=%d, mask=%u, reason=%d", 
                  driverName, functionName, status, function, mask, reason);
    else        
        asynPrint(pasynUser, ASYN_TRACEIO_DRIVER, 
              "%s:%s: function=%d, mask=%u, reason=%d\n", 
              driverName, functionName, function, mask, reason);
    return status;
}

extern "C" {static asynStatus clearInterruptUInt32Digital(void *drvPvt, asynUser *pasynUser, epicsUInt32 mask)
{
    asynPortDriver *pPvt = (asynPortDriver *)drvPvt;
    asynStatus status;
    
    pPvt->lock();
    status = pPvt->clearInterruptUInt32Digital(pasynUser, mask);
    pPvt->unlock();
    return(status);
}}

/** Called when asyn clients call pasynUInt32Digital->clearInterrupt().
  * The base class implementation simply sets the value in the parameter library.  
  * Derived classes will reimplement this function if they need to perform an action when an
  * clearInterrupt is called. 
  * \param[in] pasynUser pasynUser structure that encodes the reason and address.
  * \param[in] mask Interrupt mask. */
asynStatus asynPortDriver::clearInterruptUInt32Digital(asynUser *pasynUser, epicsUInt32 mask)
{
    int function = pasynUser->reason;
    int addr=0;
    asynStatus status = asynSuccess;
    const char* functionName = "clearInterruptUInt32Digital";

    status = getAddress(pasynUser, &addr); if (status != asynSuccess) return(status);

    /* Set the parameters in the parameter library. */
    status = this->params[addr]->clearUInt32Interrupt(function, mask);

    if (status) 
        epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize, 
                  "%s:%s: status=%d, function=%d, mask=%u", 
                  driverName, functionName, status, function, mask);
    else        
        asynPrint(pasynUser, ASYN_TRACEIO_DRIVER, 
              "%s:%s: function=%d, mask=%u\n", 
              driverName, functionName, function, mask);
    return status;
}

extern "C" {static asynStatus getInterruptUInt32Digital(void *drvPvt, asynUser *pasynUser, epicsUInt32 *mask, interruptReason reason)
{
    asynPortDriver *pPvt = (asynPortDriver *)drvPvt;
    asynStatus status;
    
    pPvt->lock();
    status = pPvt->getInterruptUInt32Digital(pasynUser, mask, reason);
    pPvt->unlock();
    return(status);
}}

/** Called when asyn clients call pasynUInt32Digital->getInterrupt().
  * The base class implementation simply returns the value from the parameter library.  
  * Derived classes rarely need to reimplement this function.
  * \param[in] pasynUser pasynUser structure that encodes the reason and address.
  * \param[out] mask Interrupt mask address.
  * \param[in] reason Interrupt reason. */
asynStatus asynPortDriver::getInterruptUInt32Digital(asynUser *pasynUser, epicsUInt32 *mask, interruptReason reason)
{
    int function = pasynUser->reason;
    int addr=0;
    asynStatus status = asynSuccess;
    const char* functionName = "getInterruptUInt32Digital";

    status = getAddress(pasynUser, &addr); if (status != asynSuccess) return(status);

    /* Get the parameters in the parameter library. */
    status = this->params[addr]->getUInt32Interrupt(function, mask, reason);

    if (status) 
        epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize, 
                  "%s:%s: status=%d, function=%d, mask=%u, reason=%d", 
                  driverName, functionName, status, function, *mask, reason);
    else        
        asynPrint(pasynUser, ASYN_TRACEIO_DRIVER, 
              "%s:%s: function=%d, mask=%u, reason=%d\n", 
              driverName, functionName, function, *mask, reason);
    return status;
}


/* asynFloat64 interface methods */
extern "C" {static asynStatus readFloat64(void *drvPvt, asynUser *pasynUser,
                              epicsFloat64 *value)
{
    asynPortDriver *pPvt = (asynPortDriver *)drvPvt;
    asynStatus status;
    
    pPvt->lock();
    status = pPvt->readFloat64(pasynUser, value);
    pPvt->unlock();
    return(status);
}}

/** Called when asyn clients call pasynFloat64->read().
  * The base class implementation simply returns the value from the parameter library.  
  * Derived classes rarely need to reimplement this function.
  * \param[in] pasynUser pasynUser structure that encodes the reason and address.
  * \param[in] value Address of the value to read. */
asynStatus asynPortDriver::readFloat64(asynUser *pasynUser, epicsFloat64 *value)
{
    int function = pasynUser->reason;
    int addr=0;
    asynStatus status = asynSuccess;
    const char *functionName = "readFloat64";
    
    status = getAddress(pasynUser, &addr); if (status != asynSuccess) return(status);
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
    
    pPvt->lock();
    status = pPvt->writeFloat64(pasynUser, value);
    pPvt->unlock();
    return(status);
}}

/** Called when asyn clients call pasynFloat64->write().
  * The base class implementation simply sets the value in the parameter library and 
  * calls any registered callbacks for this pasynUser->reason and address.  
  * Derived classes will reimplement this function if they need to perform an action when an
  * asynFloat64 value is written. 
  * \param[in] pasynUser pasynUser structure that encodes the reason and address.
  * \param[in] value Value to write. */
asynStatus asynPortDriver::writeFloat64(asynUser *pasynUser, epicsFloat64 value)
{
    int function = pasynUser->reason;
    asynStatus status = asynSuccess;
    int addr=0;
    const char *functionName = "writeFloat64";

    status = getAddress(pasynUser, &addr); if (status != asynSuccess) return(status);
 
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
    
    pPvt->lock();
    status = pPvt->readOctet(pasynUser, value, maxChars, nActual, eomReason);
    pPvt->unlock();
    return(status);
}}

/** Called when asyn clients call pasynOctet->read().
  * The base class implementation simply returns the value from the parameter library.  
  * Derived classes rarely need to reimplement this function.
  * \param[in] pasynUser pasynUser structure that encodes the reason and address.
  * \param[in] value Address of the string to read.
  * \param[in] maxChars Maximum number of characters to read.
  * \param[out] nActual Number of characters actually read. Base class sets this to strlen(value).
  * \param[out] eomReason Reason that read terminated. Base class sets this to ASYN_EOM_END. */
asynStatus asynPortDriver::readOctet(asynUser *pasynUser,
                            char *value, size_t maxChars, size_t *nActual,
                            int *eomReason)
{
    int function = pasynUser->reason;
    int addr=0;
    asynStatus status = asynSuccess;
    const char *functionName = "readOctet";
   
    status = getAddress(pasynUser, &addr); if (status != asynSuccess) return(status);
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
    if (eomReason) *eomReason = ASYN_EOM_END;
    *nActual = strlen(value);
    return(status);
}

extern "C" {static asynStatus writeOctet(void *drvPvt, asynUser *pasynUser,
                              const char *value, size_t maxChars, size_t *nActual)
{
    asynPortDriver *pPvt = (asynPortDriver *)drvPvt;
    asynStatus status;
    
    pPvt->lock();
    status = pPvt->writeOctet(pasynUser, value, maxChars, nActual);
    pPvt->unlock();
    return(status);
}}

/** Called when asyn clients call pasynOctet->write().
  * The base class implementation simply sets the value in the parameter library and 
  * calls any registered callbacks for this pasynUser->reason and address.  
  * Derived classes will reimplement this function if they need to perform an action when an
  * asynOctet value is written.
  * \param[in] pasynUser pasynUser structure that encodes the reason and address.
  * \param[in] value Address of the string to write.
  * \param[in] nChars Number of characters to write.
  * \param[out] nActual Number of characters actually written. */
asynStatus asynPortDriver::writeOctet(asynUser *pasynUser, const char *value, 
                                    size_t nChars, size_t *nActual)
{
    int addr=0;
    int function = pasynUser->reason;
    asynStatus status = asynSuccess;
    const char *functionName = "writeOctet";

    status = getAddress(pasynUser, &addr); if (status != asynSuccess) return(status);

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
    
    pPvt->lock();
    status = pPvt->readInt8Array(pasynUser, value, nElements, nIn);
    pPvt->unlock();
    return(status);
}}

/** Called when asyn clients call pasynInt8Array->read().
  * The base class implementation simply prints an error message.  
  * Derived classes may reimplement this function if required.
  * \param[in] pasynUser pasynUser structure that encodes the reason and address.
  * \param[in] value Pointer to the array to read.
  * \param[in] nElements Number of elements to read.
  * \param[out] nIn Number of elements actually read. */
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
    
    pPvt->lock();
    status = pPvt->writeInt8Array(pasynUser, value, nElements);
    pPvt->unlock();
    return(status);    
}}

/** Called when asyn clients call pasynInt8Array->write().
  * The base class implementation simply prints an error message.  
  * Derived classes may reimplement this function if required.
  * \param[in] pasynUser pasynUser structure that encodes the reason and address.
  * \param[in] value Pointer to the array to write.
  * \param[in] nElements Number of elements to write. */
asynStatus asynPortDriver::writeInt8Array(asynUser *pasynUser, epicsInt8 *value,
                                size_t nElements)
{
    return(writeArray<epicsInt8>(pasynUser, value, nElements));
}

/** Called by driver to do the callbacks to registered clients on the asynInt8Array interface.
  * \param[in] value Address of the array.
  * \param[in] nElements Number of elements in the array.
  * \param[in] reason A client will be called if reason matches pasynUser->reason registered for that client.
  * \param[in] addr A client will be called if addr matches the asyn address registered for that client. */
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
    
    pPvt->lock();
    status = pPvt->readInt16Array(pasynUser, value, nElements, nIn);
    pPvt->unlock();
    return(status);    
}}

/** Called when asyn clients call pasynInt16Array->read().
  * The base class implementation simply prints an error message.  
  * Derived classes may reimplement this function if required.
  * \param[in] pasynUser pasynUser structure that encodes the reason and address.
  * \param[in] value Pointer to the array to read.
  * \param[in] nElements Number of elements to read.
  * \param[out] nIn Number of elements actually read. */
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
    
    pPvt->lock();
    status = pPvt->writeInt16Array(pasynUser, value, nElements);
    pPvt->unlock();
    return(status);    
}}

/** Called when asyn clients call pasynInt16Array->write().
  * The base class implementation simply prints an error message.  
  * Derived classes may reimplement this function if required.
  * \param[in] pasynUser pasynUser structure that encodes the reason and address.
  * \param[in] value Pointer to the array to write.
  * \param[in] nElements Number of elements to write. */
asynStatus asynPortDriver::writeInt16Array(asynUser *pasynUser, epicsInt16 *value,
                                size_t nElements)
{
    return(writeArray<epicsInt16>(pasynUser, value, nElements));
}

/** Called by driver to do the callbacks to registered clients on the asynInt16Array interface.
  * \param[in] value Address of the array.
  * \param[in] nElements Number of elements in the array.
  * \param[in] reason A client will be called if reason matches pasynUser->reason registered for that client.
  * \param[in] addr A client will be called if addr matches the asyn address registered for that client. */
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
    asynStatus status;
     
    pPvt->lock();
    status = pPvt->readInt32Array(pasynUser, value, nElements, nIn);
    pPvt->unlock();
    return(status);
}}

/** Called when asyn clients call pasynInt32Array->read().
  * The base class implementation simply prints an error message.  
  * Derived classes may reimplement this function if required.
  * \param[in] pasynUser pasynUser structure that encodes the reason and address.
  * \param[in] value Pointer to the array to read.
  * \param[in] nElements Number of elements to read.
  * \param[out] nIn Number of elements actually read. */
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
    
    pPvt->lock();
    status = pPvt->writeInt32Array(pasynUser, value, nElements);
    pPvt->unlock();
    return(status);    
}}

/** Called when asyn clients call pasynInt32Array->write().
  * The base class implementation simply prints an error message.  
  * Derived classes may reimplement this function if required.
  * \param[in] pasynUser pasynUser structure that encodes the reason and address.
  * \param[in] value Pointer to the array to write.
  * \param[in] nElements Number of elements to write. */
asynStatus asynPortDriver::writeInt32Array(asynUser *pasynUser, epicsInt32 *value,
                                size_t nElements)
{
    return(writeArray<epicsInt32>(pasynUser, value, nElements));
}

/** Called by driver to do the callbacks to registered clients on the asynInt32Array interface.
  * \param[in] value Address of the array.
  * \param[in] nElements Number of elements in the array.
  * \param[in] reason A client will be called if reason matches pasynUser->reason registered for that client.
  * \param[in] addr A client will be called if addr matches the asyn address registered for that client. */
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
    
    pPvt->lock();
    status = pPvt->readFloat32Array(pasynUser, value, nElements, nIn);
    pPvt->unlock();
    return(status);    
}}

/** Called when asyn clients call pasynFloat32Array->read().
  * The base class implementation simply prints an error message.  
  * Derived classes may reimplement this function if required.
  * \param[in] pasynUser pasynUser structure that encodes the reason and address.
  * \param[in] value Pointer to the array to read.
  * \param[in] nElements Number of elements to read.
  * \param[out] nIn Number of elements actually read. */
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
    
    pPvt->lock();
    status = pPvt->writeFloat32Array(pasynUser, value, nElements);
    pPvt->unlock();
    return(status);    
}}

/** Called when asyn clients call pasynFloat32Array->write().
  * The base class implementation simply prints an error message.  
  * Derived classes may reimplement this function if required.
  * \param[in] pasynUser pasynUser structure that encodes the reason and address.
  * \param[in] value Pointer to the array to write.
  * \param[in] nElements Number of elements to write. */
asynStatus asynPortDriver::writeFloat32Array(asynUser *pasynUser, epicsFloat32 *value,
                                size_t nElements)
{
    return(writeArray<epicsFloat32>(pasynUser, value, nElements));
}

/** Called by driver to do the callbacks to registered clients on the asynFloat32Array interface.
  * \param[in] value Address of the array.
  * \param[in] nElements Number of elements in the array.
  * \param[in] reason A client will be called if reason matches pasynUser->reason registered for that client.
  * \param[in] addr A client will be called if addr matches the asyn address registered for that client. */
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
    
    pPvt->lock();
    status = pPvt->readFloat64Array(pasynUser, value, nElements, nIn);
    pPvt->unlock();
    return(status);    
}}

/** Called when asyn clients call pasynFloat64Array->read().
  * The base class implementation simply prints an error message.  
  * Derived classes may reimplement this function if required.
  * \param[in] pasynUser pasynUser structure that encodes the reason and address.
  * \param[in] value Pointer to the array to read.
  * \param[in] nElements Number of elements to read.
  * \param[out] nIn Number of elements actually read. */
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
    
    pPvt->lock();
    status = pPvt->writeFloat64Array(pasynUser, value, nElements);
    pPvt->unlock();
    return(status);    
}}

/** Called when asyn clients call pasynFloat64Array->write().
  * The base class implementation simply prints an error message.  
  * Derived classes may reimplement this function if required.
  * \param[in] pasynUser pasynUser structure that encodes the reason and address.
  * \param[in] value Pointer to the array to write.
  * \param[in] nElements Number of elements to write. */
asynStatus asynPortDriver::writeFloat64Array(asynUser *pasynUser, epicsFloat64 *value,
                                size_t nElements)
{
    return(writeArray<epicsFloat64>(pasynUser, value, nElements));
}

/** Called by driver to do the callbacks to registered clients on the asynFloat64Array interface.
  * \param[in] value Address of the array.
  * \param[in] nElements Number of elements in the array.
  * \param[in] reason A client will be called if reason matches pasynUser->reason registered for that client.
  * \param[in] addr A client will be called if addr matches the asyn address registered for that client. */
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
    
    pPvt->lock();
    status =pPvt->readGenericPointer(pasynUser, genericPointer);
    pPvt->unlock();
    return(status);    
}}

/** Called when asyn clients call pasynGenericPointer->read().
  * The base class implementation simply prints an error message.  
  * Derived classes may reimplement this function if required.
  * \param[in] pasynUser pasynUser structure that encodes the reason and address.
  * \param[in] genericPointer Pointer to the object to read. */
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
    
    pPvt->lock();
    status = pPvt->writeGenericPointer(pasynUser, genericPointer);
    pPvt->unlock();
    return(status);    
}}

/** Called when asyn clients call pasynGenericPointer->write().
  * The base class implementation simply prints an error message.  
  * Derived classes may reimplement this function if required.
  * \param[in] pasynUser pasynUser structure that encodes the reason and address.
  * \param[in] genericPointer Pointer to the object to write. */
asynStatus asynPortDriver::writeGenericPointer(asynUser *pasynUser, void *genericPointer)
{
    epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize, 
                "%s:writeGenericPointer not implemented", driverName);
    return(asynError);
}


/** Called by driver to do the callbacks to registered clients on the asynGenericPointer interface.
  * \param[in] genericPointer Pointer to the object
  * \param[in] reason A client will be called if reason matches pasynUser->reason registered for that client.
  * \param[in] address A client will be called if address matches the address registered for that client. */
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


/* asynEnums interface methods */
extern "C" {static asynStatus readEnum(void *drvPvt, asynUser *pasynUser, char *strings[], int values[], int severities[], 
                                       size_t nElements, size_t *nIn)
{
    asynPortDriver *pPvt = (asynPortDriver *)drvPvt;
    asynStatus status;
 
    pPvt->lock();
    status = pPvt->readEnum(pasynUser, strings, values, severities, nElements, nIn);
    pPvt->unlock();
    return(status);    
}}

/** Called when asyn clients call pasynEnum->read().
  * The base class implementation simply prints an error message.  
  * Derived classes may reimplement this function if required.
  * \param[in] pasynUser pasynUser structure that encodes the reason and address.
  * \param[in] strings Array of string pointers. 
  * \param[in] values Array of values 
  * \param[in] severities Array of severities 
  * \param[in] nElements Size of value array 
  * \param[out] nIn Number of elements actually returned */
asynStatus asynPortDriver::readEnum(asynUser *pasynUser, char *strings[], int values[], int severities[], size_t nElements, size_t *nIn)
{
    epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize, 
                "%s:readEnum not implemented", driverName);
    return(asynError);
}

extern "C" {static asynStatus writeEnum(void *drvPvt, asynUser *pasynUser, char *strings[], int values[], int severities[], size_t nElements)
{
    asynPortDriver *pPvt = (asynPortDriver *)drvPvt;
    asynStatus status;
    
    pPvt->lock();
    status = pPvt->writeEnum(pasynUser, strings, values, severities, nElements);
    pPvt->unlock();
    return(status);    
}}

/** Called when asyn clients call pasynEnum->write().
  * The base class implementation simply prints an error message.  
  * Derived classes may reimplement this function if required.
  * \param[in] pasynUser pasynUser structure that encodes the reason and address.
  * \param[in] strings Array of string pointers. 
  * \param[in] values Array of values 
  * \param[in] severities Array of severities 
  * \param[in] nElements Size of value array */
asynStatus asynPortDriver::writeEnum(asynUser *pasynUser, char *strings[], int values[], int severities[], size_t nElements)
{
    epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize, 
                "%s:writeEnum not implemented", driverName);
    return(asynError);
}


/** Called by driver to do the callbacks to registered clients on the asynEnum interface.
  * \param[in] strings Array of string pointers. 
  * \param[in] values Array of values 
  * \param[in] severities Array of severities 
  * \param[in] nElements Size of value array 
  * \param[in] reason A client will be called if reason matches pasynUser->reason registered for that client.
  * \param[in] address A client will be called if address matches the address registered for that client. */
asynStatus asynPortDriver::doCallbacksEnum(char *strings[], int values[], int severities[], size_t nElements, int reason, int address)
{
    ELLLIST *pclientList;
    interruptNode *pnode;
    int addr;

    pasynManager->interruptStart(this->asynStdInterfaces.enumInterruptPvt, &pclientList);
    pnode = (interruptNode *)ellFirst(pclientList);
    while (pnode) {
        asynEnumInterrupt *pInterrupt = (asynEnumInterrupt *)pnode->drvPvt;
        pasynManager->getAddr(pInterrupt->pasynUser, &addr);
        /* If this is not a multi-device then address is -1, change to 0 */
        if (addr == -1) addr = 0;
        if ((pInterrupt->pasynUser->reason == reason) &&
            (address == addr)) {
            pInterrupt->callback(pInterrupt->userPvt,
                                 pInterrupt->pasynUser,
                                 strings, values, severities, nElements);
        }
        pnode = (interruptNode *)ellNext(&pnode->node);
    }
    pasynManager->interruptEnd(this->asynStdInterfaces.enumInterruptPvt);
    return(asynSuccess);
}


/* asynDrvUser interface methods */
extern "C" {static asynStatus drvUserCreate(void *drvPvt, asynUser *pasynUser,
                                 const char *drvInfo, 
                                 const char **pptypeName, size_t *psize)
{ 
    asynPortDriver *pPvt = (asynPortDriver *)drvPvt;
    asynStatus status;
    
    pPvt->lock();
    status = pPvt->drvUserCreate(pasynUser, drvInfo, pptypeName, psize);
    pPvt->unlock();
    return(status);    
}}

/** Called by asynManager to pass a pasynUser structure and drvInfo string to the driver; 
  * Assigns pasynUser->reason based on the value of the drvInfo string.
  * This base class implementation looks up the drvInfo string in the parameter list.
  * \param[in] pasynUser pasynUser structure that driver will modify
  * \param[in] drvInfo String containing information about what driver function is being referenced
  * \param[out] pptypeName Location in which driver can write information.
  * \param[out] psize Location where driver can write information about size of pptypeName */
asynStatus asynPortDriver::drvUserCreate(asynUser *pasynUser,
                                       const char *drvInfo, 
                                       const char **pptypeName, size_t *psize)
{
    const char *functionName = "drvUserCreate";
    asynStatus status;
    int index;
    int addr;
    
    status = getAddress(pasynUser, &addr); if (status != asynSuccess) return(status);
    status = this->findParam(addr, drvInfo, &index);
    if (status) {
        asynPrint(pasynUser, ASYN_TRACE_ERROR,
                  "%s:%s: addr=%d, cannot find parameter %s\n", 
                  driverName, functionName, addr, drvInfo);
        return(status);
    }
    pasynUser->reason = index;
    asynPrint(pasynUser, ASYN_TRACE_FLOW,
              "%s:%s: drvInfo=%s, index=%d\n", 
              driverName, functionName, drvInfo, index);
    return(asynSuccess);
}
    
extern "C" {static asynStatus drvUserGetType(void *drvPvt, asynUser *pasynUser,
                                 const char **pptypeName, size_t *psize)
{ 
    asynPortDriver *pPvt = (asynPortDriver *)drvPvt;
    asynStatus status;
    
    pPvt->lock();
    status = pPvt->drvUserGetType(pasynUser, pptypeName, psize);
    pPvt->unlock();
    return(status);    
}}

/** Returns strings associated with driver-specific commands.
  * This base class implementation does nothing.
  * Derived classes could reimplement this function to return the requested information, but most
  * do not reimplement this.
  * \param[in] pasynUser pasynUser structure that driver will modify
  * \param[out] pptypeName Location in which driver can write information.
  * \param[out] psize Location where driver can write information about size of pptypeName */
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
    
    pPvt->lock();
    status = pPvt->drvUserDestroy(pasynUser);
    pPvt->unlock();
    return(status);    
}}

/** Frees any resources allocated by drvUserCreate.
  * This base class implementation does nothing.
  * Derived classes could reimplement this function if they allocate any resources in drvUserCreate,
  * but asynPortDriver classes typically do not need to do so. */
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
    
    pPvt->lock();
    pPvt->report(fp, details);
    pPvt->unlock();
}}


/** Reports on status of the driver
  * \param[in] fp The file pointer on which report information will be written
  * \param[in] details The level of report detail desired
  *
  * If details > 1 then information is printed about the contents of the parameter library.
  * If details > 2 then information is printed about all of the interrupt callbacks registered.
  * Derived classes typically reimplement this function to print driver-specific details and then
  * call this base class function. */
void asynPortDriver::report(FILE *fp, int details)
{
    asynStandardInterfaces *pInterfaces = &this->asynStdInterfaces;

    fprintf(fp, "Port: %s\n", this->portName);
    if (details > 1) {
        this->reportParams(fp, details);
    }
    if (details >= 2) {
        /* Report interrupt clients */
        reportInterrupt<asynInt32Interrupt>         (fp, pInterfaces->int32InterruptPvt,        "int32");
        reportInterrupt<asynUInt32DigitalInterrupt> (fp, pInterfaces->uInt32DigitalInterruptPvt,"uint32");
        reportInterrupt<asynFloat64Interrupt>       (fp, pInterfaces->float64InterruptPvt,      "float64");
        reportInterrupt<asynOctetInterrupt>         (fp, pInterfaces->octetInterruptPvt,        "octet");
        reportInterrupt<asynInt8ArrayInterrupt>     (fp, pInterfaces->int8ArrayInterruptPvt,    "int8Array");
        reportInterrupt<asynInt16ArrayInterrupt>    (fp, pInterfaces->int16ArrayInterruptPvt,   "int16Array");
        reportInterrupt<asynInt32ArrayInterrupt>    (fp, pInterfaces->int32ArrayInterruptPvt,   "int32Array");
        reportInterrupt<asynFloat32ArrayInterrupt>  (fp, pInterfaces->float32ArrayInterruptPvt, "float32Array");
        reportInterrupt<asynFloat64ArrayInterrupt>  (fp, pInterfaces->float64ArrayInterruptPvt, "float64Array");
        reportInterrupt<asynGenericPointerInterrupt>(fp, pInterfaces->genericPointerInterruptPvt, "genericPointer");
        reportInterrupt<asynEnumInterrupt>          (fp, pInterfaces->enumInterruptPvt,         "Enum");
    }
}

extern "C" {static asynStatus connect(void *drvPvt, asynUser *pasynUser)
{
    asynPortDriver *pPvt = (asynPortDriver *)drvPvt;
    asynStatus status;
    
    pPvt->lock();
    status = pPvt->connect(pasynUser);
    pPvt->unlock();
    return(status);    
}}

/** Connects driver to device; 
  * the base class implementation simply calls pasynManager->exceptionConnect.
  * Derived classes can reimplement this function for real connection management.
  * \param[in] pasynUser The pasynUser structure which contains information about the port and address */
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
    
    pPvt->lock();
    status = pPvt->disconnect(pasynUser);
    pPvt->unlock();
    return(status);    
}}

/** Disconnects driver from device; 
  * the base class implementation simply calls pasynManager->exceptionDisconnect.
  * Derived classes can reimplement this function for real connection management.
  * \param[in] pasynUser The pasynUser structure which contains information about the port and address */
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

static asynUInt32Digital ifaceUInt32Digital = {
    writeUInt32Digital,
    readUInt32Digital,
    setInterruptUInt32Digital,
    clearInterruptUInt32Digital,
    getInterruptUInt32Digital
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

static asynEnum ifaceEnum = {
    writeEnum,
    readEnum
};

static asynDrvUser ifaceDrvUser = {
    drvUserCreate,
    drvUserGetType,
    drvUserDestroy
};



/** Constructor for the asynPortDriver class.
  * \param[in] portNameIn The name of the asyn port driver to be created.
  * \param[in] maxAddrIn The maximum  number of asyn addr addresses this driver supports.
               Often it is 1 (which is the minimum), but some drivers, for example a 
               16-channel D/A or A/D would support values &gt; 1. 
               This controls the number of parameter tables that are created.
  * \param[in] paramTableSize The number of parameters that this driver supports.
               This controls the size of the parameter tables.
  * \param[in] interfaceMask Bit mask defining the asyn interfaces that this driver supports.
                The bit mask values are defined in asynPortDriver.h, e.g. asynInt32Mask.
  * \param[in] interruptMask Bit mask definining the asyn interfaces that can generate interrupts (callbacks).
               The bit mask values are defined in asynPortDriver.h, e.g. asynInt8ArrayMask.
  * \param[in] asynFlags Flags when creating the asyn port driver; includes ASYN_CANBLOCK and ASYN_MULTIDEVICE.
  * \param[in] autoConnect The autoConnect flag for the asyn port driver. 
               1 if the driver should autoconnect.
  * \param[in] priority The thread priority for the asyn port driver thread if ASYN_CANBLOCK is set in asynFlags.
               If it is 0 then the default value of epicsThreadPriorityMedium will be assigned by asynManager.
  * \param[in] stackSize The stack size for the asyn port driver thread if ASYN_CANBLOCK is set in asynFlags.
               If it is 0 then the default value of epicsThreadGetStackSize(epicsThreadStackMedium)
               will be assigned by asynManager.
  */
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
    if (maxAddrIn < 1) maxAddrIn = 1;
    this->maxAddr = maxAddrIn;
    interfaceMask |= asynCommonMask;  /* Always need the asynCommon interface */

    /* Create the epicsMutex for locking access to data structures from other threads */
    this->mutexId = epicsMutexCreate();
    if (!this->mutexId) {
        printf("%s::%s ERROR: epicsMutexCreate failure\n", driverName, functionName);
        return;
    }

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
    
    /* The following asynPrint will be governed by the global trace mask since asynUser is not yet connected to port */
    asynPrint(this->pasynUserSelf, ASYN_TRACE_FLOW,
        "%s:%s: creating port %s maxAddr=%d, paramTableSize=%d\n"
        "    interfaceMask=0x%X, interruptMask=0x%X\n"
        "    asynFlags=0x%X, autoConnect=%d, priority=%d, stackSize=%d\n",
        driverName, functionName, this->portName, this->maxAddr, paramTableSize, 
        interfaceMask, interruptMask, 
        asynFlags, autoConnect, priority, stackSize);

     /* Set addresses of asyn interfaces */
    if (interfaceMask & asynCommonMask)         pInterfaces->common.pinterface        = (void *)&ifaceCommon;
    if (interfaceMask & asynDrvUserMask)        pInterfaces->drvUser.pinterface       = (void *)&ifaceDrvUser;
    if (interfaceMask & asynInt32Mask)          pInterfaces->int32.pinterface         = (void *)&ifaceInt32;
    if (interfaceMask & asynUInt32DigitalMask)  pInterfaces->uInt32Digital.pinterface = (void *)&ifaceUInt32Digital;
    if (interfaceMask & asynFloat64Mask)        pInterfaces->float64.pinterface       = (void *)&ifaceFloat64;
    if (interfaceMask & asynOctetMask)          pInterfaces->octet.pinterface         = (void *)&ifaceOctet;
    if (interfaceMask & asynInt8ArrayMask)      pInterfaces->int8Array.pinterface     = (void *)&ifaceInt8Array;
    if (interfaceMask & asynInt16ArrayMask)     pInterfaces->int16Array.pinterface    = (void *)&ifaceInt16Array;
    if (interfaceMask & asynInt32ArrayMask)     pInterfaces->int32Array.pinterface    = (void *)&ifaceInt32Array;
    if (interfaceMask & asynFloat32ArrayMask)   pInterfaces->float32Array.pinterface  = (void *)&ifaceFloat32Array;
    if (interfaceMask & asynFloat64ArrayMask)   pInterfaces->float64Array.pinterface  = (void *)&ifaceFloat64Array;
    if (interfaceMask & asynGenericPointerMask) pInterfaces->genericPointer.pinterface= (void *)&ifaceGenericPointer;
    if (interfaceMask & asynEnumMask)           pInterfaces->Enum.pinterface          = (void *)&ifaceEnum;

    /* Define which interfaces can generate interrupts */
    if (interruptMask & asynInt32Mask)          pInterfaces->int32CanInterrupt          = 1;
    if (interruptMask & asynUInt32DigitalMask)  pInterfaces->uInt32DigitalCanInterrupt  = 1;
    if (interruptMask & asynFloat64Mask)        pInterfaces->float64CanInterrupt        = 1;
    if (interruptMask & asynOctetMask)          pInterfaces->octetCanInterrupt          = 1;
    if (interruptMask & asynInt8ArrayMask)      pInterfaces->int8ArrayCanInterrupt      = 1;
    if (interruptMask & asynInt16ArrayMask)     pInterfaces->int16ArrayCanInterrupt     = 1;
    if (interruptMask & asynInt32ArrayMask)     pInterfaces->int32ArrayCanInterrupt     = 1;
    if (interruptMask & asynFloat32ArrayMask)   pInterfaces->float32ArrayCanInterrupt   = 1;
    if (interruptMask & asynFloat64ArrayMask)   pInterfaces->float64ArrayCanInterrupt   = 1;
    if (interruptMask & asynGenericPointerMask) pInterfaces->genericPointerCanInterrupt = 1;
    if (interruptMask & asynEnumMask)           pInterfaces->enumCanInterrupt           = 1;

    status = pasynStandardInterfacesBase->initialize(portName, pInterfaces,
                                                     this->pasynUserSelf, this);
    if (status != asynSuccess) {
        asynPrint(this->pasynUserSelf, ASYN_TRACE_ERROR,
            "%s:%s ERROR: Can't register interfaces: %s.\n",
            driverName, functionName, this->pasynUserSelf->errorMessage);
        return;
    }

    /* Allocate space for the parameter objects */
    this->params = (paramList **) calloc(maxAddr, sizeof(paramList *));    
    /* Initialize the parameter library */
    for (addr=0; addr<maxAddr; addr++) {
        this->params[addr] = new paramList(paramTableSize, &this->asynStdInterfaces);
    }

    /* Connect to our device for asynTrace */
    status = pasynManager->connectDevice(this->pasynUserSelf, portName, 0);
    if (status != asynSuccess) {
        asynPrint(this->pasynUserSelf, ASYN_TRACE_ERROR,
            "%s:%s:, connectDevice failed\n", driverName, functionName);
        return;
    }

    /* Create a thread that waits for interruptAccept and then does all the callbacks once. */
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

/** Destructor for asynPortDriver class; frees resources allocated when port driver is created. */
asynPortDriver::~asynPortDriver()
{
    int addr;

    epicsMutexDestroy(this->mutexId);
    for (addr=0; addr<this->maxAddr; addr++) {
        delete this->params[addr];
    }
    free(this->params);
}

// This is a utility function that returns a pointer to an asynPortDriver object from its name
void* findAsynPortDriver(const char *portName)
{
    asynUser *pasynUser;
    asynInterface *pasynInterface;
    asynStatus status;
    
    pasynUser = pasynManager->createAsynUser(NULL, NULL);
    status = pasynManager->connectDevice(pasynUser, portName, 0);
    if (status) return NULL;
    pasynInterface = pasynManager->findInterface(pasynUser, asynCommonType, 1);
    if (!pasynInterface) return NULL;
    pasynManager->disconnect(pasynUser);
    pasynManager->freeAsynUser(pasynUser);
    return pasynInterface->drvPvt;
}
    

