/* asynInt32Callback.h

    10-July-2004  MLR  Mark Rivers

*/

#ifndef asynInt32CallbackH
#define asynInt32CallbackH

#include <asynDriver.h>
#include <epicsTypes.h>

#define asynInt32CallbackType "asynInt32Callback"

typedef void (*asynInt32Clbk)(void *drvPvt, epicsInt32 readback);

typedef struct {
    double     (*setCallbackInterval)(void *drvPvt, asynUser *pasynUser,
                                      double seconds);
    double     (*getCallbackInterval)(void *drvPvt, asynUser *pasynUser);
    asynStatus (*registerCallback)   (void *drvPvt, asynUser *pasynUser,
                                      asynInt32Clbk callback, void *pvt);
} asynInt32Callback;

#endif /* asynInt32CallbackH*/
