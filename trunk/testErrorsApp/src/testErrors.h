/*
 * testErrors.h
 * 
 * Asyn driver that inherits from the asynPortDriver class to test error handling in both normally scanned
 * and I/O Intr scanned records
 *
 * Author: Mark Rivers
 *
 * Created April 29, 2012
 */

#include <epicsEvent.h>
#include "asynPortDriver.h"

#define MAX_ARRAY_POINTS 100
#define OCTET_LENGTH 20
#define MAX_STATUS_ENUMS 6
#define MAX_INT32_ENUMS 16
#define MAX_UINT32_ENUMS 8
#define MAX_ENUM_STRING_SIZE 20

/* These are the drvInfo strings that are used to identify the parameters.
 * They are used by asyn clients, including standard asyn device support */
#define P_StatusReturnString                "STATUS_RETURN"           /* asynInt32,         r/w */
#define P_EnumOrderString                   "ENUM_ORDER"              /* asynInt32,         r/w */
#define P_DoUpdateString                    "DO_UPDATE"               /* asynInt32,         r/w */
#define P_Int32ValueString                  "INT32_VALUE"             /* asynInt32,         r/w */
#define P_BinaryInt32ValueString            "BINARY_INT32_VALUE"      /* asynInt32,         r/w */
#define P_MultibitInt32ValueString          "MULTIBIT_INT32_VALUE"    /* asynInt32,         r/w */
#define P_Float64ValueString                "FLOAT64_VALUE"           /* asynFloat64,       r/w */
#define P_UInt32DigitalValueString          "UINT32D_VALUE"           /* asynUInt32Digital, r/w */
#define P_BinaryUInt32DigitalValueString    "BINARY_UINT32D_VALUE"    /* asynUInt32Digital, r/w */
#define P_MultibitUInt32DigitalValueString  "MULTIBIT_UINT32D_VALUE"  /* asynUInt32Digital, r/w */
#define P_OctetValueString                  "OCTET_VALUE"             /* asynOctet,         r/w */
#define P_Int8ArrayValueString              "INT8_ARRAY_VALUE"        /* asynInt8Array,     r/w */
#define P_Int16ArrayValueString             "INT16_ARRAY_VALUE"       /* asynInt16Array,    r/w */
#define P_Int32ArrayValueString             "INT32_ARRAY_VALUE"       /* asynInt32Array,    r/w */
#define P_Float32ArrayValueString           "FLOAT32_ARRAY_VALUE"     /* asynFloat32Array,  r/w */
#define P_Float64ArrayValueString           "FLOAT64_ARRAY_VALUE"     /* asynFloat64Array,  r/w */

/** Class that tests error handing of the asynPortDriver base class using both normally scanned records and I/O Intr
  * scanned records. */
class testErrors : public asynPortDriver {
public:
    testErrors(const char *portName);
                 
    /* These are the methods that we override from asynPortDriver */
    virtual asynStatus writeInt32(asynUser *pasynUser, epicsInt32 value);
    virtual asynStatus writeFloat64(asynUser *pasynUser, epicsFloat64 value);
    virtual asynStatus writeUInt32Digital(asynUser *pasynUser, epicsUInt32 value, epicsUInt32 mask);
    virtual asynStatus writeOctet(asynUser *pasynUser, const char *value, 
                                  size_t nChars, size_t *nActual);
    virtual asynStatus readInt8Array   (asynUser *pasynUser, epicsInt8 *value,
                                        size_t nElements, size_t *nIn);
    virtual asynStatus readInt16Array  (asynUser *pasynUser, epicsInt16 *value,
                                        size_t nElements, size_t *nIn);
    virtual asynStatus readInt32Array  (asynUser *pasynUser, epicsInt32 *value,
                                        size_t nElements, size_t *nIn);
    virtual asynStatus readFloat32Array(asynUser *pasynUser, epicsFloat32 *value,
                                        size_t nElements, size_t *nIn);
    virtual asynStatus readFloat64Array(asynUser *pasynUser, epicsFloat64 *value,
                                        size_t nElements, size_t *nIn);
    virtual asynStatus readOption(asynUser *pasynUser, const char *key, char *value, int maxChars);
    virtual asynStatus writeOption(asynUser *pasynUser, const char *key, const char *value);
    virtual asynStatus readEnum(asynUser *pasynUser, char *strings[], int values[], int severities[],
                                size_t nElements, size_t *nIn);

    /* These are the methods that are new to this class */
    void callbackTask(void);

protected:
    /** Values used for pasynUser->reason, and indexes into the parameter library. */
    int P_StatusReturn;
    #define FIRST_COMMAND P_StatusReturn
    int P_EnumOrder;
    int P_DoUpdate;
    int P_Int32Value;
    int P_BinaryInt32Value;
    int P_MultibitInt32Value;
    int P_Float64Value;
    int P_UInt32DigitalValue;
    int P_BinaryUInt32DigitalValue;
    int P_MultibitUInt32DigitalValue;
    int P_OctetValue;
    int P_Int8ArrayValue;
    int P_Int16ArrayValue;
    int P_Int32ArrayValue;
    int P_Float32ArrayValue;
    int P_Float64ArrayValue;
    #define LAST_COMMAND P_Float64ArrayValue
 
private:
    /* Our data */
    char *int32EnumStrings_  [MAX_INT32_ENUMS];
    int int32EnumValues_     [MAX_INT32_ENUMS];
    int int32EnumSeverities_ [MAX_INT32_ENUMS];
    char *uint32EnumStrings_ [MAX_UINT32_ENUMS];
    int uint32EnumValues_    [MAX_UINT32_ENUMS];
    int uint32EnumSeverities_[MAX_UINT32_ENUMS];
    epicsEventId eventId_;
    void setEnums();
    epicsInt8     int8ArrayValue_   [MAX_ARRAY_POINTS];
    epicsInt16    int16ArrayValue_  [MAX_ARRAY_POINTS];
    epicsInt32    int32ArrayValue_  [MAX_ARRAY_POINTS];
    epicsFloat32  float32ArrayValue_[MAX_ARRAY_POINTS];
    epicsFloat64  float64ArrayValue_[MAX_ARRAY_POINTS];
    template <typename epicsType> 
        asynStatus doReadArray(asynUser *pasynUser, epicsType *value, 
                           size_t nElements, size_t *nIn, int paramIndex, epicsType *pValue);
};


#define NUM_PARAMS (int)(&LAST_COMMAND - &FIRST_COMMAND + 1)

