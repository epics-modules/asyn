/* asynInt32Callback.h

    10-July-2004  MLR  Mark Rivers

*/

#ifndef asynInt32CallbackH
#define asynInt32CallbackH

#include <asynDriver.h>
#include <epicsTypes.h>

#define asynInt32CallbackType "asynInt32Callback"

typedef void (*asynInt32DataCallback)(void *drvPvt, epicsInt32 data);
typedef void (*asynInt32IntervalCallback)(void *drvPvt, double seconds);

typedef struct asynInt32Callback {
    double     (*setCallbackInterval)(void *drvPvt, asynUser *pasynUser,
                                      double seconds);
    double     (*getCallbackInterval)(void *drvPvt, asynUser *pasynUser);
    asynStatus (*registerCallbacks)(void *drvPvt, asynUser *pasynUser,
                                    asynInt32DataCallback dataCallback, 
                                    asynInt32IntervalCallback intervalCallback,
                                    void *pvt);
} asynInt32Callback;

#endif /* asynInt32CallbackH*/
