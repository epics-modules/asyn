/*
 * ScalarParamCallback.h
 *
 *  Created on: Jul 18, 2011
 *      Author: hammonds
 */

#ifndef SCALARPARAMCALLBACK_H_
#define SCALARPARAMCALLBACK_H_

#include "ParamCallback.h"
#include <epicsTypes.h>

template<class epicsType, class interruptType> class ScalarParamCallback:
	public ParamCallback<epicsType, interruptType>
{
public:
	ScalarParamCallback(int command, int addr, void *interruptPvt, epicsType value);

protected:
	 virtual void sendCallback(void* interrupt);
	 virtual epicsType getInterruptValue();
	 epicsType value;

};

// Define the templates for the methods that will be used by this
// template class.

/** Constructor for the template.
 *
 */
template<class epicsType, class interruptType> ScalarParamCallback<epicsType,
		interruptType>::ScalarParamCallback(int command, int addr,
		void *interruptPvt, epicsType value):
	ParamCallback<epicsType, interruptType> (command, addr, interruptPvt),
			value(value)
{
	// Intentionally blank
}

template<class epicsType, class interruptType> epicsType ScalarParamCallback<epicsType,
interruptType>::getInterruptValue()
{
	return value;
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
		pInterrupt->callback(pInterrupt->userPvt, pInterrupt->pasynUser, this->getInterruptValue());
	}
}


#endif /* SCALARPARAMCALLBACK_H_ */
