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
#include "ParamValWrongType.h"
#include "ParamValNotDefined.h"

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
    type(asynParamNotDefined), status_(asynSuccess), alarmStatus_(0), alarmSeverity_(0),
    valueDefined(false), valueChanged(false)
{
    this->name = epicsStrDup(name);
}

paramVal::paramVal(const char *name, asynParamType type):
    type(type), status_(asynSuccess), alarmStatus_(0), alarmSeverity_(0),
    valueDefined(false), valueChanged(false){
    this->name = epicsStrDup(name);
}

paramVal::~paramVal(){
    free(name);
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

void paramVal::setStatus(asynStatus status){
    if (status_ != status) {
        setValueChanged();
        status_ = status;
        // We need to do callbacks on all bits if the status has changed
        if (type == asynParamUInt32Digital) uInt32CallbackMask = 0xFFFFFFFF;
    }
}

asynStatus paramVal::getStatus(){
    return status_;
}

void paramVal::setAlarmStatus(int alarmStatus){
    if (alarmStatus_ != alarmStatus) {
        setValueChanged();
        alarmStatus_ = alarmStatus;
        // We need to do callbacks on all bits if the status has changed
        if (type == asynParamUInt32Digital) uInt32CallbackMask = 0xFFFFFFFF;
    }
}

int paramVal::getAlarmStatus(){
    return alarmStatus_;
}

void paramVal::setAlarmSeverity(int alarmSeverity){
    if (alarmSeverity_ != alarmSeverity) {
        setValueChanged();
        alarmSeverity_ = alarmSeverity;
        // We need to do callbacks on all bits if the status has changed
        if (type == asynParamUInt32Digital) uInt32CallbackMask = 0xFFFFFFFF;
    }
}

int paramVal::getAlarmSeverity(){
    return alarmSeverity_;
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
  * \throws ParamValWrongType if type is not asynParamInt32
  */
void paramVal::setInteger(epicsInt32 value)
{
    if (type != asynParamInt32)
        throw ParamValWrongType("paramVal::setInteger can only handle asynParamInt32");
    if (!isDefined() || (data.ival != value))
    {
        setDefined(true);
        data.ival = value;
        setValueChanged();
    }
}

/** Gets the value for an integer in the parameter library.
  * \throws ParamValWrongType if type is not asynParamInt32
  * \throws paramValNotDefined if the value is not defined
  */
epicsInt32 paramVal::getInteger()
{
    if (type != asynParamInt32)
        throw ParamValWrongType("paramVal::getInteger can only handle asynParamInt32");
    if (!isDefined())
        throw ParamValNotDefined("paramVal::getInteger value not defined");
    return data.ival;
}

/** Sets the value for a UInt32 in the parameter library.
  * \param[in] value Value to set.
  * \param[in] valueMask Mask to use when setting the value.
  * \param[in] interruptMask Mask of bits that have changed even if the value has not changed
  * \return Returns asynParamBadIndex if the index is not valid or asynParamWrongType if the parameter type is not asynParamUInt32Digital. */
void paramVal::setUInt32(epicsUInt32 value, epicsUInt32 valueMask, epicsUInt32 interruptMask)
{
    epicsUInt32 oldValue;

    if (type != asynParamUInt32Digital)
        throw ParamValWrongType("paramVal::setUInt32 can only handle asynParamUInt32Digital");
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

/** Gets the value for a UInt32 in the parameter library.
  * \param[in] valueMask Mask to use when getting the value.
  * \return Returns asynParamBadIndex if the index is not valid or asynParamWrongType if the parameter type is not asynParamUInt32Digital. */
epicsUInt32 paramVal::getUInt32(epicsUInt32 valueMask)
{
    if (type != asynParamUInt32Digital)
        throw ParamValWrongType("paramVal::getUInt32 can only handle asynParamUInt32Digital");
    if (!isDefined())
        throw ParamValNotDefined("paramVal::getUInt32 value not defined");
    return data.uival & valueMask;
}


/** Sets the value for a double in the parameter library.
  * \param[in] value Value to set.
  * \return Returns asynParamBadIndex if the index is not valid or asynParamWrongType if the parameter type is not asynParamFloat64. */
void paramVal::setDouble(double value)
{
    if (type != asynParamFloat64)
        throw ParamValWrongType("paramVal::setDouble can only handle asynParamFloat64");
    if (!isDefined() || (data.dval != value))
    {
        setDefined(true);
        data.dval = value;
        setValueChanged();
    }
}

/** Gets the value for an double in the parameter library.
  * \throws ParamValWrongType if type is not asynParamFloat64
  * \throws paramValNotDefined if the value is not defined
  */
double paramVal::getDouble()
{
    if (type != asynParamFloat64)
        throw ParamValWrongType("paramVal::getDouble can only handle asynParamFloat64");
    if (!isDefined())
        throw ParamValNotDefined("paramVal::getDouble value not defined");
    return data.dval;
}

/** Sets the value for a string in the parameter library.
  * \param[out] value Address of value to set.
  * \return Returns asynParamBadIndex if the index is not valid or asynParamWrongType if the parameter type is not asynParamOctet. */
void paramVal::setString(const std::string& value)
{
    if (type != asynParamOctet)
        throw ParamValWrongType("paramVal::setString can only handle asynParamOctet");
    if (!isDefined() || (sval != value))
    {
        setDefined(true);
        sval = value;
        setValueChanged();
    }
}

/** Gets the value for a string in the parameter library.
  * \throws ParamValWrongType if type is not asynParamOctet
  * \throws paramValNotDefined if the value is not defined
  */
const std::string& paramVal::getString()
{
    if (type != asynParamOctet)
        throw ParamValWrongType("paramVal::getString can only handle asynParamOctet");
    if (!isDefined())
        throw ParamValNotDefined("paramVal::getString value not defined");
    return sval;
}

void paramVal::report(int id, FILE *fp, int details)
{
    switch (type)
    {
        case asynParamInt32:
            if (isDefined())
                fprintf(fp, "Parameter %d type=asynInt32, name=%s, value=%d, status=%d\n", id, getName(), getInteger(), getStatus());
            else
                fprintf(fp, "Parameter %d type=asynInt32, name=%s, value is undefined\n", id, getName());
            break;
        case asynParamUInt32Digital:
            if (isDefined())
                fprintf(fp, "Parameter %d type=asynUInt32Digital, name=%s, value=0x%x, status=%d, risingMask=0x%x, fallingMask=0x%x, callbackMask=0x%x\n",
                    id, getName(), getUInt32(0xFFFFFFFF), getStatus(),
                    uInt32RisingMask, uInt32FallingMask, uInt32CallbackMask );
            else
                fprintf(fp, "Parameter %d type=asynUInt32Digital, name=%s, value is undefined\n", id, getName());
            break;
        case asynParamFloat64:
            if (isDefined())
                fprintf(fp, "Parameter %d type=asynFloat64, name=%s, value=%f, status=%d\n", id, getName(), getDouble(), getStatus());
            else
                fprintf(fp, "Parameter %d type=asynFloat64, name=%s, value is undefined\n", id, getName());
            break;
        case asynParamOctet:
            if (isDefined())
                fprintf(fp, "Parameter %d type=string, name=%s, value=%s, status=%d\n", id, getName(), getString().c_str(), getStatus());
            else
                fprintf(fp, "Parameter %d type=string, name=%s, value is undefined\n", id, getName());
            break;
        case asynParamInt8Array:
            if (isDefined())
                fprintf(fp, "Parameter %d type=asynInt8Array, name=%s, value=%p, status=%d\n", id, getName(), data.pi8, getStatus());
            else
                fprintf(fp, "Parameter %d type=asynInt8Array, name=%s, value is undefined\n", id, getName());
            break;
        case asynParamInt16Array:
            if (isDefined())
                fprintf(fp, "Parameter %d type=asynInt16Array, name=%s, value=%p, status=%d\n", id, getName(), data.pi16, getStatus() );
            else
                fprintf(fp, "Parameter %d type=asynInt16Array, name=%s, value is undefined\n", id, getName());
            break;
        case asynParamInt32Array:
            if (isDefined())
                fprintf(fp, "Parameter %d type=asynInt32Array, name=%s, value=%p, status=%d\n", id, getName(), data.pi32, getStatus() );
            else
                fprintf(fp, "Parameter %d type=asynInt32Array, name=%s, value is undefined\n", id, getName());
            break;
        case asynParamFloat32Array:
            if (isDefined())
                fprintf(fp, "Parameter %d type=asynFloat32Array, name=%s, value=%p, status=%d\n", id, getName(), data.pf32, getStatus() );
            else
                fprintf(fp, "Parameter %d type=asynFloat32Array, name=%s, value is undefined\n", id, getName());
            break;
        case asynParamFloat64Array:
            if (isDefined())
                fprintf(fp, "Parameter %d type=asynFloat64Array, name=%s, value=%p, status=%d\n", id, getName(), data.pf64, getStatus() );
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
