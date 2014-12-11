#ifndef asynparamVal_H
#define asynparamVal_H

#include "stdio.h"
#include "epicsTypes.h"
#include "asynParamType.h"
#include "asynDriver.h"
#ifdef __cplusplus

/** Structure for storing parameter value in parameter library */
class paramVal {
public:
    paramVal(const char *name);
    paramVal(const char *name, asynParamType type);
    bool isDefined();
    void setDefined(bool defined);
    bool hasValueChanged();
    void setValueChanged();
    void resetValueChanged();
    void setStatus(asynStatus status);
    asynStatus getStatus();
    char* getName();
    bool nameEquals(const char* name);
    void setInteger(epicsInt32 value);
    epicsInt32 getInteger();
    void setUInt32(epicsUInt32 value, epicsUInt32 valueMask, epicsUInt32 interruptMask);
    epicsUInt32 getUInt32(epicsUInt32 valueMask);
    void setDouble(epicsFloat64 value);
    epicsFloat64 getDouble();
    void setString(const char *value);
    char *getString();
    void report(int id, FILE *fp, int details);
    const char* getTypeName();
    asynParamType type; /**< Parameter data type */
    static const char* typeNames[];
    epicsUInt32 uInt32RisingMask;
    epicsUInt32 uInt32FallingMask;
    epicsUInt32 uInt32CallbackMask;

protected:
    asynStatus status_;
    bool valueDefined;
    bool valueChanged;
    char *name;         /**< Parameter name */
    /** Union for parameter value */
    union
    {
        epicsInt32   ival;
        epicsUInt32  uival;
        epicsFloat64 dval;
        char         *sval;
        epicsInt8    *pi8;
        epicsInt16   *pi16;
        epicsInt32   *pi32;
        epicsFloat32 *pf32;
        epicsFloat64 *pf64;
        void         *pgp;
    } data;
};

#endif /* cplusplus */

#endif
