#ifndef paramList_H
#define paramList_H

#ifdef __cplusplus
#include "asynStandardInterfaces.h"
#include "asynParamType.h"
#include "paramVal.h"



/** Class to support parameter library (also called parameter list);
  * set and get values indexed by parameter number (pasynUser->reason)
  * and do asyn callbacks when parameters change.
  * The parameter class supports 3 types of parameters: int, double
  * and dynamic-length strings. */
class paramList {
public:
    paramList(int nVals, asynStandardInterfaces *pasynInterfaces);
    ~paramList();
    paramVal* getParameter(int index);
    asynStatus createParam(const char *name, asynParamType type, int *index);
    asynStatus findParam(const char *name, int *index);
    asynStatus getName(int index, const char **name);
    asynStatus setInteger(int index, int value);
    asynStatus setUInt32(int index, epicsUInt32 value, epicsUInt32 valueMask, epicsUInt32 interruptMask);
    asynStatus setDouble(int index, double value);
    asynStatus setString(int index, const char *string);
    asynStatus getInteger(int index, int *value);
    asynStatus getUInt32(int index, epicsUInt32 *value, epicsUInt32 mask);
    asynStatus getDouble(int index, double *value);
    asynStatus getString(int index, int maxChars, char *value);
    asynStatus setUInt32Interrupt(int index, epicsUInt32 mask, interruptReason reason);
    asynStatus clearUInt32Interrupt(int index, epicsUInt32 mask);
    asynStatus getUInt32Interrupt(int index, epicsUInt32 *mask, interruptReason reason);
    asynStatus callCallbacks(int addr);
    asynStatus callCallbacks();
    asynStatus setStatus(int index, asynStatus status);
    asynStatus getStatus(int index, asynStatus *status);
    void report(FILE *fp, int details);

private:
    asynStatus setFlag(int index);
    asynStatus int32Callback(int command, int addr);
    asynStatus uint32Callback(int command, int addr, epicsUInt32 interruptMask);
    asynStatus float64Callback(int command, int addr);
    asynStatus octetCallback(int command, int addr);
    void registerParameterChange(paramVal *param, int index);
    int nextParam;
    int nVals;
    int nFlags;
    asynStandardInterfaces *pasynInterfaces;
    int *flags;
    paramVal **vals;
};

#endif /* cplusplus */

#endif

