/* asynInt32ArrayArrayCallback.h

    10-July-2004  MLR  Mark Rivers

*/

#ifndef asynInt32ArrayCallbackH
#define asynInt32ArrayCallbackH

#include <asynDriver.h>
#include <epicsTypes.h>

#define asynInt32ArrayCallbackType "asynInt32ArrayCallback"

typedef void (*asynInt32ArrayClbk)(void *drvPvt, epicsInt32 *readback);

typedef struct {
    double     (*setCallbackInterval)(void *drvPvt, asynUser *pasynUser,
                                      double seconds);
    double     (*getCallbackInterval)(void *drvPvt, asynUser *pasynUser);
    asynStatus (*registerCallback)   (void *drvPvt, asynUser *pasynUser,
                                      asynInt32ArrayClbk callback, void *pvt);
} asynInt32ArrayCallback;

#endif /* asynInt32ArrayCallbackH */
