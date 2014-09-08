/*++
Copyright (c) 2008  Microsoft Corporation

Module Name:

    moufiltr.h

Abstract:

    This module contains the common private declarations for the mouse
    packet filter

Environment:

    kernel mode only

Notes:


Revision History:


--*/

#ifndef MOUFILTER_H
#define MOUFILTER_H

#include <ntddk.h>
#include <kbdmou.h>
#include <ntddmou.h>
#include <ntdd8042.h>
#include <wdf.h>


#define MOUSE_BUFFER_SIZE 16



#if DBG

#define TRAP()                      DbgBreakPoint()

#define DebugPrint(_x_) DbgPrint _x_

#else   // DBG

#define TRAP()

#define DebugPrint(_x_)

#endif


 
typedef struct _DEVICE_EXTENSION
{
 
     //
    // Previous hook routine and context
    //                               
    PVOID UpperContext;
     
    PI8042_MOUSE_ISR UpperIsrHook;

    //
    // Write to the mouse in the context of MouFilter_IsrHook
    //
    IN PI8042_ISR_WRITE_PORT IsrWritePort;

    //
    // Context for IsrWritePort, QueueMousePacket
    //
    IN PVOID CallContext;

    //
    // Queue the current packet (ie the one passed into MouFilter_IsrHook)
    // to be reported to the class driver
    //
    IN PI8042_QUEUE_PACKET QueueMousePacket;

    //
    // The real connect data that this driver reports to
    //
    CONNECT_DATA UpperConnectData;
	
	
	MOUSE_RESET_SUBSTATE state;
	MOUSE_RESET_SUBSTATE state_next;
	unsigned int state_next_register;

	unsigned int state_step;
	unsigned int state_step_register;
	unsigned int state_step_resolution;
	unsigned int state_step_range;
	unsigned int state_step_extcommand;


	UCHAR SampleRate;
	UCHAR MouseResolution;

	UCHAR ibuf[6];
	unsigned int buf_size;
	int packet_type;

	UCHAR magic_knock_response[3];
	UCHAR etp_capabilities[3];

	UCHAR ps2ext_command;
	UCHAR ps2ext_response[3];

	UCHAR register_address;
	UCHAR register_value;
	UCHAR register_response[3];

	ULONG64 time_bytes_received;

	HAND_STATUS hand;
} DEVICE_EXTENSION, *PDEVICE_EXTENSION;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(DEVICE_EXTENSION,
                                        FilterGetData)



//
// Prototypes
//
DRIVER_INITIALIZE DriverEntry;

EVT_WDF_DRIVER_DEVICE_ADD MouFilter_EvtDeviceAdd;
EVT_WDF_IO_QUEUE_IO_INTERNAL_DEVICE_CONTROL MouFilter_EvtIoInternalDeviceControl;
 

/*
NTSTATUS
InitializeLogFile();

NTSTATUS
LogByte(
		IN USHORT Direction,
		IN UCHAR DataByte
	);
*/

VOID
MouFilter_DispatchPassThrough(
     _In_ WDFREQUEST Request,
    _In_ WDFIOTARGET Target
    );

VOID
MouFilter_ServiceCallback(
    IN PDEVICE_OBJECT DeviceObject,
    IN PMOUSE_INPUT_DATA InputDataStart,
    IN PMOUSE_INPUT_DATA InputDataEnd,
    IN OUT PULONG InputDataConsumed
    );










#endif  // MOUFILTER_H


