/*
 * UInt32DigiatalParam.cpp
 *
 *  Created on: Jun 17, 2011
 *      Author: hammonds
 */

#include <UInt32DigitalParam.h>
#include <asynPortDriver.h>

UInt32DigitalParam::UInt32DigitalParam(const char *name, int index, paramList *parentList)
: ParamVal(name, index, parentList), value(0)
{
  type = asynParamInt32Array;
  uInt32InterruptMask = 0;
  uInt32InterruptReason = 0;
}

asynStatus UInt32DigitalParam::setUInt32(epicsUInt32 value, epicsUInt32 mask)
{
  setMaskedSetBits(value, mask);
  clearMaskedClearBits(value, mask);
  markValueIsDefined();
  notifyList();
  return asynSuccess;
}

asynStatus UInt32DigitalParam::getUInt32(epicsUInt32 *value, epicsUInt32 mask)
{
  asynStatus retStat = asynParamUndefined;
  if (isValueDefined())
  {
    *value = this->value &mask;
    retStat = asynSuccess;
  }
  return retStat;
}

asynStatus UInt32DigitalParam::setUInt32Interrupt(epicsUInt32 mask,
    interruptReason reason)
{
  this->uInt32InterruptMask = mask;
  this->uInt32InterruptReason = reason;
  return asynSuccess;
}

asynStatus UInt32DigitalParam::clearUInt32Interrupt(epicsUInt32 mask)
{
  this->uInt32InterruptMask = mask;
  return asynSuccess;
}

asynStatus UInt32DigitalParam::getUInt32Interrupt(epicsUInt32 *mask, interruptReason reason)
{
  *mask = this->uInt32InterruptMask;
  return asynSuccess;
}

/** Set any bits that are set in the value and the mask
 */
void UInt32DigitalParam::setMaskedSetBits(epicsUInt32 value, epicsUInt32 mask)
{
  this->value |=  (value & mask);
}

/** Clear any bits that are clear in the value and set in the mask.
 */
void UInt32DigitalParam::clearMaskedClearBits(epicsUInt32 value, epicsUInt32 mask)
{
  this->value &= (value | mask);
}

void UInt32DigitalParam::reportDefinedValue(FILE *fp, int details)
{
  fprintf(
      fp,
      "Parameter %d type=%s, name=%s, value=%u, mask=%u\n",
      getIndex(), getTypeName(), getName(), value,
      uInt32Mask);
}

asynStatus UInt32DigitalParam::callCallback(int addr)
{
  asynStatus retStat = asynSuccess;
  if(isValueDefined())
    retStat = parentList->uint32Callback(getIndex(), addr, value,
        uInt32InterruptMask);
  return retStat;
}
