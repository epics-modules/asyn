/*
 * testOutputCallback.h
 * 
* Asyn driver that inherits from the asynPortDriver class to test output records using callbacks

 *
 * Author: Mark Rivers
 *
 * Created November 9, 2017
 */
 
#include <epicsEvent.h>
#include <asynPortDriver.h>

/* These are the drvInfo strings that are used to identify the parameters.
 * They are used by asyn clients, including standard asyn device support */
#define P_Int32ValueString                  "INT32_VALUE"             /* asynInt32,         r/w */
#define P_Int32BinaryValueString            "INT32_BINARY_VALUE"      /* asynInt32,         r/w */
#define P_UInt32DigitalValueString          "UINT32D_VALUE"           /* asynUInt32Digital, r/w */
#define P_Float64ValueString                "FLOAT64_VALUE"           /* asynFloat64,       r/w */
#define P_OctetValueString                  "OCTET_VALUE"             /* asynOctet,         r/w */
#define P_NumCallbacksString                "NUM_CALLBACKS"           /* asynInt32,         r/w */
#define P_SleepTimeString                   "SLEEP_TIME"              /* asynFloat64,       r/w */
#define P_TriggerCallbacksString            "TRIGGER_CALLBACKS"       /* asynInt32,         r/w */

/** Class that tests error handing of the asynPortDriver base class using both normally scanned records and I/O Intr
  * scanned records. */
class testOutputCallback : public asynPortDriver {
public:
    testOutputCallback(const char *portName, int canBlock);
    asynStatus writeInt32(asynUser *pasynUser, epicsInt32 value);
    asynStatus writeUInt32Digital(asynUser *pasynUser, epicsUInt32 value, epicsUInt32 mask);
    asynStatus writeFloat64(asynUser *pasynUser, epicsFloat64 value);
    asynStatus writeOctet(asynUser *pasynUser, const char *value, size_t maxChars, size_t *nActual);
    void callbackThread();

private:
    int P_Int32Value;
    int P_Int32BinaryValue;
    int P_UInt32DigitalValue;
    int P_Float64Value;
    int P_OctetValue;
    int P_TriggerCallbacks;
    int P_NumCallbacks;
    int P_SleepTime;

    epicsEventId callbackEvent_;
    int numCallbacks_;
    epicsFloat64 sleepTime_;
    void doInt32Callbacks();
    void doInt32BinaryCallbacks();
    void doUInt32DigitalCallbacks();
    void doFloat64Callbacks();
    void doOctetCallbacks();
};
