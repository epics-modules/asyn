/*  asynUtils.h

    22-July-2004 Mark Rivers

*/

#ifndef asynUtilsH
#define asynUtilsH

#include <link.h>
#include "asynDriver.h"

typedef struct asynUtils {
    asynStatus (*parseVmeIo)(asynUser *pasynUser, DBLINK *plink, int *card, int *signal, 
                char **port, char **userParam);
} asynUtils;
epicsShareExtern asynUtils *pasynUtils;


#endif /* asynUtilsH */
