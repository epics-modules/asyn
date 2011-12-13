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

#include "paramList.h"
#include "paramErrors.h"
#include "asynParamType.h"

/** Constructor for paramList class.
  * \param[in] nValues Number of parameters in the list.
  * \param[in] pasynInterfaces Pointer to asynStandardInterfaces structure, used for callbacks */
paramList::paramList(int nValues, asynStandardInterfaces *pasynInterfaces)
    : nextParam(0), nVals(nValues), nFlags(0), pasynInterfaces(pasynInterfaces)
{
    vals = (paramVal *) calloc(nVals, sizeof(paramVal));
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
    this->vals[*index].name = epicsStrDup(name);
    this->vals[*index].type = type;
    this->vals[*index].valueDefined = 0;
    return asynSuccess;
}

/** Finds a parameter in the parameter library.
  * \param[in] name The name of this parameter
  * \param[out] index The parameter number
  * \return Returns asynParamNotFound if name is not found in the parameter list. */
asynStatus paramList::findParam(const char *name, int *index)
{
    for (*index=0; *index<this->nVals; (*index)++) {
        if (name && this->vals[*index].name && (epicsStrCaseCmp(name, this->vals[*index].name) == 0)) return asynSuccess;
    }
    return asynParamNotFound;
}

/** Sets the value for an integer in the parameter library.
  * \param[in] index The parameter number
  * \param[in] value Value to set.
  * \return Returns asynParamBadIndex if the index is not valid or asynParamWrongType if the parametertype is not asynParamInt32. */
asynStatus paramList::setInteger(int index, int value)
{
    if (index < 0 || index >= this->nVals) return asynParamBadIndex;
    if (this->vals[index].type != asynParamInt32) return asynParamWrongType;
    if ((!this->vals[index].valueDefined) || (this->vals[index].data.ival != value))
    {
        this->vals[index].valueDefined = 1;
        setFlag(index);
        this->vals[index].data.ival = value;
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
    epicsUInt32 oldValue;

    if (index < 0 || index >= this->nVals) return asynParamBadIndex;
    if (this->vals[index].type != asynParamUInt32Digital) return asynParamWrongType;
    this->vals[index].valueDefined = 1;
    oldValue = this->vals[index].data.uival;
    /* Set any bits that are set in the value and the mask */
    this->vals[index].data.uival |= (value & valueMask);
    /* Clear bits that are clear in the value and set in the mask */
    this->vals[index].data.uival &= (value | ~valueMask);
    if (this->vals[index].data.uival != oldValue) {
      /* Set the bits in the callback mask that have changed */
      this->vals[index].uInt32CallbackMask |= (this->vals[index].data.uival ^ oldValue);
      setFlag(index);
    }
    if (interruptMask) {
      this->vals[index].uInt32CallbackMask |= interruptMask;
      setFlag(index);
    }
    return asynSuccess;
}

/** Sets the value for a double in the parameter library.
  * \param[in] index The parameter number
  * \param[in] value Value to set.
  * \return Returns asynParamBadIndex if the index is not valid or asynParamWrongType if the parameter type is not asynParamFloat64. */
asynStatus paramList::setDouble(int index, double value)
{
    if (index < 0 || index >= this->nVals) return asynParamBadIndex;
    if (this->vals[index].type != asynParamFloat64) return asynParamWrongType;
    if ((!this->vals[index].valueDefined) || (this->vals[index].data.dval != value))
    {
        this->vals[index].valueDefined = 1;
        setFlag(index);
        this->vals[index].data.dval = value;
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
    if (this->vals[index].type != asynParamOctet) return asynParamWrongType;
    if ((!this->vals[index].valueDefined) || (strcmp(this->vals[index].data.sval, value)))
    {
        this->vals[index].valueDefined = 1;
        setFlag(index);
        free(this->vals[index].data.sval);
        this->vals[index].data.sval = epicsStrDup(value);
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
    if (index < 0 || index >= this->nVals) return asynParamBadIndex;
    if (this->vals[index].type != asynParamInt32) return asynParamWrongType;
    if (!this->vals[index].valueDefined) return asynParamUndefined;
    *value = this->vals[index].data.ival;
    return asynSuccess;
}

/** Returns the value for an integer from the parameter library.
  * \param[in] index The parameter number
  * \param[out] value Address of value to get.
  * \param[in] mask The mask to use when getting the value.
  * \return Returns asynParamBadIndex if the index is not valid, asynParamWrongType if the parameter type is not asynParamUInt32Digital,
  * or asynParamUndefined if the value has not been defined. */
asynStatus paramList::getUInt32(int index, epicsUInt32 *value, epicsUInt32 mask)
{
    if (index < 0 || index >= this->nVals) return asynParamBadIndex;
    if (this->vals[index].type != asynParamUInt32Digital) return asynParamWrongType;
    if (!this->vals[index].valueDefined) return asynParamUndefined;
    *value = this->vals[index].data.uival & mask;
    return asynSuccess;
}

/** Returns the value for a double from the parameter library.
  * \param[in] index The parameter number
  * \param[out] value Address of value to get.
  * \return Returns asynParamBadIndex if the index is not valid, asynParamWrongType if the parameter type is not asynParamFloat64,
  * or asynParamUndefined if the value has not been defined. */
asynStatus paramList::getDouble(int index, double *value)
{
    if (index < 0 || index >= this->nVals) return asynParamBadIndex;
    if (this->vals[index].type != asynParamFloat64) return asynParamWrongType;
    if (!this->vals[index].valueDefined) return asynParamUndefined;
    *value = this->vals[index].data.dval;
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
    if (this->vals[index].type != asynParamUInt32Digital) return asynParamWrongType;
    switch (reason) {
      case interruptOnZeroToOne:
        this->vals[index].uInt32RisingMask = mask;
        break;
      case interruptOnOneToZero:
        this->vals[index].uInt32FallingMask = mask;
        break;
      case interruptOnBoth:
        this->vals[index].uInt32RisingMask = mask;
        this->vals[index].uInt32FallingMask = mask;
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
    if (this->vals[index].type != asynParamUInt32Digital) return asynParamWrongType;
    this->vals[index].uInt32RisingMask &= ~mask;
    this->vals[index].uInt32FallingMask &= ~mask;
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
    if (this->vals[index].type != asynParamUInt32Digital) return asynParamWrongType;
    switch (reason) {
      case interruptOnZeroToOne:
        *mask = this->vals[index].uInt32RisingMask;
        break;
      case interruptOnOneToZero:
        *mask = this->vals[index].uInt32FallingMask;
        break;
      case interruptOnBoth:
        *mask = this->vals[index].uInt32RisingMask | this->vals[index].uInt32FallingMask;
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
    if (index < 0 || index >= this->nVals) return asynParamBadIndex;
    if (this->vals[index].type != asynParamOctet) return asynParamWrongType;
    if (!this->vals[index].valueDefined) return asynParamUndefined;
    if (maxChars > 0) {
        strncpy(value, this->vals[index].data.sval, maxChars-1);
        value[maxChars-1] = '\0';
    }
    return asynSuccess;
}

/** Returns the name of a parameter from the parameter library.
  * \param[in] index The parameter number
  * \param[out] value Address of pointer that will contain name string pointer.
  * \return Returns asynParamBadIndex if the index is not valid */
asynStatus paramList::getName(int index, const char **value)
{
    if (index < 0 || index >= this->nVals) return asynParamBadIndex;
    *value = (const char *)this->vals[index].name;
    return asynSuccess;
}

/** Calls the registered asyn callback functions for all clients for an integer parameter */
asynStatus paramList::int32Callback(int command, int addr, epicsInt32 value)
{
    ELLLIST *pclientList;
    interruptNode *pnode;
    asynStandardInterfaces *pInterfaces = this->pasynInterfaces;
    int address;

    /* Pass int32 interrupts */
    if (!pInterfaces->int32InterruptPvt) return(asynParamNotFound);
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

/** Calls the registered asyn callback functions for all clients for an UInt32 parameter */
asynStatus paramList::uint32Callback(int command, int addr, epicsUInt32 value, epicsUInt32 interruptMask)
{
    ELLLIST *pclientList;
    interruptNode *pnode;
    asynStandardInterfaces *pInterfaces = this->pasynInterfaces;
    int address;

    /* Pass UInt32Digital interrupts */
    if (!pInterfaces->uInt32DigitalInterruptPvt) return(asynParamNotFound);
    pasynManager->interruptStart(pInterfaces->uInt32DigitalInterruptPvt, &pclientList);
    pnode = (interruptNode *)ellFirst(pclientList);
    while (pnode) {
        asynUInt32DigitalInterrupt *pInterrupt = (asynUInt32DigitalInterrupt *) pnode->drvPvt;
        pasynManager->getAddr(pInterrupt->pasynUser, &address);
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
asynStatus paramList::float64Callback(int command, int addr, epicsFloat64 value)
{
    ELLLIST *pclientList;
    interruptNode *pnode;
    asynStandardInterfaces *pInterfaces = this->pasynInterfaces;
    int address;

    /* Pass float64 interrupts */
    if (!pInterfaces->float64InterruptPvt) return(asynParamNotFound);
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

/** Calls the registered asyn callback functions for all clients for a string parameter */
asynStatus paramList::octetCallback(int command, int addr, char *value)
{
    ELLLIST *pclientList;
    interruptNode *pnode;
    asynStandardInterfaces *pInterfaces = this->pasynInterfaces;
    int address;

    /* Pass octet interrupts */
    if (!pInterfaces->octetInterruptPvt) return(asynParamNotFound);
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

    for (i = 0; i < this->nFlags; i++)
    {
        index = this->flags[i];
        if (!this->vals[index].valueDefined) return(status);
        switch(this->vals[index].type) {
            case asynParamInt32:
                status = int32Callback(index, addr, this->vals[index].data.ival);
                break;
            case asynParamUInt32Digital:
                status = uint32Callback(index, addr, this->vals[index].data.uival, this->vals[index].uInt32CallbackMask);
                this->vals[index].uInt32CallbackMask = 0;
                break;
            case asynParamFloat64:
                status = float64Callback(index, addr, this->vals[index].data.dval);
                break;
            case asynParamOctet:
                status = octetCallback(index, addr, this->vals[index].data.sval);
                break;
            default:
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

/** Reports on status of the paramList
  * \param[in] fp The file pointer on which report information will be written
  * \param[in] details The level of report detail desired. Prints the number of parameters in the list,
  * and if details >1 also prints the index, data type, name, and value of each parameter.
 */
void paramList::report(FILE *fp, int details)
{
    int i;

    printf( "Number of parameters is: %d\n", this->nVals );
    if (details <= 1) return;
    for (i=0; i<this->nVals; i++)
    {
        switch (this->vals[i].type)
        {
            case asynParamInt32:
                if (this->vals[i].valueDefined)
                    fprintf(fp, "Parameter %d type=asynInt32, name=%s, value=%d\n", i, this->vals[i].name, this->vals[i].data.ival );
                else
                    fprintf(fp, "Parameter %d type=asynInt32, name=%s, value is undefined\n", i, this->vals[i].name);
                break;
            case asynParamUInt32Digital:
                if (this->vals[i].valueDefined)
                    fprintf(fp, "Parameter %d type=asynUInt32Digital, name=%s, value=0x%x, risingMask=0x%x, fallingMask=0x%x, callbackMask=0x%x\n",
                        i, this->vals[i].name, this->vals[i].data.uival,
                        this->vals[i].uInt32RisingMask, this->vals[i].uInt32FallingMask, this->vals[i].uInt32CallbackMask );
                else
                    fprintf(fp, "Parameter %d type=asynUInt32Digital, name=%s, value is undefined\n", i, this->vals[i].name);
                break;
            case asynParamFloat64:
                if (this->vals[i].valueDefined)
                    fprintf(fp, "Parameter %d type=asynFloat64, name=%s, value=%f\n", i, this->vals[i].name, this->vals[i].data.dval );
                else
                    fprintf(fp, "Parameter %d type=asynFloat64, name=%s, value is undefined\n", i, this->vals[i].name);
                break;
            case asynParamOctet:
                if (this->vals[i].valueDefined)
                    fprintf(fp, "Parameter %d type=string, name=%s, value=%s\n", i, this->vals[i].name, this->vals[i].data.sval );
                else
                    fprintf(fp, "Parameter %d type=string, name=%s, value is undefined\n", i, this->vals[i].name);
                break;
            case asynParamInt8Array:
                if (this->vals[i].valueDefined)
                    fprintf(fp, "Parameter %d type=asynInt8Array, name=%s, value=%p\n", i, this->vals[i].name, this->vals[i].data.pi8 );
                else
                    fprintf(fp, "Parameter %d type=asynInt8Array, name=%s, value is undefined\n", i, this->vals[i].name);
                break;
            case asynParamInt16Array:
                if (this->vals[i].valueDefined)
                    fprintf(fp, "Parameter %d type=asynInt16Array, name=%s, value=%p\n", i, this->vals[i].name, this->vals[i].data.pi16 );
                else
                    fprintf(fp, "Parameter %d type=asynInt16Array, name=%s, value is undefined\n", i, this->vals[i].name);
                break;
            case asynParamInt32Array:
                if (this->vals[i].valueDefined)
                    fprintf(fp, "Parameter %d type=asynInt32Array, name=%s, value=%p\n", i, this->vals[i].name, this->vals[i].data.pi32 );
                else
                    fprintf(fp, "Parameter %d type=asynInt32Array, name=%s, value is undefined\n", i, this->vals[i].name);
                break;
            case asynParamFloat32Array:
                if (this->vals[i].valueDefined)
                    fprintf(fp, "Parameter %d type=asynFloat32Array, name=%s, value=%p\n", i, this->vals[i].name, this->vals[i].data.pf32 );
                else
                    fprintf(fp, "Parameter %d type=asynFloat32Array, name=%s, value is undefined\n", i, this->vals[i].name);
                break;
            case asynParamFloat64Array:
                if (this->vals[i].valueDefined)
                    fprintf(fp, "Parameter %d type=asynFloat64Array, name=%s, value=%p\n", i, this->vals[i].name, this->vals[i].data.pf64 );
                else
                    fprintf(fp, "Parameter %d type=asynFloat64Array, name=%s, value is undefined\n", i, this->vals[i].name);
                break;
            default:
                fprintf(fp, "Parameter %d is undefined, name=%s\n", i, this->vals[i].name);
                break;
        }
    }
}


