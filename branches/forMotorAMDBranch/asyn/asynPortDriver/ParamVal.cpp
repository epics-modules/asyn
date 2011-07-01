/*
 * paramVal.cpp
 *
 *  Created on: Jun 15, 2011
 *      Author: hammonds
 */
#include <ParamVal.h>
#include <ParamValInvalidMethod.h>
#include <ParamListCallbackError.h>

#include <stdlib.h>
#include <epicsTypes.h>
#include <epicsString.h>
#include <asynPortDriver.h>

ParamVal::ParamVal(const char *name, int index, paramList *parentList){
  this->name = epicsStrDup(name);
  this->parentList = parentList;
  this->index = index;
  valueDefined = false;
  type = asynParamTypeUndefined;
}

ParamVal::ParamVal()
{
  this->name = epicsStrDup("Blank");
  this->index = index;
  valueDefined = false;
  type = asynParamTypeUndefined;

}

ParamVal::~ParamVal()
{
  //free(name);
}

const char* ParamVal::typeNames[] = {
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

/** Return the name associated with this parameter
 */
char* ParamVal::getName()
{
  return this->name;
}

/** Return the type associated with this parameter
 */
asynParamType ParamVal::getType()
{
  return this->type;
}

/** Place holder function to return the value of the parameter
 * as a double.  Subclasses should override this method if
 * they can return the value as a double.
 */
double ParamVal::getDouble()
{
  throw ParamValInvalidMethod(this);
}

/** Place holder function to return the value of the parameter
 * as an Integer.  Subclasses should override this method if
 * they can return the value as an integer.
 */
int ParamVal::getInteger()
{
  throw ParamValInvalidMethod(this);
}

/** Place holder function to return the value of the parameter
 * as a string.  Subclasses should override this method if
 * they can return the value as an String.
 */
char* ParamVal::getString(unsigned int maxChars, char *value)
{
  throw ParamValInvalidMethod(this);
}

/** Place holder function to return the value of the parameter
 * as an unsigned Int32.  Subclasses should override this method if
 * they can return the value as a UInt32
 */
epicsUInt32 ParamVal::getUInt32(epicsUInt32 mask)
{
  throw ParamValInvalidMethod(this);
}

/** Place holder function to return the Interrupt mask for the parameter.
 *   Subclasses should override this method if appropriate.
 */
epicsUInt32 ParamVal::getUInt32Interrupt(interruptReason reason)
{
  throw ParamValInvalidMethod(this);
}

/** Place holder function to set the value of the parameter
 * as a double.  Subclasses should override this method if
 * they can set the value as a double.
 */
void ParamVal::setDouble(double value)
{
  throw ParamValInvalidMethod(this);
}

/** Place holder function to set the value of the parameter
 * as an Integer.  Subclasses should override this method if
 * they can set the value as an integer.
 */
void ParamVal::setInteger(int value)
{
  throw ParamValInvalidMethod(this);
}

/** Place holder function to set the value of the parameter
 * as a string.  Subclasses should override this method if
 * they can set the value as an string.
 */
void ParamVal::setString(const char *value)
{
  throw ParamValInvalidMethod(this);
}

/** Place holder function to set the value of the parameter
 * as an unsigned Int32.  Subclasses should override this method if
 * they can set the value as a UInt32
 */
void ParamVal::setUInt32(epicsUInt32 value, epicsUInt32 mask)
{
  throw ParamValInvalidMethod(this);
}

/** Place holder function to return the Interrupt mask for the parameter.
 *   Subclasses should override this method if appropriate.
 */
void ParamVal::setUInt32Interrupt(epicsUInt32 mask, interruptReason reason)
{
  throw ParamValInvalidMethod(this);
}

/** Place holder function to clear the Interrupt mask for the
 * parameter.  Subclasses should override this as appropriate.
 */
void ParamVal::clearUInt32Interrupt(epicsUInt32 mask)
{
  throw ParamValInvalidMethod(this);
}

/** Check if the value has been set.
 */
bool ParamVal::isValueDefined()
{
  return valueDefined;
}

/** Mark that the value has been set.
 *
 */
void ParamVal::markValueIsDefined()
{
  valueDefined = true;
}

/** Notify the containing list that the value has changed.
 */
void ParamVal::notifyList()
{
  if (parentList->setFlag(index) != asynSuccess){
    throw ParamListCallbackError(parentList);
  }
}

/** Return a descriptive name associated with parameter type.
 */
const char* ParamVal::getTypeName(){
  return typeNames[type];
}

void ParamVal::report(FILE *fp, int details)
{
  if (isValueDefined())
    reportDefinedValue(fp, details);
  else
    fprintf(fp,
        "Parameter %d type=%s, name=%s, value is undefined\n",
        getIndex(), getTypeName(), getName());

}

void ParamVal::reportDefinedValue(FILE *fp, int details){
  fprintf(fp, "This report is coming from the base class ParamVal.  "
      "This method report should come from a subclass\n");
}

asynStatus ParamVal::callCallback(int addr){
  return asynSuccess;
}

int ParamVal::getIndex()
{
  return index;
}
