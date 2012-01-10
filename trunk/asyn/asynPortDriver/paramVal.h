#ifndef asynParamVal_H
#define asynParamVal_H

#include "stdio.h"
#include "epicsTypes.h"
#include "asynParamType.h"
#ifdef __cplusplus

/** Structure for storing parameter value in parameter library */
class epicsShareFunc ParamVal {
public:
	ParamVal(const char *name);
	ParamVal(const char *name, asynParamType type);
	bool isDefined();
	void setDefined(bool defined);
	bool hasValueChanged();
	void setValueChanged();
	void resetValueChanged();
	char* getName();
	bool nameEquals(const char* name);
	void setInteger(int value);
	void setUInt32(epicsUInt32 value, epicsUInt32 valueMask, epicsUInt32 interruptMask);
	void setDouble(double value);
	void setString(const char *value);
	void report(int id, FILE *fp, int details);
	const char* getTypeName();
	asynParamType type; /**< Parameter data type */
    epicsUInt32 uInt32RisingMask;
    epicsUInt32 uInt32FallingMask;
    epicsUInt32 uInt32CallbackMask;
    /** Union for parameter value */
    union
    {
        epicsInt32   ival;
        epicsUInt32  uival;
        epicsFloat64 dval;
        char         *sval;
        epicsInt8     *pi8;
        epicsInt16    *pi16;
        epicsInt32    *pi32;
        epicsFloat32  *pf32;
        epicsFloat64  *pf64;
        void         *pgp;
    } data;
    static const char* typeNames[];

protected:
    bool valueDefined;
    bool valueChanged;
    char *name;         /**< Parameter name */
};

#endif /* cplusplus */

#endif
