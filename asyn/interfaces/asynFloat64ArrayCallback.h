/* asynFloat64ArrayCallback.h

    10-July-2004  MLR  Mark Rivers

*/

#ifndef asynFloat64ArrayCallbackH
#define asynFloat64ArrayCallbackH

#include <asynDriver.h>
#include <epicsTypes.h>

#define asynFloat64ArrayCallbackType "asynFloat64ArrayCallback"

typedef void (*asynFloat64ArrayDataCallback)(void *drvPvt, epicsFloat64 *data);
typedef void (*asynFloat64ArrayIntervalCallback)(void *drvPvt, double seconds);

typedef struct asynFloat64ArrayCallback {
    double     (*setCallbackInterval)(void *drvPvt, asynUser *pasynUser,
                                      double seconds);
    double     (*getCallbackInterval)(void *drvPvt, asynUser *pasynUser);
    asynStatus (*registerCallbacks)  (void *drvPvt, asynUser *pasynUser,
                                      asynFloat64ArrayDataCallabck dataCallback,
                                      asynFloat64ArrayIntervalCallback 
                                          intervalCallback, 
                                      void *pvt);
} asynFloat64ArrayCallback;

#endif /* asynFloat64ArrayCallbackH*/
