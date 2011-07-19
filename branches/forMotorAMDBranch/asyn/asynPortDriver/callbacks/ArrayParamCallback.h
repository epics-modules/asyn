/*
 * ArrayParamCallback.h
 *
 *  Created on: Jul 19, 2011
 *      Author: hammonds
 */

#ifndef ARRAYPARAMCALLBACK_H_
#define ARRAYPARAMCALLBACK_H_

#include "ParamCallback.h"
#include <epicsTypes.h>

template<class epicsType, class interruptType> class ArrayParamCallback: public ParamCallback<
		epicsType, interruptType>
{
public:
	ArrayParamCallback(int command, int addr, void *interruptPvt,
			epicsType value, size_t nElements);

protected:
	virtual void sendCallback(void* interrupt);
	virtual bool correctParam(interruptType *pInterrupt);
	virtual epicsType getInterruptValue();
	int getInterruptAddress(interruptType *pInterrupt);
	epicsType value;
	size_t nElements
};

// Define the templates for the methods that will be used by this
// template class.

/** Constructor for the template.
 *
 */
template<class epicsType, class interruptType> ArrayParamCallback<epicsType,
		interruptType>::ArrayParamCallback(int command, int addr,
		void *interruptPvt, epicsType value, size_t nElements):
	ParamCallback<epicsType, interruptType> (command, addr, interruptPvt),
			value(value), nElements(nElements)
{
	// Intentionally blank
}

/** Method called by the doCallback method in ParamCallback. This performs
 * the actual callback.
 *
 */
template<class epicsType, class interruptType> void ScalarParamCallback<epicsType,
		interruptType>::sendCallback(void *interrupt)
{
	interruptType *pInterrupt = (interruptType *) interrupt;
	if (this->correctParam(pInterrupt))
	{
		pInterrupt->callback(pInterrupt->userPvt, pInterrupt->pasynUser, this->value, this->nElements);
	}
}



#endif /* ARRAYPARAMCALLBACK_H_ */
