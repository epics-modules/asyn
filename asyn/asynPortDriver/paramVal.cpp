/*
 * paramVal.cpp
 *
 *  Created on: Dec 19, 2011
 *      Author: hammonds
 */

#include <string.h>
#include <stdlib.h>

#include "epicsString.h"
#include "paramVal.h"
#include "paramValWrongType.h"

const char* paramVal::typeNames[] = {
    "asynParamTypeUndefined",
    "asynParamInt32",
    "asynParamUInt32Digital",
    "asynParamFloat64",
    "asynParamOctet",
    "asynParamInt8Array",
    "asynParamInt16Array",
    "asynParamInt32Array",
    "asynParamFloat32Array",
    "asynParamFloat64Array",
    "asynParamGenericPointer"
};



paramVal::paramVal(const char *name):
    type(asynParamUndefined), valueDefined(false), valueChanged(false){
    this->name = epicsStrDup(name);
    this->data.sval = 0;
}

paramVal::paramVal(const char *name, asynParamType type):
    type(type), valueDefined(false), valueChanged(false){
    this->name = epicsStrDup(name);
    this->data.sval = 0;
}

/* Returns true if the value is defined (has been set)
 *
 */
bool paramVal::isDefined(){
    return valueDefined;
}

bool paramVal::hasValueChanged(){
    return valueChanged;
}

void paramVal::setValueChanged(){
    valueChanged = true;
}

void paramVal::resetValueChanged(){
    valueChanged = false;
}

/*
 * Set valueDefined to indicate that the value has been set.
 */
void paramVal::setDefined(bool defined){
    valueDefined = defined;
}

char* paramVal::getName(){
    return name;
}

bool paramVal::nameEquals(const char* name){
    return (name &&
            this->name &&
            (epicsStrCaseCmp(name, this->name) == 0));
}

/** Sets the value for an integer.
  * \param[in] value Value to set.
  * \throws paramValWrongType if type is not asynParamInt32
  * \throws paramValNotChanged if the value does not need to change
  */
void paramVal::setInteger(int value)
{
    if (type != asynParamInt32)
        throw paramValWrongType("paramVal::setInteger can only handle asynParamInt32");
    if (!isDefined() || (data.ival != value))
    {
        setDefined(true);
        data.ival = value;
        setValueChanged();
    }
}

/** Sets the value for an integer in the parameter library.
  * \param[in] value Value to set.
  * \param[in] valueMask Mask to use when setting the value.
  * \param[in] interruptMask Mask of bits that have changed even if the value has not changed
  * \return Returns asynParamBadIndex if the index is not valid or asynParamWrongType if the parameter type is not asynParamUInt32Digital. */
void paramVal::setUInt32(epicsUInt32 value, epicsUInt32 valueMask, epicsUInt32 interruptMask)
{
    epicsUInt32 oldValue;

    if (type != asynParamUInt32Digital)
        throw paramValWrongType("paramVal::setUInt32 can only handle asynParamUInt32Digital");
    setDefined(true);
    oldValue = data.uival;
    /* Set any bits that are set in the value and the mask */
    data.uival |= (value & valueMask);
    /* Clear bits that are clear in the value and set in the mask */
    data.uival &= (value | ~valueMask);
    if (data.uival != oldValue) {
      /* Set the bits in the callback mask that have changed */
      uInt32CallbackMask |= (data.uival ^ oldValue);
      setValueChanged();
    }
    if (interruptMask) {
      uInt32CallbackMask |= interruptMask;
      setValueChanged();
    }
}


/** Sets the value for a double in the parameter library.
  * \param[in] value Value to set.
  * \return Returns asynParamBadIndex if the index is not valid or asynParamWrongType if the parameter type is not asynParamFloat64. */
void paramVal::setDouble(double value)
{
    if (type != asynParamFloat64)
        throw paramValWrongType("paramVal::setDouble can only handle asynParamFloat64");
    if (!isDefined() || (data.dval != value))
    {
        setDefined(true);
        data.dval = value;
        setValueChanged();
    }
}

/** Sets the value for a string in the parameter library.
  * \param[out] value Address of value to set.
  * \return Returns asynParamBadIndex if the index is not valid or asynParamWrongType if the parameter type is not asynParamOctet. */
void paramVal::setString(const char *value)
{
    if (type != asynParamOctet)
        throw paramValWrongType("paramVal::setString can only handle asynParamOctet");
    if (!isDefined() || (strcmp(data.sval, value)))
    {
        setDefined(true);
        if (data.sval != NULL)
            free(data.sval);
        data.sval = epicsStrDup(value);
        setValueChanged();
    }
}

void paramVal::report(int id, FILE *fp, int details)
{
    switch (type)
    {
        case asynParamInt32:
            if (isDefined())
                fprintf(fp, "Parameter %d type=asynInt32, name=%s, value=%d\n", id, getName(), data.ival );
            else
                fprintf(fp, "Parameter %d type=asynInt32, name=%s, value is undefined\n", id, getName());
            break;
        case asynParamUInt32Digital:
            if (isDefined())
                fprintf(fp, "Parameter %d type=asynUInt32Digital, name=%s, value=0x%x, risingMask=0x%x, fallingMask=0x%x, callbackMask=0x%x\n",
                    id, getName(), data.uival,
                    uInt32RisingMask, uInt32FallingMask, uInt32CallbackMask );
            else
                fprintf(fp, "Parameter %d type=asynUInt32Digital, name=%s, value is undefined\n", id, getName());
            break;
        case asynParamFloat64:
            if (isDefined())
                fprintf(fp, "Parameter %d type=asynFloat64, name=%s, value=%f\n", id, getName(), data.dval );
            else
                fprintf(fp, "Parameter %d type=asynFloat64, name=%s, value is undefined\n", id, getName());
            break;
        case asynParamOctet:
            if (isDefined())
                fprintf(fp, "Parameter %d type=string, name=%s, value=%s\n", id, getName(), data.sval );
            else
                fprintf(fp, "Parameter %d type=string, name=%s, value is undefined\n", id, getName());
            break;
        case asynParamInt8Array:
            if (isDefined())
                fprintf(fp, "Parameter %d type=asynInt8Array, name=%s, value=%p\n", id, getName(), data.pi8 );
            else
                fprintf(fp, "Parameter %d type=asynInt8Array, name=%s, value is undefined\n", id, getName());
            break;
        case asynParamInt16Array:
            if (isDefined())
                fprintf(fp, "Parameter %d type=asynInt16Array, name=%s, value=%p\n", id, getName(), data.pi16 );
            else
                fprintf(fp, "Parameter %d type=asynInt16Array, name=%s, value is undefined\n", id, getName());
            break;
        case asynParamInt32Array:
            if (isDefined())
                fprintf(fp, "Parameter %d type=asynInt32Array, name=%s, value=%p\n", id, getName(), data.pi32 );
            else
                fprintf(fp, "Parameter %d type=asynInt32Array, name=%s, value is undefined\n", id, getName());
            break;
        case asynParamFloat32Array:
            if (isDefined())
                fprintf(fp, "Parameter %d type=asynFloat32Array, name=%s, value=%p\n", id, getName(), data.pf32 );
            else
                fprintf(fp, "Parameter %d type=asynFloat32Array, name=%s, value is undefined\n", id, getName());
            break;
        case asynParamFloat64Array:
            if (isDefined())
                fprintf(fp, "Parameter %d type=asynFloat64Array, name=%s, value=%p\n", id, getName(), data.pf64 );
            else
                fprintf(fp, "Parameter %d type=asynFloat64Array, name=%s, value is undefined\n", id, getName());
            break;
        default:
            fprintf(fp, "Parameter %d is undefined, name=%s\n", id, getName());
            break;
    }
}

const char* paramVal::getTypeName(){
    return typeNames[type];
}
