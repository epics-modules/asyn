/*
 * UInt32DigitalParam.h
 *
 *  Created on: Jun 15, 2011
 *      Author: hammonds
 */

#ifndef UINT32DIGITALPARAM_H_
#define UINT32DIGITALPARAM_H_
#include <ParamVal.h>

class UInt32DigitalParam: public ParamVal
{
public:
  UInt32DigitalParam(const char *name, int index, paramList *parentList);
  virtual void setUInt32(epicsUInt32 value, epicsUInt32 mask);
  virtual epicsUInt32 getUInt32(epicsUInt32 mask);
  virtual void setUInt32Interrupt(epicsUInt32 mask, interruptReason reason);
  virtual void clearUInt32Interrupt(epicsUInt32 mask);
  virtual epicsUInt32 getUInt32Interrupt(interruptReason reason);
  virtual asynStatus callCallback(int addr);

protected:
  virtual void reportDefinedValue(FILE *fp, int details);

private:
  epicsUInt32 value;
  epicsUInt32 uInt32Mask;
  epicsUInt32 uInt32InterruptMask;
  epicsUInt32 uInt32InterruptReason;
  void setMaskedSetBits(epicsUInt32 value, epicsUInt32 mask);
  void clearMaskedClearBits(epicsUInt32 value, epicsUInt32 mask);
};

#endif /* UINT32DIGITALPARAM_H_ */
