#ifndef asynParamType_H
#define asynParamType_H


#include "asynParamType.h"

/** Structure for storing parameter value in parameter library */
typedef struct
{
    asynParamType type; /**< Parameter data type */
    char *name;         /**< Parameter name */
    bool valueDefined;
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
} paramVal;

#endif
