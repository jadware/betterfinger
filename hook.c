#include <ntddk.h>
#include <kbdmou.h>
#include <ntddmou.h>
#include <ntdd8042.h>
#include <ntstrsafe.h>

#include "elantechwin.h"
#include "moufiltr.h"
#include "hook.h"


BOOLEAN
MouFilter_IsrHook (
    PVOID         DeviceExtension, 
    PMOUSE_INPUT_DATA       CurrentInput, 
    POUTPUT_PACKET          CurrentOutput,
    UCHAR                   StatusByte,
    PUCHAR                  DataByte,
    PBOOLEAN                ContinueProcessing,
    PMOUSE_STATE            MouseState,
    PMOUSE_RESET_SUBSTATE   ResetSubState
)
/*++

Remarks:
    i8042prt specific code, if you are writing a packet only filter driver, you
    can remove this function

Arguments:

    DeviceExtension - Our context passed during IOCTL_INTERNAL_I8042_HOOK_MOUSE
    
    CurrentInput - Current input packet being formulated by processing all the
                    interrupts

    CurrentOutput - Current list of bytes being written to the mouse or the
                    i8042 port.
                    
    StatusByte    - Byte read from I/O port 60 when the interrupt occurred                                            
    
    DataByte      - Byte read from I/O port 64 when the interrupt occurred. 
                    This byte can be modified and i8042prt will use this byte
                    if ContinueProcessing is TRUE

    ContinueProcessing - If TRUE, i8042prt will proceed with normal processing of
                         the interrupt.  If FALSE, i8042prt will return from the
                         interrupt after this function returns.  Also, if FALSE,
                         it is this functions responsibilityt to report the input
                         packet via the function provided in the hook IOCTL or via
                         queueing a DPC within this driver and calling the
                         service callback function acquired from the connect IOCTL
                                             
Return byte:

    Status is returned.



can do isrwrite from this function

  --+*/
{
    PDEVICE_EXTENSION   pde;
    BOOLEAN             retVal = TRUE;
	unsigned char byte;
	/*
	UNREFERENCED_PARAMETER(ResetSubState);
	UNREFERENCED_PARAMETER(MouseState);
	UNREFERENCED_PARAMETER(StatusByte);
	UNREFERENCED_PARAMETER(CurrentOutput);
	UNREFERENCED_PARAMETER(CurrentInput);
	*/


    pde = (PDEVICE_EXTENSION)DeviceExtension;
	byte = *DataByte;
	/*
	if(byte == 0xFA)
		DebugPrint(("[PS/2] JMOUSE [%3d] <- ACK", pde->state));
	else if(byte == 0xFE)
		DebugPrint(("[PS/2] JMOUSE [%3d] <- NAK", pde->state));
	else
		DebugPrint(("[PS/2] JMOUSE [%3d] <- %02hhx", pde->state, byte));
		*/
//	LogByte(RECEIVE, byte);


begin_state_machine:;
	switch ((ULONG)pde->state)
	{
		case WaitingForTrigger:
			if(pde->buf_size == 3)
			{
				pde->buf_size = 0;
			}
			else
			{
				pde->ibuf[pde->buf_size++] = byte;
			}
			break;

		case ResetMouse:
			pde->state = ExpectingReset;
			WRITE_BYTE(pde, 0xFF);
			break;

		case ExpectingReset:
			if (MOUSE_ACK == byte)
			{
				//drop the ack
				break; //return TRUE;
			}

			/* First, 0xFF is sent. The mouse is supposed to say AA00 if ok, FC00 if not. */
			if (0xAA == byte)
			{
				pde->state = ExpectingResetId;
			}
			else
			{
				/*
				Portpde->Flags &= ~MOUSE_PRESENT;
				pde->MouseState = MouseIdle;
				*/
				DebugPrint(("[PS/2] Mouse returned bad reset reply: %x (expected aa)\n", byte));
			}
			break; //return TRUE;

		case ExpectingResetId:
			if (MOUSE_ACK == byte)
			{
				DebugPrint(("[PS/2] Dropping extra ACK #2\n"));
				break; //return TRUE;
			}

			if (0x00 == byte) //this is the mouse ID
			{
//skip right to the magic knock, eh?
pde->state = SendMagicKnock;
goto begin_state_machine;
//				pde->state++;
				//pde->MouseType = GenericPS2;

//				WRITE_BYTE(pde, 0xF2); //should this be uppercontext?

				//it's a regular mouse, so just call the normal function?
			}
			else
			{
				/*
				Portpde->Flags &= ~MOUSE_PRESENT;
				pde->MouseState = MouseIdle;
				*/
				DebugPrint(("[PS/2] Mouse returned bad reset reply part two: %x (expected 0)\n", byte));
			}
			break; //return TRUE;

		case ExpectingGetDeviceIdACK:
			if (MOUSE_ACK == byte)
			{
				pde->state++;
			}
			else if (MOUSE_NACK == byte || MOUSE_ERROR == byte)
			{
				pde->state++;
				/* Act as if 00 (normal mouse) was received */
				DebugPrint(("[PS/2] Mouse doesn't support 0xd2, (returns %x, expected %x), faking\n", byte, MOUSE_ACK));
//				i8042MouResetIsr(DeviceExtension, Status, 0);
				break;
			}
			break; //return TRUE;

		case ExpectingGetDeviceIdValue:
			switch (byte)
			{
				case 0x02:
					//pde->MouseAttributes.MouseIdentifier = BALLPOINT_I8042_HARDWARE;
					break;

				case 0x03:
				case 0x04:
					//pde->MouseAttributes.MouseIdentifier = WHEELMOUSE_I8042_HARDWARE;
					break;

				default:
					//pde->MouseAttributes.MouseIdentifier = MOUSE_I8042_HARDWARE;
					break;
			}

//			pde->state++;
//			WRITE_BYTE(pde, PSMOUSE_CMD_SETRES);
			break; //return TRUE;

		case ReEnable:
			pde->state = ExpectingEnableACK;
			WRITE_BYTE(pde, PSMOUSE_CMD_ENABLE); //re-enable
			break;

		case ExpectingEnableACK:
			DebugPrint(("[PS/2] Streaming re-enabled"));
//			DebugPrint(("[PS/2] Response to enable was %02hxx", byte));
			/*
			PortDeviceExtension->Flags |= MOUSE_PRESENT;
			pde->MouseState = MouseIdle;
			pde->MouseTimeoutState = TimeoutCancel;
			*/

//			DebugPrint(("[PS/2] Mouse type = %u\n", pde->MouseType));

			//set into normal processing mode
			pde->state = ReceivingPackets4;
			break;


		case SendMagicKnock: //hijack the ISR instead of doing a switch statement
			switch(pde->state_step++)
			{
				case 0:
					WRITE_BYTE(pde, PSMOUSE_CMD_DISABLE);
					break;

				case 1:
					WRITE_BYTE(pde, PSMOUSE_CMD_SETSCALE11);
					break;

				case 2:
					WRITE_BYTE(pde, PSMOUSE_CMD_SETSCALE11);
					break;

				case 3:
					WRITE_BYTE(pde, PSMOUSE_CMD_SETSCALE11);
					break;

				case 4:
					WRITE_BYTE(pde, PSMOUSE_CMD_GETINFO);
					break;

				case 5:
					//ACK the getinfo
					break;

				case 6:
					pde->magic_knock_response[0] = byte;
					break;

				case 7:
					pde->magic_knock_response[1] = byte;
					break;

				case 8:
					pde->magic_knock_response[2] = byte;

					DebugPrint(("[PS/2] Magic knock response: %02hhx %02hhx %02hhx",
						pde->magic_knock_response[0],
						pde->magic_knock_response[1],
						pde->magic_knock_response[2]));

					//reset the step counter
					pde->state_step = 0;

					//jump to the next state
					pde->state = SendExtendedCommand;
					pde->ps2ext_command = ETP_FW_VERSION_QUERY;
					pde->state_next = GotETPVersion; //the state we want to be in after the command

					goto begin_state_machine;
			}
			break;
			

		case SendExtendedCommand:
			switch(pde->state_step_extcommand++)
			{
				case 0:
					WRITE_BYTE(pde, PSMOUSE_CMD_SETSCALE11);
					break;

				case 1:
					WRITE_BYTE(pde, PSMOUSE_CMD_SETRES);
					break;

				case 2:
					WRITE_BYTE(pde, (pde->ps2ext_command >> 6) & 3);
					break;

				case 3:
					WRITE_BYTE(pde, PSMOUSE_CMD_SETRES);
					break;

				case 4:
					WRITE_BYTE(pde, (pde->ps2ext_command >> 4) & 3);
					break;

				case 5:
					WRITE_BYTE(pde, PSMOUSE_CMD_SETRES);
					break;

				case 6:
					WRITE_BYTE(pde, (pde->ps2ext_command >> 2) & 3);
					break;

				case 7:
					WRITE_BYTE(pde, PSMOUSE_CMD_SETRES);
					break;

				case 8:
					WRITE_BYTE(pde, (pde->ps2ext_command >> 0) & 3);
					break;

				case 9:
					WRITE_BYTE(pde, PSMOUSE_CMD_GETINFO);
					break;

				case 10:
					//we get an ACK
					break;

				case 11:
					pde->ps2ext_response[0] = byte;
					break;

				case 12:
					pde->ps2ext_response[1] = byte;
					break;

				case 13:
					pde->ps2ext_response[2] = byte;
/*
					DebugPrint(("[PS/2] PS/2 extended command %02hhx response: %02hhx %02hhx %02hhx",
						pde->ps2ext_command,
						pde->ps2ext_response[0],
						pde->ps2ext_response[1],
						pde->ps2ext_response[2]));
*/
					//reset the step counter
					pde->state_step_extcommand = 0;

					//jump to the next state
					pde->state = pde->state_next;
					goto begin_state_machine;
			}
			break;

		case GotETPVersion:
			DebugPrint(("[PS/2] ETP Version: %02hhx %02hhx %02hhx",
						pde->ps2ext_response[0],
						pde->ps2ext_response[1],
						pde->ps2ext_response[2]));

			pde->state = SendExtendedCommand;
			pde->ps2ext_command = ETP_CAPABILITIES_QUERY;
			pde->state_next = GotETPCapabilities;
			goto begin_state_machine;

		case GotETPCapabilities:
			DebugPrint(("[PS/2] ETP Capabilities: %02hhx %02hhx %02hhx",
						pde->ps2ext_response[0],
						pde->ps2ext_response[1],
						pde->ps2ext_response[2]));

			pde->etp_capabilities[0] = pde->ps2ext_response[0];
			pde->etp_capabilities[1] = pde->ps2ext_response[1];
			pde->etp_capabilities[2] = pde->ps2ext_response[2];

			pde->state = SetSamplingRate;
			goto begin_state_machine;

		case SetSamplingRate:
			switch(pde->state_step++)
			{
				case 0:
					WRITE_BYTE(pde, PSMOUSE_CMD_SETSAMPLINGRATE);
					break;

				case 1:
					WRITE_BYTE(pde, pde->SampleRate);
					break;

				case 2:
					//reset the step counter
					pde->state_step = 0;

					//jump to the next state
					pde->state = SetResolution;
					goto begin_state_machine;
			}
			break;

		case SetResolution:
			switch(pde->state_step++)
			{
				case 0:
					WRITE_BYTE(pde, PSMOUSE_CMD_SETRES);
					break;

				case 1:
					WRITE_BYTE(pde, 0x03); //8 count / mm
					break;

				case 2:
					//reset the step counter
					pde->state_step = 0;

					//jump to the next state
					pde->state = SetAbsoluteMode;
					goto begin_state_machine;
			}
			break;

		case WriteRegister1:
			switch(pde->state_step_register++)
			{
				case 0:
					DebugPrint(("[PS/2] Writing reg_%02hhx with 0x%02hhx", pde->register_address, pde->register_value));
					WRITE_BYTE(pde, ETP_REGISTER_WRITE);
					break;

				case 1:
					WRITE_BYTE(pde, pde->register_address);
					break;

				case 2:
					WRITE_BYTE(pde, pde->register_value);
					break;

				case 3:
					WRITE_BYTE(pde, PSMOUSE_CMD_SETSCALE11);
					break;

				case 4:
					DebugPrint(("[PS/2] reg_%02hhx <- 0x%02hhx", pde->register_address, pde->register_value));

					//reset the step counter
					pde->state_step_register = 0;

					//jump to the next state
					pde->state = pde->state_next;
					goto begin_state_machine;
			}
			break;

		case WriteRegister2:
			switch(pde->state_step_register++)
			{
				case 0:
//					DebugPrint(("[PS/2] writing2 reg_%02hhx with 0x%02hhx", pde->register_address, pde->register_value));
					WRITE_BYTE(pde, ETP_PS2_CUSTOM_COMMAND);
					break;

				case 1:
					WRITE_BYTE(pde, ETP_REGISTER_WRITE);
					break;

				case 2:
					WRITE_BYTE(pde, ETP_PS2_CUSTOM_COMMAND);
					break;

				case 3:
					WRITE_BYTE(pde, pde->register_address);
					break;

				case 4:
					WRITE_BYTE(pde, ETP_PS2_CUSTOM_COMMAND);
					break;

				case 5:
					WRITE_BYTE(pde, pde->register_value);
					break;

				case 6:
					WRITE_BYTE(pde, PSMOUSE_CMD_SETSCALE11);
					break;

				case 7:
					DebugPrint(("[PS/2] reg_%02hhx <- 0x%02hhx", pde->register_address, pde->register_value));

					//reset the step counter
					pde->state_step_register = 0;

					//jump to the next state
					pde->state = pde->state_next;
					goto begin_state_machine;
			}
			break;

		case WriteRegister4:
			switch(pde->state_step_register++)
			{
				case 0:
//					DebugPrint(("[PS/2] writing34 reg_%02hhx with 0x%02hhx", pde->register_address, pde->register_value));
					WRITE_BYTE(pde, ETP_PS2_CUSTOM_COMMAND);
					break;

				case 1:
					WRITE_BYTE(pde, ETP_REGISTER_READWRITE);
					break;

				case 2:
					WRITE_BYTE(pde, ETP_PS2_CUSTOM_COMMAND);
					break;

				case 3:
					WRITE_BYTE(pde, pde->register_address);
					break;

				case 4:
					WRITE_BYTE(pde, ETP_PS2_CUSTOM_COMMAND);
					break;

				case 5:
					WRITE_BYTE(pde, ETP_REGISTER_READWRITE);
					break;

				case 6:
					WRITE_BYTE(pde, ETP_PS2_CUSTOM_COMMAND);
					break;

				case 7:
					WRITE_BYTE(pde, pde->register_value);
					break;

				case 8:
					WRITE_BYTE(pde, PSMOUSE_CMD_SETSCALE11);
					break;

				case 9:
					DebugPrint(("[PS/2] reg_%02hhx <- 0x%02hhx", pde->register_address, pde->register_value));

					//reset the step counter
					pde->state_step_register = 0;

					//jump to the next state
					pde->state = pde->state_next;
					goto begin_state_machine;
			}
			break;

		case ReadRegister1:
			switch(pde->state_step_register++)
			{
				case 0:
					DebugPrint(("[PS/2] reading1 reg_%02hhx", pde->register_address));
					WRITE_BYTE(pde, ETP_REGISTER_READ);
					break;

				case 1:
					WRITE_BYTE(pde, pde->register_address);
					break;

				case 2:
					WRITE_BYTE(pde, PSMOUSE_CMD_GETINFO);
					break;

				case 3:
					//ACK
					break;

				case 4:
					pde->register_response[0] = byte;
					break;

				case 5:
					pde->register_response[1] = byte;
					break;

				case 6:
					pde->register_response[2] = byte;

					DebugPrint(("[PS/2] reg_%02hhx: 0x%02hhx", pde->register_address, pde->register_response[0]));

					//reset the step counter
					pde->state_step_register = 0;

					//jump to the next state
					pde->state = pde->state_next;
					goto begin_state_machine;
			}
			break;

		case ReadRegister2:
			switch(pde->state_step_register++)
			{
				case 0:
					DebugPrint(("[PS/2] reading2 reg_%02hhx", pde->register_address));
					WRITE_BYTE(pde, ETP_PS2_CUSTOM_COMMAND);
					break;

				case 1:
					WRITE_BYTE(pde, ETP_REGISTER_READ);
					break;

				case 2:
					WRITE_BYTE(pde, ETP_PS2_CUSTOM_COMMAND);
					break;

				case 3:
					WRITE_BYTE(pde, pde->register_address);
					break;
				
				case 4:
					WRITE_BYTE(pde, PSMOUSE_CMD_GETINFO);
					break;

				case 5:
					//ACK
					break;

				case 6:
					pde->register_response[0] = byte;
					break;

				case 7:
					pde->register_response[1] = byte;
					break;

				case 8:
					pde->register_response[2] = byte;

					DebugPrint(("[PS/2] reg_%02hhx: 0x%02hhx", pde->register_address, pde->register_response[0]));

					//reset the step counter
					pde->state_step_register = 0;

					//jump to the next state
					pde->state = pde->state_next;
					goto begin_state_machine;
			}
			break;

		case ReadRegister3:
		case ReadRegister4:
			switch(pde->state_step_register++)
			{
				case 0:
					DebugPrint(("[PS/2] reading2 reg_%02hhx", pde->register_address));
					WRITE_BYTE(pde, ETP_PS2_CUSTOM_COMMAND);
					break;

				case 1:
					WRITE_BYTE(pde, ETP_REGISTER_READWRITE);
					break;

				case 2:
					WRITE_BYTE(pde, ETP_PS2_CUSTOM_COMMAND);
					break;

				case 3:
					WRITE_BYTE(pde, pde->register_address);
					break;
				
				case 4:
					WRITE_BYTE(pde, PSMOUSE_CMD_GETINFO);
					break;

				case 5:
					//ACK
					break;

				case 6:
					pde->register_response[0] = byte;
					break;

				case 7:
					pde->register_response[1] = byte;
					break;

				case 8:
					pde->register_response[2] = byte;

					DebugPrint(("[PS/2] reg_%02hhx: 0x%02hhx", pde->register_address, pde->register_response[0]));

					//reset the step counter
					pde->state_step_register = 0;

					//jump to the next state
					pde->state = pde->state_next;
					goto begin_state_machine;
			}
			break;

		case SetAbsoluteMode:
			//this is hardware version 4 (clickpad)
			switch(pde->state_step++)
			{
				case 0:
					DebugPrint(("[PS/2] Setting Absolute Mode"));

					pde->state = WriteRegister4;
					pde->register_address = ETP_REGISTER_07;
					pde->register_value = 0x01;

					//return here for another step
					pde->state_next = SetAbsoluteMode;
					goto begin_state_machine;

				case 1:
					DebugPrint(("[PS/2] Absolute Mode Set"));

					//reset the step counter
					pde->state_step = 0;

					//jump to the next state
					pde->state = SetRange;
					goto begin_state_machine;
			}
			break;

		case SetRange:
			//this is hardware version 4 (clickpad)
			switch(pde->state_step++)
			{
				case 0:
					pde->state = SendExtendedCommand;
					pde->ps2ext_command = ETP_FW_ID_QUERY;
					pde->state_next = SetRange; //the state we want to be in after the command

					goto begin_state_machine;

				case 1:
					pde->hand.x_max = (0x0f & pde->ps2ext_response[0]) << 8 | pde->ps2ext_response[1];
					pde->hand.y_max = (0xf0 & pde->ps2ext_response[0]) << 4 | pde->ps2ext_response[2];

					pde->hand.traces = pde->etp_capabilities[1];
					if((pde->hand.traces < 2) || (pde->hand.traces > pde->hand.x_max))
					{
						DebugPrint(("[PS/2] Error while setting range: traces out of range"));
					}
					else
					{
					}

					pde->hand.width = pde->hand.x_max / (pde->hand.traces - 1);

					DebugPrint(("[PS/2] Set Range - x_max: %d, y_max: %d", pde->hand.x_max, pde->hand.y_max));


					//reset the step counter
					pde->state_step = 0;

					//jump to the next state
					pde->state = GetResolution;
					goto begin_state_machine;
			}
			break;

		case GetResolution:
			switch(pde->state_step++)
			{
				case 0:
					pde->state = SendExtendedCommand;
					pde->ps2ext_command = ETP_RESOLUTION_QUERY;
					pde->state_next = GetResolution; //return here
					goto begin_state_machine;

				case 1:
					pde->hand.x_res = elantech_convert_res(pde->ps2ext_response[1] & 0x0f);
					pde->hand.y_res = elantech_convert_res((pde->ps2ext_response[1] & 0xf0) >> 4);

					DebugPrint(("[PS/2] Get Resolution: %d x %d", pde->hand.x_res, pde->hand.y_res));


					//reset the step counter
					pde->state_step = 0;

					//now re-enable the stream
					pde->state = ReEnable;
					goto begin_state_machine;
			}
			break;

		case ReceivingPackets4:
			if(pde->buf_size == 0)
				KeQuerySystemTime(&pde->time_bytes_received);

			//store the byte into the buffer
			pde->ibuf[pde->buf_size++] = byte;

			//wait for 6 bytes
			if(pde->buf_size < 6)
				break;

			//reset the buffer size
			pde->buf_size = 0;


			//determine the packet type
			pde->packet_type = elantech_packet_check_v4(pde->ibuf);

			//inline the report_absolute_v4
			if(pde->packet_type == PACKET_UNKNOWN)
			{
				//do something
				break;
			}

			switch(pde->packet_type)
			{
				case PACKET_V4_STATUS:
					process_packet_status_v4(&(pde->hand), pde->ibuf);
//					DebugPrint(("[PS/2] pkt status"));
					break;

				case PACKET_V4_HEAD:
					process_packet_head_v4(&(pde->hand), pde->ibuf);
//					DebugPrint(("[PS/2] pkt head"));
					break;

				case PACKET_V4_MOTION:
					process_packet_motion_v4(&(pde->hand), pde->ibuf);
//					DebugPrint(("[PS/2] pkt motion"));
					break;

				case PACKET_UNKNOWN:
					DebugPrint(("[PS/2] pkt unknown"));
					break;
			}

			//flag us for a packet push
			(*pde->QueueMousePacket)(pde->CallContext);
			break;


		default:
			if (pde->state < 100 || pde->state > 999)
				DebugPrint(("[PS/2] MouseResetState went out of range: %lu\n", pde->state));
			return FALSE;
	}

	
    
	//pass the hook 

    if(pde->UpperIsrHook)
	{
        retVal = (*pde->UpperIsrHook) (pde->UpperContext,
                            CurrentInput,
                            CurrentOutput,
                            StatusByte,
                            DataByte,
                            ContinueProcessing,
                            MouseState,
                            ResetSubState
            );

        if (!retVal || !(*ContinueProcessing))
		{
            return retVal;
        }
    }

	if(pde->state == WaitingForTrigger)
		*ContinueProcessing = TRUE;
	else
		*ContinueProcessing = FALSE;
		
    return retVal;
}

