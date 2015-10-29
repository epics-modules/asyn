/*
 * testOutputReadback.h
 * 
 * Asyn driver that inherits from the asynPortDriver class to test error handling in both normally scanned
 * and I/O Intr scanned records
 *
 * Author: Mark Rivers
 *
 * Created April 29, 2012
 */
#include "asynPortDriver.h"

/* These are the drvInfo strings that are used to identify the parameters.
 * They are used by asyn clients, including standard asyn device support */
#define P_Int32ValueString                  "INT32_VALUE"             /* asynInt32,         r/w */
#define P_BinaryInt32ValueString            "BINARY_INT32_VALUE"      /* asynInt32,         r/w */
#define P_MultibitInt32ValueString          "MULTIBIT_INT32_VALUE"    /* asynInt32,         r/w */
#define P_Float64ValueString                "FLOAT64_VALUE"           /* asynFloat64,       r/w */
#define P_UInt32DigitalValueString          "UINT32D_VALUE"           /* asynUInt32Digital, r/w */
#define P_BinaryUInt32DigitalValueString    "BINARY_UINT32D_VALUE"    /* asynUInt32Digital, r/w */
#define P_MultibitUInt32DigitalValueString  "MULTIBIT_UINT32D_VALUE"  /* asynUInt32Digital, r/w */

/** Class that tests error handing of the asynPortDriver base class using both normally scanned records and I/O Intr
  * scanned records. */
class testOutputReadback : public asynPortDriver {
public:
    testOutputReadback(const char *portName, int initialReadStatus);
    virtual asynStatus readInt32(asynUser *pasynUser, epicsInt32 *value);
    virtual asynStatus readFloat64(asynUser *pasynUser, epicsFloat64 *value);
    virtual asynStatus readUInt32Digital(asynUser *pasynUser, epicsUInt32 *value, epicsUInt32 mask);

protected:
    /** Values used for pasynUser->reason, and indexes into the parameter library. */
    int P_Int32Value;
    #define FIRST_COMMAND P_Int32Value
    int P_BinaryInt32Value;
    int P_MultibitInt32Value;
    int P_Float64Value;
    int P_UInt32DigitalValue;
    int P_BinaryUInt32DigitalValue;
    int P_MultibitUInt32DigitalValue;
    #define LAST_COMMAND P_MultibitUInt32DigitalValue
 
private:
    /* Our data */
    asynStatus initialReadStatus_;
};


#define NUM_PARAMS (int)(&LAST_COMMAND - &FIRST_COMMAND + 1)

