/*
 * UInt32DigitalCallback.h
 *
 *  Created on: Jul 19, 2011
 *      Author: hammonds
 */

#ifndef UInt32DigitalCallback_H_
#define UInt32DigitalCallback_H_

#include "ScalarParamCallback.h"
#include <epicsTypes.h>

/** Specialize the template for the UInt32DigitalCallback.  I think that this will
 * always be a special case since the interruptMask is unique to UInt32Digital params.
 *
 */
template<class epicsType, class interruptType> class UInt32DigitalCallback: public ScalarParamCallback<
		epicsType, interruptType>
{
public:
	UInt32DigitalCallback(int command, int addr,
			void *interruptPvt, epicsType value, epicsUInt32 mask);

protected:
	virtual bool correctParam(interruptType *pInterrupt);
	virtual epicsType getInterruptValue();

private:
	epicsUInt32 interruptMask;
};

// Define the templates for the methods that will be used by this
// template class.

#include <asynUInt32Digital.h>

/** Constructor for the template.
 *
 */
template<class epicsType, class interruptType> UInt32DigitalCallback<epicsType,
		interruptType>::UInt32DigitalCallback(int command, int addr,
		void *interruptPvt, epicsType value,
		epicsUInt32 interruptMask) :
			ScalarParamCallback<epicsType, interruptType> (command, addr,
					interruptPvt, value), interruptMask(interruptMask)
{
	// Intentionally blank
}

/** Check things to make sure that this is the correct parmam for the interrupt
 *
 */
template<class epicsType, class interruptType> bool UInt32DigitalCallback<
		epicsType, interruptType>::correctParam(interruptType *pInterrupt)
{
	return ((this->getCommand() == pInterrupt->pasynUser->reason)
			&& (this->getAddress() == this->getInterruptAddress(pInterrupt))
			&& (pInterrupt->mask && this->interruptMask));
}

/**Get the value to be sent to the interrupt
 *
 */
template<class epicsType, class interruptType> epicsType UInt32DigitalCallback<
		epicsType, interruptType>::getInterruptValue()
{
	return this->value & this->interruptMask;
}


#endif /* UInt32DigitalCallback_H_ */
