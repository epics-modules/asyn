/*
 * OctetCallback.h
 *
 *  Created on: Jul 19, 2011
 *      Author: hammonds
 */

#ifndef OCTETCALLBACK_H_
#define OCTETCALLBACK_H_

#include "ParamCallback.h"
#include <epicsTypes.h>

/** Specialize the template for the UInt32DigitalCallback.  I think that this will
 * always be a special case since the interruptMask is unique to UInt32Digital params.
 *
 */
template<class epicsType, class interruptType> class OctetCallback: public ParamCallback<
		epicsType, interruptType>
{
public:
	OctetCallback(int command, int addr, void *interruptPvt, epicsType value);

protected:
	virtual void sendCallback(void *interrupt);

private:
	epicsType value;

};

// Define the templates for the methods that will be used by this
// template class.

#include <asynOctet.h>
#include <string.h>

/** Constructor for the template.
 *
 */
template<class epicsType, class interruptType> OctetCallback<epicsType,
		interruptType>::OctetCallback(int command, int addr, void *interruptPvt,
		epicsType value) :
	ParamCallback<epicsType, interruptType> (command, addr, interruptPvt),
			value(value)
{
	// Intentionally blank
}

/** Method called by the doCallback method in ParamCallback. This performs
 * the actual callback.
 *
 */
template<class epicsType, class interruptType> void OctetCallback<epicsType,
		interruptType>::sendCallback(void *interrupt)
{
	interruptType *pInterrupt = (interruptType *) interrupt;
	if (this->correctParam(pInterrupt))
	{
		pInterrupt->callback(pInterrupt->userPvt, pInterrupt->pasynUser,
				this->value, strlen(this->value), ASYN_EOM_END);
	}
}

#endif /* OCTETCALLBACK_H_ */
