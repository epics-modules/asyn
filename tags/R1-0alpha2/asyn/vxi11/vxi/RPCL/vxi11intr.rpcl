/******************************************************************************
 *
 * vxi11intr.rpcl
 *
 *	This file is best viewed with a tabwidth of 4
 *
 ******************************************************************************
 *
 * TODO:
 *
 ******************************************************************************
 *
 *	Original Author:	someone from VXIbus Consortium
 *	Current Author:		Benjamin Franksen
 *	Date:				03-06-97
 *
 *	RPCL description of the intr-channel of the TCP/IP Instrument Protocol 
 *	Specification.
 *
 *
 * Modification Log:
 * -----------------
 * .00	03-06-97	bfr		created this file
 *
 ******************************************************************************
 *
 * Notes: 
 *
 *	This stuff is literally from
 *
 *		"VXI-11, Ref 1.0 : TCP/IP Instrument Protocol Specification"
 *
 */

struct Device_SrqParms
{
	opaque	handle<>;
};

program DEVICE_INTR
{
	version DEVICE_INTR_VERSION
	{
		void device_intr_srq (Device_SrqParms) = 30;
	} = 1;
} = 0x0607B1;
