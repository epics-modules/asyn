#include <devCommonGpib.h>

int
boSRQonOff(struct gpibDpvt *pdpvt, int p1, int p2,char **p3)
{   
    boRecord *pbo = (boRecord *)pdpvt->precord;
    asynGpib  *pasynGpib  = pdpvt->pasynGpib;   

    pasynGpib->pollAddr(pdpvt->asynGpibPvt,pdpvt->pasynUser,pbo->rval);
    /* initiate a polling round :*/
    if (pbo->rval == 1) pasynGpib->srqHappened(pdpvt->asynGpibPvt);
    return 0;
}
