/*
 * Callback.h
 *
 *  Created on: Jul 8, 2011
 *      Author: hammonds
 */

#ifndef PARAMCALLBACK_H_
#define PARAMCALLBACK_H_

#include <asynStandardInterfaces.h>
#include <ellLib.h>

template<class epicsType, class interruptType> class ParamCallback
{
public:
	ParamCallback(int command, int address, void *interruptPvt);
	virtual ~ParamCallback();
	int getCommand();
	int getAddress();
	void doCallback();

protected:
	virtual void sendCallback(void *interrupt) = 0;
	virtual void interruptStart();
	virtual void interruptEnd();
	virtual bool correctParam(interruptType *pInterrupt);
	int getInterruptAddress(interruptType *pInterrupt);
	void *interruptPvt;
	ELLLIST *pclientList;

private:
	int command;
	int address;
};

// Define the templates for the methods that will be used by this
// template class.

/** Constructor for the template.
 *
 */
template<class epicsType, class interruptType> ParamCallback<epicsType,
		interruptType>::ParamCallback(int command, int address,
		void *interruptPvt) :
	command(command), address(address), interruptPvt(interruptPvt)
{
}

template<class epicsType, class interruptType> ParamCallback<epicsType,
		interruptType>::~ParamCallback()
{
}

/** getter for the command */
template<class epicsType, class interruptType> int ParamCallback<epicsType,
		interruptType>::getCommand()
{
	return this->command;
}

/** getter for the address */
template<class epicsType, class interruptType> int ParamCallback<epicsType,
		interruptType>::getAddress()
{
	return this->address;
}

/** Template method (in the Design Patterns sense) that sets up the behavior
 * for a callback.  Clients will call this method.  This calls methods that
 * needs to be defined in child classes.
 *
 */
template<class epicsType, class interruptType> void ParamCallback<epicsType,
		interruptType>::doCallback()
{
	interruptNode *pNode;

	this->interruptStart();
	pNode = (interruptNode *) ellFirst(this->pclientList);
	while (pNode)
	{
		//	interruptType *pInterrupt = (interruptType *) pNode->drvPvt;
		this->sendCallback((void*) pNode->drvPvt);
		pNode = (interruptNode *) ellNext(&pNode->node);
	}
	this->interruptEnd();
}
/** start the interrupt associated with the callback.  This uses the
 * int32InterruptPvt of the asynStandardInterfaces
 *
 */
template<class epicsType, class interruptType> void ParamCallback<
		epicsType, interruptType>::interruptStart()
{
	pasynManager->interruptStart(this->interruptPvt, &(this->pclientList));
}

/** start the interrupt associated with the callback.  This uses the
 * int32InterruptPvt of the asynStandardInterfaces
 *
 */
template<class epicsType, class interruptType> void ParamCallback<
		epicsType, interruptType>::interruptEnd()
{
	pasynManager->interruptEnd(this->interruptPvt);
}

template<class epicsType, class interruptType>  int ParamCallback<epicsType,
interruptType>::getInterruptAddress(interruptType *pInterrupt)
{
	int addr;
	pasynManager->getAddr(pInterrupt->pasynUser, &addr);
	if (addr == -1)
	{
		addr = 0;
	}
	return addr;
}

template<class epicsType, class interruptType> bool ParamCallback<epicsType,
interruptType>::correctParam(interruptType *pInterrupt)
{
	return ((this->getCommand() == pInterrupt->pasynUser->reason) &&
			(this->getAddress() == this->getInterruptAddress(pInterrupt)));
}


#endif /* PARAMCALLBACK_H_ */
