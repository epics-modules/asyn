/*
 * asynParameter.h
 *
 *  Created on: Jun 15, 2011
 *      Author: hammonds
 */

#ifndef ASYNPARAMETER_H_
#define ASYNPARAMETER_H_
#include <asynStandardInterfaces.h>
#include <asynParamTypes.h>
#include <asynStandardInterfaces.h>
class paramList;

class ParamVal
{
public:
  ParamVal(const char *name, int index, paramList *parent);
  ParamVal();
  virtual ~ParamVal();
  char* getName();
  asynParamType getType();
  virtual double getDouble();
  virtual int getInteger();
  virtual char* getString(unsigned int maxChars, char *value);
  virtual epicsUInt32 getUInt32(epicsUInt32 mask);
  virtual epicsUInt32 getUInt32Interrupt(interruptReason reason);
  virtual void setDouble(double value);
  virtual void setInteger(int value);
  virtual void setString(const char *string);
  virtual void setUInt32(epicsUInt32 value, epicsUInt32 mask);
  virtual void setUInt32Interrupt(epicsUInt32 mask, interruptReason reason);
  virtual void clearUInt32Interrupt(epicsUInt32 mask);
  void report(FILE *fp, int details);
  virtual const char* getTypeName();
  virtual asynStatus callCallback(int addr);
  static const char *typeNames[];

protected:
  asynParamType type;
  bool isValueDefined();
  void markValueIsDefined();
  void notifyList();
  int getIndex();
  virtual void reportDefinedValue(FILE *fp, int details);
  paramList *parentList;

private:

  char *name;
  int index;
  bool valueDefined;
};

#endif /* ASYNPARAMETER_H_ */
