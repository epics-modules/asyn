/* asynInt32ArrayArrayCallback.h

    10-July-2004  MLR  Mark Rivers

*/

#ifndef asynInt32ArrayCallbackH
#define asynInt32ArrayCallbackH

#include <asynDriver.h>
#include <epicsTypes.h>

#define asynInt32ArrayCallbackType "asynInt32ArrayCallback"

typedef void (*asynInt32ArrayDataCallback)(void *drvPvt, epicsInt32 *daya);
typedef void (*asynInt32ArrayIntervalCallback)(void *drvPvt, double seconds);

typedef struct asynInt32ArrayCallback {
    double     (*setCallbackInterval)(void *drvPvt, asynUser *pasynUser,
                                      double seconds);
    double     (*getCallbackInterval)(void *drvPvt, asynUser *pasynUser);
    asynStatus (*registerCallbacks)   (void *drvPvt, asynUser *pasynUser,
                                      asynInt32ArrayDataCallback dataCallback, 
                                      asynInt32ArrayIntervalCallback 
                                          intervalCallback, 
                                      void *pvt);
} asynInt32ArrayCallback;

#endif /* asynInt32ArrayCallbackH */
