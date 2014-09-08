/*--         
Copyright (c) 2008  Microsoft Corporation

Module Name:

    moufiltr.c

Abstract:

Environment:

    Kernel mode only- Framework Version 

Notes:


--*/

#include <ntddk.h>
#include <kbdmou.h>
#include <ntddmou.h>
#include <ntdd8042.h>
#include <wdf.h>
#include <ntstrsafe.h>

#include "elantechwin.h"
#include "moufiltr.h"
#include "hook.h"






#ifdef ALLOC_PRAGMA
#pragma alloc_text (INIT, DriverEntry)
#pragma alloc_text (PAGE, MouFilter_EvtDeviceAdd)
#pragma alloc_text (PAGE, MouFilter_EvtIoInternalDeviceControl)
#endif

#pragma warning(push)
#pragma warning(disable:4055) // type case from PVOID to PSERVICE_CALLBACK_ROUTINE
#pragma warning(disable:4152) // function/data pointer conversion in expression



NTSTATUS
DriverEntry (
    IN  PDRIVER_OBJECT  DriverObject,
    IN  PUNICODE_STRING RegistryPath
    )
/*++
Routine Description:

     Installable driver initialization entry point.
    This entry point is called directly by the I/O system.

--*/
{
    WDF_DRIVER_CONFIG               config;
    NTSTATUS                                status;

    DebugPrint(("JMOUSE: Mouse Filter Driver Sample - Driver Framework Edition.\n"));
    DebugPrint(("JMOUSE: Built %s %s\n", __DATE__, __TIME__));
    
    // Initiialize driver config to control the attributes that
    // are global to the driver. Note that framework by default
    // provides a driver unload routine. If you create any resources
    // in the DriverEntry and want to be cleaned in driver unload,
    // you can override that by manually setting the EvtDriverUnload in the
    // config structure. In general xxx_CONFIG_INIT macros are provided to
    // initialize most commonly used members.

    WDF_DRIVER_CONFIG_INIT(
        &config,
        MouFilter_EvtDeviceAdd
    );

    //
    // Create a framework driver object to represent our driver.
    //
    status = WdfDriverCreate(DriverObject,
                            RegistryPath,
                            WDF_NO_OBJECT_ATTRIBUTES,
                            &config,
                            WDF_NO_HANDLE); // hDriver optional
    if (!NT_SUCCESS(status)) {
        DebugPrint(("JMOUSE: WdfDriverCreate failed with status 0x%x\n", status));
    }


    return status; 
}

NTSTATUS
MouFilter_EvtDeviceAdd(
    IN WDFDRIVER        Driver,
    IN PWDFDEVICE_INIT  DeviceInit
    )
/*++
Routine Description:

    EvtDeviceAdd is called by the framework in response to AddDevice
    call from the PnP manager. Here you can query the device properties
    using WdfFdoInitWdmGetPhysicalDevice/IoGetDeviceProperty and based
    on that, decide to create a filter device object and attach to the
    function stack.

    If you are not interested in filtering this particular instance of the
    device, you can just return STATUS_SUCCESS without creating a framework
    device.

Arguments:

    Driver - Handle to a framework driver object created in DriverEntry

    DeviceInit - Pointer to a framework-allocated WDFDEVICE_INIT structure.

Return byte:

    NTSTATUS

--*/   
{
    WDF_OBJECT_ATTRIBUTES   deviceAttributes;
    NTSTATUS                            status;
    WDFDEVICE                          hDevice;
    WDF_IO_QUEUE_CONFIG        ioQueueConfig;
    
    UNREFERENCED_PARAMETER(Driver);

    PAGED_CODE();

    DebugPrint(("JMOUSE: Enter FilterEvtDeviceAdd \n"));

    //
    // Tell the framework that you are filter driver. Framework
    // takes care of inherting all the device flags & characterstics
    // from the lower device you are attaching to.
    //
    WdfFdoInitSetFilter(DeviceInit);

    WdfDeviceInitSetDeviceType(DeviceInit, FILE_DEVICE_MOUSE);

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&deviceAttributes,
        DEVICE_EXTENSION);

    
    //
    // Create a framework device object.  This call will in turn create
    // a WDM deviceobject, attach to the lower stack and set the
    // appropriate flags and attributes.
    //
    status = WdfDeviceCreate(&DeviceInit, &deviceAttributes, &hDevice);
    if (!NT_SUCCESS(status)) {
        DebugPrint(("JMOUSE: WdfDeviceCreate failed with status code 0x%x\n", status));
        return status;
    }


    //
    // Configure the default queue to be Parallel. Do not use sequential queue
    // if this driver is going to be filtering PS2 ports because it can lead to
    // deadlock. The PS2 port driver sends a request to the top of the stack when it
    // receives an ioctl request and waits for it to be completed. If you use a
    // a sequential queue, this request will be stuck in the queue because of the 
    // outstanding ioctl request sent earlier to the port driver.
    //
    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&ioQueueConfig,
                             WdfIoQueueDispatchParallel);

    //
    // Framework by default creates non-power managed queues for
    // filter drivers.
    //
    ioQueueConfig.EvtIoInternalDeviceControl = MouFilter_EvtIoInternalDeviceControl;

    status = WdfIoQueueCreate(hDevice,
                            &ioQueueConfig,
                            WDF_NO_OBJECT_ATTRIBUTES,
                            WDF_NO_HANDLE // pointer to default queue
                            );
    if (!NT_SUCCESS(status)) {
        DebugPrint(("JMOUSE: WdfIoQueueCreate failed 0x%x\n", status));
        return status;
    }

    return status;
}



VOID
MouFilter_DispatchPassThrough(
    _In_ WDFREQUEST Request,
    _In_ WDFIOTARGET Target
    )
/*++
Routine Description:

    Passes a request on to the lower driver.


--*/
{
    //
    // Pass the IRP to the target
    //
 
    WDF_REQUEST_SEND_OPTIONS options;
    BOOLEAN ret;
    NTSTATUS status = STATUS_SUCCESS;

    //
    // We are not interested in post processing the IRP so 
    // fire and forget.
    //

	DebugPrint(("JMOUSE: MouFilter_DispatchPassThrough"));
	
    WDF_REQUEST_SEND_OPTIONS_INIT(&options, WDF_REQUEST_SEND_OPTION_SEND_AND_FORGET);

    ret = WdfRequestSend(Request, Target, &options);

    if (ret == FALSE)
	{
        status = WdfRequestGetStatus (Request);
        DebugPrint(("JMOUSE: WdfRequestSend failed: 0x%x\n", status));
        WdfRequestComplete(Request, status);
    }

    return;
}           

VOID
MouFilter_EvtIoInternalDeviceControl(
    IN WDFQUEUE      Queue,
    IN WDFREQUEST    Request,
    IN size_t        OutputBufferLength,
    IN size_t        InputBufferLength,
    IN ULONG         IoControlCode
    )
/*++

Routine Description:

    This routine is the dispatch routine for internal device control requests.
    There are two specific control codes that are of interest:
    
    IOCTL_INTERNAL_MOUSE_CONNECT:
        Store the old context and function pointer and replace it with our own.
        This makes life much simpler than intercepting IRPs sent by the RIT and
        modifying them on the way back up.
                                      
    IOCTL_INTERNAL_I8042_HOOK_MOUSE:
        Add in the necessary function pointers and context bytes so that we can
        alter how the ps/2 mouse is initialized.
                                            
    NOTE:  Handling IOCTL_INTERNAL_I8042_HOOK_MOUSE is *NOT* necessary if 
           all you want to do is filter MOUSE_INPUT_DATAs.  You can remove
           the handling code and all related device extension fields and 
           functions to conserve space.
                                         

--*/
{
    
    PDEVICE_EXTENSION           devExt;
    PCONNECT_DATA               connectData;
    PINTERNAL_I8042_HOOK_MOUSE  hookMouse;
    NTSTATUS                   status = STATUS_SUCCESS;
    WDFDEVICE                 hDevice;
    size_t                           length; 
	char *buffer;
	int f, m;
	PFINGER_STATUS finger;


    UNREFERENCED_PARAMETER(OutputBufferLength);
    UNREFERENCED_PARAMETER(InputBufferLength);

    PAGED_CODE();

    hDevice = WdfIoQueueGetDevice(Queue);
    devExt = FilterGetData(hDevice);

    switch (IoControlCode)
	{

    //
    // Connect a mouse class device driver to the port driver.
    //
    case IOCTL_INTERNAL_MOUSE_CONNECT:
        //
        // Only allow one connection.
        //
        if (devExt->UpperConnectData.ClassService != NULL)
		{
            status = STATUS_SHARING_VIOLATION;
            break;
        }
        
        //
        // Copy the connection parameters to the device extension.
        //
         status = WdfRequestRetrieveInputBuffer(Request,
                            sizeof(CONNECT_DATA),
                            &connectData,
                            &length);
        if(!NT_SUCCESS(status))
		{
            DebugPrint(("JMOUSE: WdfRequestRetrieveInputBuffer failed %x\n", status));
            break;
        }

        
        devExt->UpperConnectData = *connectData;

		//reset the devext's buffer vars
		devExt->buf_size = 0;

		devExt->SampleRate = 200;
		devExt->MouseResolution = 3;
		devExt->state = WaitingForTrigger;

		devExt->state_step = 0;
		devExt->state_step_extcommand = 0;
		devExt->state_step_register = 0;
		devExt->state_step_range = 0;
		devExt->state_step_resolution = 0;


		//reset the moment pointer
		for(f = 0; f < ETP_MAX_FINGERS; f++)
		{
			finger = &(devExt->hand.fingers[f]);

			//make a circular linked list
			for(m = 0; m < MOMENT_BUFFER_SIZE; m++)
			{
				finger->moment_history[m].id = m;

				finger->moment_history[m].dx = 0;
				finger->moment_history[m].dy = 0;

				finger->moment_history[m].vx = 0;

				finger->moment_history[m].pressure = 0;

				finger->moment_history[m].timestamp = 0;
				finger->moment_history[m].dt = 0;

				finger->moment_history[m].prev = &(finger->moment_history[(m - 1 + MOMENT_BUFFER_SIZE) % MOMENT_BUFFER_SIZE]);
				finger->moment_history[m].next = &(finger->moment_history[(m + 1) % MOMENT_BUFFER_SIZE]);
			}

			//point to the current moment
			finger->moment = &(finger->moment_history[0]);

			//reset number of moments in the buffer
			finger->num_moments = 0;
		}
				


        //
        // Hook into the report chain.  Everytime a mouse packet is reported to
        // the system, MouFilter_ServiceCallback will be called
        //
        connectData->ClassDeviceObject = WdfDeviceWdmGetDeviceObject(hDevice);
        connectData->ClassService = MouFilter_ServiceCallback;

		DebugPrint(("JMOUSE: IOCTL_INTERNAL_MOUSE_CONNECT\n"));
        break;

    //
    // Disconnect a mouse class device driver from the port driver.
    //
    case IOCTL_INTERNAL_MOUSE_DISCONNECT:

        //
        // Clear the connection parameters in the device extension.
        //
        // devExt->UpperConnectData.ClassDeviceObject = NULL;
        // devExt->UpperConnectData.ClassService = NULL;

        status = STATUS_NOT_IMPLEMENTED;
        break;

    //
    // Attach this driver to the initialization and byte processing of the 
    // i8042 (ie PS/2) mouse.  This is only necessary if you want to do PS/2
    // specific functions, otherwise hooking the CONNECT_DATA is sufficient
    //
    case IOCTL_INTERNAL_I8042_HOOK_MOUSE:   
		//triggered to add an ISR to the mouse and ultimately replace its default behavior
		//this is where we attach our ISR and then initialize

        DebugPrint(("JMOUSE: IOCTL_INTERNAL_I8042_HOOK_MOUSE\n"));
        
        // Get the input buffer from the request
        // (Parameters.DeviceIoControl.Type3InputBuffer)
        //
        status = WdfRequestRetrieveInputBuffer(Request,
                            sizeof(INTERNAL_I8042_HOOK_MOUSE),
                            &hookMouse,
                            &length);
        if(!NT_SUCCESS(status))
		{
            DebugPrint(("JMOUSE: WdfRequestRetrieveInputBuffer failed %x\n", status));
            break;
        }
      
        //
        // Set isr routine and context and record any bytes from above this driver
        //
        devExt->UpperContext = hookMouse->Context;
        hookMouse->Context = (PVOID) devExt;


        if (hookMouse->IsrRoutine)
		{
            devExt->UpperIsrHook = hookMouse->IsrRoutine;
        }

        hookMouse->IsrRoutine = (PI8042_MOUSE_ISR) MouFilter_IsrHook;

        //
        // Store all of the other functions we might need in the future
        //
        devExt->IsrWritePort = hookMouse->IsrWritePort;
        devExt->CallContext = hookMouse->CallContext;
        devExt->QueueMousePacket = hookMouse->QueueMousePacket;

        status = STATUS_SUCCESS;
        break;

	case IOCTL_INTERNAL_I8042_MOUSE_WRITE_BUFFER:
		//this is triggered when data is being written out to the mouse, so we only process it for debugging purposes

		DebugPrint(("JMOUSE: ioctl: write buffer (%d): ", InputBufferLength));

		status = WdfRequestRetrieveInputBuffer(Request,
				InputBufferLength,
				&buffer,
				&length);
		if(!NT_SUCCESS(status))
		{
			DebugPrint(("JMOUSE: WdfRequestRetrieveInputBuffer failed %x\n", status));
			break;
		}

		//LogByte(SEND, buffer[0]);

		status = STATUS_SUCCESS;
		break;

    //
    // Might want to capture this in the future.  For now, then pass it down
    // the stack.  These queries must be successful for the RIT to communicate
    // with the mouse.
    //
    case IOCTL_MOUSE_QUERY_ATTRIBUTES:
		DebugPrint(("JMOUSE: ioctl: query_attributes\n"));
		break;

	case IOCTL_INTERNAL_I8042_MOUSE_START_INFORMATION:
		DebugPrint(("JMOUSE: ioctl: start_information\n"));
		break;

	default:
		DebugPrint(("JMOUSE: ps/2 ioctrl: 0x%x\n", IoControlCode));
		break;
    }

    if (!NT_SUCCESS(status)) {
        WdfRequestComplete(Request, status);
        return ;
    }

    MouFilter_DispatchPassThrough(Request, WdfDeviceGetIoTarget(hDevice));
}




VOID
MouFilter_ServiceCallback(
    IN PDEVICE_OBJECT DeviceObject,
    IN PMOUSE_INPUT_DATA InputDataStart,
    IN PMOUSE_INPUT_DATA InputDataEnd,
    IN OUT PULONG InputDataConsumed
    )
/*++

Routine Description:

    Called when there are mouse packets to report to the RIT.  You can do 
    anything you like to the packets.  For instance:
    
    o Drop a packet altogether
    o Mutate the contents of a packet 
    o Insert packets into the stream 
                    
Arguments:

    DeviceObject - Context passed during the connect IOCTL
    
    InputDataStart - First packet to be reported
    
    InputDataEnd - One past the last packet to be reported.  Total number of
                   packets is equal to InputDataEnd - InputDataStart
    
    InputDataConsumed - Set to the total number of packets consumed by the RIT
                        (via the function pointer we replaced in the connect
                        IOCTL)

Return byte:

    Status is returned.





	ButtonFlags:
			MOUSE_LEFT_BUTTON_DOWN The left mouse button changed to down. 
			MOUSE_LEFT_BUTTON_UP The left mouse button changed to up. 
			MOUSE_RIGHT_BUTTON_DOWN The right mouse button changed to down. 
			MOUSE_RIGHT_BUTTON_UP The right mouse button changed to up. 
			MOUSE_MIDDLE_BUTTON_DOWN The middle mouse button changed to down. 
			MOUSE_MIDDLE_BUTTON_UP The middle mouse button changed to up. 
			MOUSE_BUTTON_4_DOWN The fourth mouse button changed to down. 
			MOUSE_BUTTON_4_UP The fourth mouse button changed to up. 
			MOUSE_BUTTON_5_DOWN The fifth mouse button changed to down. 
			MOUSE_BUTTON_5_UP The fifth mouse button changed to up. 
			MOUSE_WHEEL Mouse wheel data is present. 

--*/
{
    
    PDEVICE_EXTENSION   devExt;
	unsigned int button_pressed;
	unsigned char b1, b2, b3;
	PMOUSE_INPUT_DATA	pCursor;
	PHAND_STATUS hand;
	PFINGER_STATUS finger;
	ULONG64 time_bytes_processed;
	LONG dx, dy;

	//ULONG dx, dy;
	LONG m;
	PFINGER_MOMENT moment;


	UNREFERENCED_PARAMETER(InputDataConsumed);

	devExt = (PDEVICE_EXTENSION) DeviceObject->DeviceExtension;

        // if there's at least one input packet, this pointer is good. trust the executive's pointers!


	if(devExt->state == WaitingForTrigger)
	{
		button_pressed = 0;

		//parse the buffer
		b1 = (devExt->ibuf[0] & 0x01) ? 1 : 0;
		b2 = (devExt->ibuf[0] & 0x02) ? 1 : 0;
		b3 = (devExt->ibuf[0] & 0x04) ? 1 : 0;

		if(b1)
			button_pressed = 1;
		else if(b2)
			button_pressed = 2;
		else if(b3)
			button_pressed = 3;


		//now reset the buffer
		devExt->buf_size = 0;



		if(button_pressed == 1)
		{
			//push the reset command, just to see what happens
			devExt->state = ExpectingReset;
			DebugPrint(("TRIGGERED by click - pushing 0xFF mouse reset"));
			//InitializeLogFile();
			WRITE_BYTE(devExt, 0xFF);
			//(*devExt->IsrWritePort)(devExt->UpperContext, 0xFF);


			//reset the button state
			for(pCursor = InputDataStart; pCursor < InputDataEnd; pCursor++)
			{
				pCursor->ButtonFlags = 0;
			}
		}
	}
	else if(devExt->state == ReceivingPackets4)
	{
		hand = &(devExt->hand);

		/*
		if(devExt->packet_type == PACKET_V4_MOTION)
			DebugPrint(("[PS/2] motion: %02hhx %02hhx %02hhx %02hhx %02hhx %02hhx", devExt->ibuf[0], devExt->ibuf[1], devExt->ibuf[2], devExt->ibuf[3], devExt->ibuf[4], devExt->ibuf[5]));
		else
			*/
		if(devExt->packet_type == PACKET_V4_STATUS)
			DebugPrint(("[PS/2] status: f %02hhx - fingers %d", (devExt->ibuf[1] & 0x1f), hand->num_fingers_down));
		else if(devExt->packet_type == PACKET_V4_HEAD)
		{
			/*
			DebugPrint(("[PS/2]   head: %02hhx %02hhx %02hhx %02hhx %02hhx %02hhx",
				devExt->ibuf[0],
				devExt->ibuf[1],
				devExt->ibuf[2],
				devExt->ibuf[3],
				devExt->ibuf[4],
				devExt->ibuf[5]));
			
			DebugPrint(("[PS/2]   head: %4d %4d  %4d %4d  %4d %4d  %4d %4d  %4d %4d",
				hand->fingers[0].x, hand->fingers[0].y,
				hand->fingers[1].x, hand->fingers[1].y,
				hand->fingers[2].x, hand->fingers[2].y,
				hand->fingers[3].x, hand->fingers[3].y,
				hand->fingers[4].x, hand->fingers[4].y));
			*/
		}


		DebugPrint(("[PS/2] fingrs: %4d %4d  %4d %4d  %4d %4d  %4d %4d  %4d %4d",
			hand->fingers[0].x, hand->fingers[0].y,
			hand->fingers[1].x, hand->fingers[1].y,
			hand->fingers[2].x, hand->fingers[2].y,
			hand->fingers[3].x, hand->fingers[3].y,
			hand->fingers[4].x, hand->fingers[4].y));

		/*
		if(devExt->packet_type == PACKET_V4_HEAD)
		{
			if(devExt->hand.fingers[0].dx != 0 && devExt->hand.fingers[0].dy != 0)
				DebugPrint(("[PS/2]   head: %02hhx %02hhx %02hhx %02hhx %02hhx %02hhx - %4d (%2d) %4d (%2d)", devExt->ibuf[0], devExt->ibuf[1], devExt->ibuf[2], devExt->ibuf[3], devExt->ibuf[4], devExt->ibuf[5], devExt->hand.fingers[0].x, devExt->hand.fingers[0].dx, devExt->hand.fingers[0].y, devExt->hand.fingers[0].dy));
		}
		else 
		else if(devExt->packet_type == PACKET_V4_STATUS)
			DebugPrint(("[PS/2] status: %02hhx %02hhx %02hhx %02hhx %02hhx %02hhx", devExt->ibuf[0], devExt->ibuf[1], devExt->ibuf[2], devExt->ibuf[3], devExt->ibuf[4], devExt->ibuf[5]));
*/
		for(pCursor = InputDataStart; pCursor < InputDataEnd; pCursor++)
		{
			pCursor->ButtonFlags = 0;


			//clickpad translate to mouse click
			if(hand->clickpad_down_changed == TRUE)
			{
				hand->clickpad_down_changed = FALSE;

				pCursor->ButtonFlags = hand->clickpad_down ? MOUSE_LEFT_BUTTON_DOWN : MOUSE_LEFT_BUTTON_UP;

				//DebugPrint(("[PS/2] clickpad: %d", hand->clickpad_down));
			}

			//if there is only one finger, allow movement across the entire touchpad
			if(hand->num_fingers == 0)
			{
				//all fingers lifted off touchpad
			}
			if(hand->num_fingers_down == 1)
			{
				finger = &(hand->fingers[0]);
				

//				moment = finger->moment;
				

				if(finger->num_moments == 0)
				{
					dx = finger->moment->dx;
					dy = finger->moment->dy;
				}
				else
				{
					moment = finger->moment;
					m = 0;
					dx = 0;
					dy = 0;

					//smooth out samples
					while(m < (LONG)finger->num_moments && m < 4)
					{
						dx += moment->dx;
						dy += moment->dy;

						moment = (PFINGER_MOMENT) moment->prev;
						m++;
					}

					dx /= m;
					dy /= m;
				}
				
				if(finger->moment->pressure <= 16)
				{
					dx /= 8;
					dy /= 8;
				}
				if(finger->moment->pressure < 20)
				{
					dx /= 5;
					dy /= 5;
				}
				else if(finger->moment->pressure < 30)
				{
					dx /= 4;
					dy /= 4;
				}
				else if(finger->moment->pressure < 40)
				{
					dx /= 3;
					dy /= 3;
				}

				//after smoothing out, re-scale
				if(dx > 10)
					dx = dx * 5 / 3;

				if(dy > 10)
					dy = dy * 5 / 3;
				
				KeQuerySystemTime(&time_bytes_processed);
/*
				DebugPrint(("[PS/2 %6ld us] %d; press %4d - x:%4d dx:%4d vx:%6ld => %2d %2d", (time_bytes_processed - devExt->time_bytes_received), devExt->packet_type, finger->moment->pressure,
														finger->x, finger->dx, finger->vx,   dx, dy));
*/
				pCursor->Flags = MOUSE_MOVE_RELATIVE;
				pCursor->LastX = dx;
				pCursor->LastY = dy;
			}
			else
			{
				//detect two fingers scrolling in main area
				//

				//detect resting thumb
				//
			}



/*			else

			for(f = 0; f < ETP_MAX_FINGERS; f++)
			{
//				DebugPrint(("[PS/2] Finger %i down: %i, dxy: %d, %d", f, devExt->hand.fingers[f].down, devExt->hand.fingers[f].dx, devExt->hand.fingers[f].dy));

				//test for tap change status in the lower quarter
				if(devExt->hand.fingers[f].down_changed)
				{
					//reset the change status to mark it as processed
					devExt->hand.fingers[f].down_changed = FALSE;

					if(devExt->hand.fingers[f].y <= (devExt->hand.y_max * 1 / 4))
					{
						if(devExt->hand.fingers[f].x <= (devExt->hand.x_max / 2))
						{
							pCursor->ButtonFlags = devExt->hand.fingers[f].down ? MOUSE_LEFT_BUTTON_DOWN : MOUSE_LEFT_BUTTON_UP;

							//DebugPrint(("[PS/2] Finger %i left third: %i", f, devExt->hand.fingers[f].down));
							if(devExt->hand.fingers[f].down)
								DebugPrint(("[PS/2] left click down", f));
							else
								DebugPrint(("[PS/2] left click up", f));
						}
						else if(devExt->hand.fingers[f].x > (devExt->hand.x_max / 2))
						{
							pCursor->ButtonFlags = devExt->hand.fingers[f].down ? MOUSE_RIGHT_BUTTON_DOWN : MOUSE_RIGHT_BUTTON_UP;

//							DebugPrint(("[PS/2] Finger %i right two thirds: %i", f, devExt->hand.fingers[f].down));

							if(devExt->hand.fingers[f].down)
								DebugPrint(("[PS/2] right click down", f));
							else
								DebugPrint(("[PS/2] right click up", f));
						}
						else
						{
							pCursor->Flags = MOUSE_MOVE_RELATIVE;

							pCursor->LastX = 0;
							pCursor->LastY = 0;
						}
					}
				}
				else
				{
					if(devExt->hand.fingers[f].down)
					{
//						DebugPrint(("[PS/2] f[%d]: %li (%i), %li (%i)\n", f, devExt->hand.fingers[f].x, devExt->hand.fingers[f].dx, devExt->hand.fingers[f].y, devExt->hand.fingers[f].dy));

						//test for movement in the upper 3/4
						if(devExt->hand.fingers[f].y > (devExt->hand.y_max * 1 / 4))
						{
							pCursor->Flags = MOUSE_MOVE_RELATIVE;

							pCursor->LastX = devExt->hand.fingers[f].dx;
							pCursor->LastY = devExt->hand.fingers[f].dy;
						}
					}
				}
			}
*/			
		}
	}


    // Here we stop playing with the data!
    // UpperConnectData must be called at DISPATCH
    //
    (*(PSERVICE_CALLBACK_ROUTINE) devExt->UpperConnectData.ClassService)(
        devExt->UpperConnectData.ClassDeviceObject,
        InputDataStart,
        InputDataEnd,
        InputDataConsumed
        );
} 



#pragma warning(pop)
