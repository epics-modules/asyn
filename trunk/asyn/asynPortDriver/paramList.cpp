/*
 * paramList.cpp
 *
 *  Created on: Dec 13, 2011
 *      Author: hammonds
 *      File split from asynPortDriver.cpp
 */
#include <string.h>
#include <epicsString.h>

#include <stdlib.h>
/* NOTE: This is needed for interruptAccept */
#include <dbAccess.h>

#include "paramVal.h"
#include "paramList.h"
#include "paramErrors.h"
#include "asynParamType.h"
#include "ParamListInvalidIndex.h"
#include "ParamValWrongType.h"
#include "ParamValNotDefined.h"

/** Constructor for paramList class.
  * \param[in] nValues Number of parameters in the list.
  * \param[in] pasynInterfaces Pointer to asynStandardInterfaces structure, used for callbacks */
paramList::paramList(int nValues, asynStandardInterfaces *pasynInterfaces)
    : nextParam(0), nVals(nValues), nFlags(0), pasynInterfaces(pasynInterfaces)
{
    char eName[6];
    sprintf(eName, "empty");
    vals = (paramVal **) calloc(nVals, sizeof(paramVal));
    for (int ii=0; ii<nVals; ii++){
        vals[ii] = new paramVal(eName);
    }
    flags = (int *) calloc(nVals, sizeof(int));
}

/** Destructor for paramList class; frees resources allocated in constructor */
paramList::~paramList()
{
    free(vals);
    free(flags);
}

asynStatus paramList::setFlag(int index)
{
    int i;

    if (index < 0 || index >= this->nVals) return asynParamBadIndex;
    /* See if we have already set the flag for this parameter */
    for (i=0; i<this->nFlags; i++) if (this->flags[i] == index) break;
    /* If not found add a flag */
    if (i == this->nFlags) this->flags[this->nFlags++] = index;
    return asynSuccess;
}

/** Adds a new parameter to the parameter library.
  * \param[in] name The name of this parameter
  * \param[in] type The type of this parameter
  * \param[out] index The parameter number
  * \return Returns asynParamAlreadyExists if the parameter already exists, or asynBadParamIndex if
  * adding this parameter would exceed the size of the parameter list. */
asynStatus paramList::createParam(const char *name, asynParamType type, int *index)
{
    if (this->findParam(name, index) == asynSuccess) return asynParamAlreadyExists;
    *index = this->nextParam++;
    if (*index < 0 || *index >= this->nVals) return asynParamBadIndex;
    delete this->vals[*index];
    this->vals[*index] = new paramVal(name, type);
    return asynSuccess;
}

/** Finds a parameter in the parameter library.
  * \param[in] name The name of this parameter
  * \param[out] index The parameter number
  * \return Returns asynParamNotFound if name is not found in the parameter list. */
asynStatus paramList::findParam(const char *name, int *index)
{
    for (*index=0; *index<this->nVals; (*index)++) {
        if (this->vals[*index]->nameEquals(name)) return asynSuccess;
    }
    return asynParamNotFound;
}

void paramList::registerParameterChange(paramVal *param,int index)
{
    if(param->hasValueChanged()){
        setFlag(index);
        param->resetValueChanged();
    }
}

/** Sets the value for an integer in the parameter library.
  * \param[in] index The parameter number
  * \param[in] value Value to set.
  * \return Returns asynParamBadIndex if the index is not valid or asynParamWrongType if the parametertype is not asynParamInt32. */
asynStatus paramList::setInteger(int index, int value)
{
    try{
        getParameter(index)->setInteger(value);
        registerParameterChange(getParameter(index), index);
    }
    catch (ParamValWrongType&) {
        return asynParamWrongType;
    }
    catch (ParamListInvalidIndex&){
        return asynParamBadIndex;
    }

    return asynSuccess;
}

/** Sets the value for an integer in the parameter library.
  * \param[in] index The parameter number
  * \param[in] value Value to set.
  * \param[in] valueMask Mask to use when setting the value.
  * \param[in] interruptMask Mask of bits that have changed even if the value has not changed
  * \return Returns asynParamBadIndex if the index is not valid or asynParamWrongType if the parameter type is not asynParamUInt32Digital. */
asynStatus paramList::setUInt32(int index, epicsUInt32 value, epicsUInt32 valueMask, epicsUInt32 interruptMask)
{
    try {
        getParameter(index)->setUInt32(value, valueMask, interruptMask);
        registerParameterChange(getParameter(index), index);
    }
    catch (ParamValWrongType&) {
        return asynParamWrongType;
    }
    catch (ParamListInvalidIndex&) {
        return asynParamBadIndex;
    }

    return asynSuccess;
}

/** Sets the value for a double in the parameter library.
  * \param[in] index The parameter number
  * \param[in] value Value to set.
  * \return Returns asynParamBadIndex if the index is not valid or asynParamWrongType if the parameter type is not asynParamFloat64. */
asynStatus paramList::setDouble(int index, double value)
{
    try
    {
        getParameter(index)->setDouble(value);
        registerParameterChange(getParameter(index), index);
    }
    catch (ParamValWrongType&)
    {
        return asynParamWrongType;
    }
    catch (ParamListInvalidIndex&) {
        return asynParamBadIndex;
    }
    return asynSuccess;
}

/** Sets the value for a string in the parameter library.
  * \param[in] index The parameter number
  * \param[out] value Address of value to set.
  * \return Returns asynParamBadIndex if the index is not valid or asynParamWrongType if the parameter type is not asynParamOctet. */
asynStatus paramList::setString(int index, const char *value)
{
    if (index < 0 || index >= this->nVals) return asynParamBadIndex;
    try {
        getParameter(index)->setString(value);
        registerParameterChange(getParameter(index), index);
    }
    catch (ParamValWrongType&){
        return asynParamWrongType;
    }
    catch (ParamListInvalidIndex&) {
        return asynParamBadIndex;
    }
    return asynSuccess;
}

/** Returns the value for an integer from the parameter library.
  * \param[in] index The parameter number
  * \param[out] value Address of value to get.
  * \return Returns asynParamBadIndex if the index is not valid, asynParamWrongType if the parameter type is not asynParamInt32,
  * or asynParamUndefined if the value has not been defined. */
asynStatus paramList::getInteger(int index, int *value)
{
    asynStatus status;
    
    try {
        paramVal *pVal = getParameter(index);
        *value = pVal->getInteger();
        status = pVal->getStatus();
    }
    catch (ParamValWrongType&){
        return asynParamWrongType;
    }
    catch (ParamValNotDefined&){
        return asynParamUndefined;
    }
    catch (ParamListInvalidIndex&) {
        return asynParamBadIndex;
    }
    return status;
}

/** Returns the value for an integer from the parameter library.
  * \param[in] index The parameter number
  * \param[out] value Address of value to get.
  * \param[in] mask The mask to use when getting the value.
  * \return Returns asynParamBadIndex if the index is not valid, asynParamWrongType if the parameter type is not asynParamUInt32Digital,
  * or asynParamUndefined if the value has not been defined. */
asynStatus paramList::getUInt32(int index, epicsUInt32 *value, epicsUInt32 mask)
{
    asynStatus status;
    
    try {
        paramVal *pVal = getParameter(index);
        *value = pVal->getUInt32(mask);
        status = pVal->getStatus();
    }
    catch (ParamValWrongType&){
        return asynParamWrongType;
    }
    catch (ParamValNotDefined&){
        return asynParamUndefined;
    }
    catch (ParamListInvalidIndex&) {
        return asynParamBadIndex;
    }
    return status;
}

/** Returns the value for a double from the parameter library.
  * \param[in] index The parameter number
  * \param[out] value Address of value to get.
  * \return Returns asynParamBadIndex if the index is not valid, asynParamWrongType if the parameter type is not asynParamFloat64,
  * or asynParamUndefined if the value has not been defined. */
asynStatus paramList::getDouble(int index, double *value)
{
    asynStatus status;
    
    try {
        paramVal *pVal = getParameter(index);
        *value = pVal->getDouble();
        status = pVal->getStatus();
    }
    catch (ParamValWrongType&){
        return asynParamWrongType;
    }
    catch (ParamValNotDefined&){
        return asynParamUndefined;
    }
    catch (ParamListInvalidIndex&) {
        return asynParamBadIndex;
    }
    return status;
}

/** Returns the status for a parameter in the parameter library.
  * \param[in] index The parameter number
  * \param[out] status Address of status to get
  * \return Returns asynParamBadIndex if the index is not valid */
asynStatus paramList::getStatus(int index, asynStatus *status)
{
    if (index < 0 || index >= this->nVals) return asynParamBadIndex;
    *status = this->vals[index]->getStatus();
    return asynSuccess;
}

/** Sets the status for a parameter in the parameter library.
  * \param[in] index The parameter number
  * \param[in] status Status to set
  * \return Returns asynParamBadIndex if the index is not valid */
asynStatus paramList::setStatus(int index, asynStatus status)
{
    if (index < 0 || index >= this->nVals) return asynParamBadIndex;
    this->vals[index]->setStatus(status);
    registerParameterChange(getParameter(index), index);
    return asynSuccess;
}

/** Sets the value of the UInt32Interrupt in the parameter library.
  * \param[in] index The parameter number
  * \param[in] mask The interrupt mask.
  * \param[in] reason The interrupt reason.
  * \return Returns asynParamBadIndex if the index is not valid,
  * or asynParamWrongType if the parameter type is not asynParamUInt32Digital */
asynStatus paramList::setUInt32Interrupt(int index, epicsUInt32 mask, interruptReason reason)
{
    if (index < 0 || index >= this->nVals) return asynParamBadIndex;
    if (this->vals[index]->type != asynParamUInt32Digital) return asynParamWrongType;
    switch (reason) {
      case interruptOnZeroToOne:
        this->vals[index]->uInt32RisingMask = mask;
        break;
      case interruptOnOneToZero:
        this->vals[index]->uInt32FallingMask = mask;
        break;
      case interruptOnBoth:
        this->vals[index]->uInt32RisingMask = mask;
        this->vals[index]->uInt32FallingMask = mask;
        break;
    }
    return asynSuccess;
}

/** Clears the UInt32Interrupt in the parameter library.
  * \param[in] index The parameter number
  * \param[in] mask The interrupt mask.
  * \return Returns asynParamBadIndex if the index is not valid,
  * or asynParamWrongType if the parameter type is not asynParamUInt32Digital */
asynStatus paramList::clearUInt32Interrupt(int index, epicsUInt32 mask)
{
    if (index < 0 || index >= this->nVals) return asynParamBadIndex;
    if (this->vals[index]->type != asynParamUInt32Digital) return asynParamWrongType;
    this->vals[index]->uInt32RisingMask &= ~mask;
    this->vals[index]->uInt32FallingMask &= ~mask;
    return asynSuccess;
}

/** Returns the UInt32Interrupt from the parameter library.
  * \param[in] index The parameter number
  * \param[out] mask The address of the interrupt mask to return.
  * \param[in] reason The interrupt reason.
  * \return Returns asynParamBadIndex if the index is not valid,
  * or asynParamWrongType if the parameter type is not asynParamUInt32Digital */
asynStatus paramList::getUInt32Interrupt(int index, epicsUInt32 *mask, interruptReason reason)
{
    if (index < 0 || index >= this->nVals) return asynParamBadIndex;
    if (this->vals[index]->type != asynParamUInt32Digital) return asynParamWrongType;
    switch (reason) {
      case interruptOnZeroToOne:
        *mask = this->vals[index]->uInt32RisingMask;
        break;
      case interruptOnOneToZero:
        *mask = this->vals[index]->uInt32FallingMask;
        break;
      case interruptOnBoth:
        *mask = this->vals[index]->uInt32RisingMask | this->vals[index]->uInt32FallingMask;
        break;
    }
    return asynSuccess;
}

/** Returns the value for a string from the parameter library.
  * \param[in] index The parameter number
  * \param[in] maxChars Maximum number of characters to return.
  * \param[out] value Address of value to get.
  * \return Returns asynParamBadIndex if the index is not valid, asynParamWrongType if the parameter type is not asynParamOctet,
  * or asynParamUndefined if the value has not been defined. */
asynStatus paramList::getString(int index, int maxChars, char *value)
{
    asynStatus status=asynSuccess;
    
    try {
        if (maxChars > 0) {
            paramVal *pVal = getParameter(index);
            status = pVal->getStatus();
            strncpy(value, pVal->getString(), maxChars-1);
            value[maxChars-1] = '\0';
        }
    }
    catch (ParamValWrongType&){
        return asynParamWrongType;
    }
    catch (ParamValNotDefined&){
        return asynParamUndefined;
    }
    catch (ParamListInvalidIndex&) {
        return asynParamBadIndex;
    }
    return status;
}

/** Returns the name of a parameter from the parameter library.
  * \param[in] index The parameter number
  * \param[out] value Address of pointer that will contain name string pointer.
  * \return Returns asynParamBadIndex if the index is not valid */
asynStatus paramList::getName(int index, const char **value)
{
    if (index < 0 || index >= this->nVals) return asynParamBadIndex;
    *value = (const char *)this->vals[index]->getName();
    return asynSuccess;
}

/** Calls the registered asyn callback functions for all clients for an integer parameter */
asynStatus paramList::int32Callback(int command, int addr)
{
    ELLLIST *pclientList;
    interruptNode *pnode;
    asynStandardInterfaces *pInterfaces = this->pasynInterfaces;
    int address;
    epicsInt32 value;
    asynStatus status;

    /* Pass int32 interrupts */
    status = getInteger(command, &value);
    if (!pInterfaces->int32InterruptPvt) return(asynParamNotFound);
    pasynManager->interruptStart(pInterfaces->int32InterruptPvt, &pclientList);
    pnode = (interruptNode *)ellFirst(pclientList);
    while (pnode) {
        asynInt32Interrupt *pInterrupt = (asynInt32Interrupt *) pnode->drvPvt;
        pasynManager->getAddr(pInterrupt->pasynUser, &address);
        /* Set the status for the callback */
        pInterrupt->pasynUser->auxStatus = status;
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

/** Calls the registered asyn callback functions for all clients for an UInt32 parameter */
asynStatus paramList::uint32Callback(int command, int addr, epicsUInt32 interruptMask)
{
    ELLLIST *pclientList;
    interruptNode *pnode;
    asynStandardInterfaces *pInterfaces = this->pasynInterfaces;
    int address;
    epicsUInt32 value;
    asynStatus status;

    /* Pass UInt32Digital interrupts */
    status = getUInt32(command, &value, 0xFFFFFFFF);
    if (!pInterfaces->uInt32DigitalInterruptPvt) return(asynParamNotFound);
    pasynManager->interruptStart(pInterfaces->uInt32DigitalInterruptPvt, &pclientList);
    pnode = (interruptNode *)ellFirst(pclientList);
    while (pnode) {
        asynUInt32DigitalInterrupt *pInterrupt = (asynUInt32DigitalInterrupt *) pnode->drvPvt;
        pasynManager->getAddr(pInterrupt->pasynUser, &address);
        /* Set the status for the callback */
        pInterrupt->pasynUser->auxStatus = status;
        /* If this is not a multi-device then address is -1, change to 0 */
        if (address == -1) address = 0;
        if ((command == pInterrupt->pasynUser->reason) &&
            (address == addr) &&
            (pInterrupt->mask & interruptMask)) {
            pInterrupt->callback(pInterrupt->userPvt,
                                 pInterrupt->pasynUser,
                                 pInterrupt->mask & value);
        }
        pnode = (interruptNode *)ellNext(&pnode->node);
    }
    pasynManager->interruptEnd(pInterfaces->uInt32DigitalInterruptPvt);
    return(asynSuccess);
}

/** Calls the registered asyn callback functions for all clients for a double parameter */
asynStatus paramList::float64Callback(int command, int addr)
{
    ELLLIST *pclientList;
    interruptNode *pnode;
    asynStandardInterfaces *pInterfaces = this->pasynInterfaces;
    int address;
    epicsFloat64 value;
    asynStatus status;

    /* Pass float64 interrupts */
    status = getDouble(command, &value);
    if (!pInterfaces->float64InterruptPvt) return(asynParamNotFound);
    pasynManager->interruptStart(pInterfaces->float64InterruptPvt, &pclientList);
    pnode = (interruptNode *)ellFirst(pclientList);
    while (pnode) {
        asynFloat64Interrupt *pInterrupt = (asynFloat64Interrupt *) pnode->drvPvt;
        pasynManager->getAddr(pInterrupt->pasynUser, &address);
        /* Set the status for the callback */
        pInterrupt->pasynUser->auxStatus = status;
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

/** Calls the registered asyn callback functions for all clients for a string parameter */
asynStatus paramList::octetCallback(int command, int addr)
{
    ELLLIST *pclientList;
    interruptNode *pnode;
    asynStandardInterfaces *pInterfaces = this->pasynInterfaces;
    int address;
    char *value;
    asynStatus status;

    /* Pass octet interrupts */
    value = getParameter(command)->getString();
    getStatus(command, &status);
    if (!pInterfaces->octetInterruptPvt) return(asynParamNotFound);
    pasynManager->interruptStart(pInterfaces->octetInterruptPvt, &pclientList);
    pnode = (interruptNode *)ellFirst(pclientList);
    while (pnode) {
        asynOctetInterrupt *pInterrupt = (asynOctetInterrupt *) pnode->drvPvt;
        pasynManager->getAddr(pInterrupt->pasynUser, &address);
        /* Set the status for the callback */
        pInterrupt->pasynUser->auxStatus = status;
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

/** Calls the registered asyn callback functions for all clients for any parameters that have changed
  * since the last time this function was called.
  * \param[in] addr A client will be called if addr matches the asyn address registered for that client.
  *
  * Don't do anything if interruptAccept=0.
  * There is a thread that will do all callbacks once when interruptAccept goes to 1.
  */
asynStatus paramList::callCallbacks(int addr)
{
    int i, index;
    asynStatus status = asynSuccess;

    if (!interruptAccept) return(asynSuccess);

    try {
        for (i = 0; i < this->nFlags; i++)
        {
            index = this->flags[i];
            if (!getParameter(index)->isDefined()) return status;
            switch(getParameter(index)->type) {
                case asynParamInt32:
                    status = int32Callback(index, addr);
                    break;
                case asynParamUInt32Digital:
                    status = uint32Callback(index, addr, this->vals[index]->uInt32CallbackMask);
                    this->vals[index]->uInt32CallbackMask = 0;
                    break;
                case asynParamFloat64:
                    status = float64Callback(index, addr);
                    break;
                case asynParamOctet:
                    status = octetCallback(index, addr);
                    break;
                default:
                    break;
            }
        }
    }
    catch (ParamListInvalidIndex&) {
        return asynParamBadIndex;
    }
    this->nFlags=0;
    return(status);
}

asynStatus paramList::callCallbacks()
{
    return(callCallbacks(0));
}

/** Reports on status of the paramList
  * \param[in] fp The file pointer on which report information will be written
  * \param[in] details The level of report detail desired. Prints the number of parameters in the list,
  * and if details >1 also prints the index, data type, name, and value of each parameter.
 */
void paramList::report(FILE *fp, int details)
{
    int i;

    fprintf(fp, "Number of parameters is: %d\n", this->nVals );
    if (details <= 1) return;
    for (i=0; i<this->nVals; i++)
    {
        this->vals[i]->report(i, fp, details);
    }
}

/** Get a parameter from the list by index
 *  \param[in] index The index of the desired parameter in the list
 *  \return The parameter associated with the input index
 *  \throws ParamListInvalidIndex if the index is outside the list
 *  boundaries
 */
paramVal* paramList::getParameter(int index)
{
    if (index < 0 || index >= this->nVals) throw ParamListInvalidIndex("paramList::getParameter invalid index");
    return this->vals[index];
}

