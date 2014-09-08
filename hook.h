#ifndef HOOK_H
#define HOOK_H


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
);

//quick macro to log and write to the port
#define WRITE_BYTE(pde, byte) /*DebugPrint(("JMOUSE [%3d] -> %02hhx", pde->state, byte));*/ (*pde->IsrWritePort)(pde->CallContext, byte);


enum _MOUSE_STATE_EXTENDED
{
	_placeholder_ = CustomHookStateMinimum,
	WaitingForTrigger,
	ResetMouse,
	ReEnable,

	SendMagicKnock,
	SendExtendedCommand,

	ReadRegister1,
	ReadRegister2,
	ReadRegister3,
	ReadRegister4,

	WriteRegister1,
	WriteRegister2,
	WriteRegister3,
	WriteRegister4,

	SetAbsoluteMode,
	SetRange,
	GetResolution,
	SetResolution,
	SetSamplingRate,

	GotETPVersion,
	GotETPCapabilities,

	ReceivingPackets4
};

#endif