/* asynFloat64ArrayCallback.h

    10-July-2004  MLR  Mark Rivers

*/

#ifndef asynFloat64ArrayCallbackH
#define asynFloat64ArrayCallbackH

#include <asynDriver.h>
#include <epicsTypes.h>

#define asynFloat64ArrayCallbackType "asynFloat64ArrayCallback"

typedef void (*asynFloat64ArrayClbk)(void *drvPvt, epicsFloat64 *readback);

typedef struct {
    double     (*setCallbackInterval)(void *drvPvt, asynUser *pasynUser,
                                      double seconds);
    double     (*getCallbackInterval)(void *drvPvt, asynUser *pasynUser);
    asynStatus (*registerCallback)   (void *drvPvt, asynUser *pasynUser,
                                      asynFloat64ArrayClbk callback, void *pvt);
} asynFloat64ArrayCallback;

#endif /* asynFloat64ArrayCallbackH*/
