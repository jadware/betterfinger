
#ifndef ELANTECHWIN_H
#define ELANTECHWIN_H





#define PSMOUSE_CMD_SETSCALE11		0xE6
#define PSMOUSE_CMD_SETSCALE21		0xE7
#define PSMOUSE_CMD_SETRES			0xE8
#define PSMOUSE_CMD_GETINFO			0xE9
#define PSMOUSE_CMD_SETSTREAM		0xEA
#define PSMOUSE_CMD_SETSAMPLINGRATE	0xF3
#define PSMOUSE_CMD_ENABLE			0xF4
#define PSMOUSE_CMD_DISABLE			0xF5


#define MOUSE_ACK          0xFA
#define MOUSE_ERROR        0xFC
#define MOUSE_NACK         0xFE


#define ETP_FW_ID_QUERY				0x00
#define ETP_FW_VERSION_QUERY		0x01
#define ETP_CAPABILITIES_QUERY		0x02
#define ETP_SAMPLE_QUERY			0x03
#define ETP_RESOLUTION_QUERY		0x04


#define ETP_REGISTER_07				0x07
#define ETP_REGISTER_10				0x10
#define ETP_REGISTER_11				0x11
#define ETP_REGISTER_20				0x20
#define ETP_REGISTER_21				0x21
#define ETP_REGISTER_22				0x22
#define ETP_REGISTER_23				0x23
#define ETP_REGISTER_24				0x24
#define ETP_REGISTER_25				0x25
#define ETP_REGISTER_26				0x26


/*
 * Command values for register reading or writing
 */
#define ETP_REGISTER_READ			0x10
#define ETP_REGISTER_WRITE			0x11
#define ETP_REGISTER_READWRITE		0x00

/*
 * Hardware version 2 custom PS/2 command value
 */
#define ETP_PS2_CUSTOM_COMMAND		0xf8


#define ETP_CAP_HAS_ROCKER			0x04


#define ETP_MAX_FINGERS				5
#define ETP_WEIGHT_VALUE			5

#define MOMENT_BUFFER_SIZE			32


enum _PACKET_TYPE
{
	PACKET_UNKNOWN,
	PACKET_DEBOUNCE,
	PACKET_V3_HEAD,
	PACKET_V3_TAIL,
	PACKET_V4_HEAD,
	PACKET_V4_MOTION,
	PACKET_V4_STATUS
} PACKET_TYPE;

typedef UCHAR MOUSE_PACKET_V4[6];



typedef struct _FINGER_MOMENT
{
	unsigned int id;

	unsigned int x, y;
	int dx, dy;
	int vx, vy;
	int ax, ay;

	unsigned int pressure;
	int weight;

	ULONG64 timestamp, dt;

	//make this a two-way circular list
	VOID *prev, *next;

} FINGER_MOMENT, *PFINGER_MOMENT;

typedef struct _FINGER_STATUS
{
	BOOLEAN down, down_changed;
	ULONG64 down_timestamp;

	unsigned int x_start, y_start;
	
	unsigned int x, y; //position
	int dx, dy; //change in position
	ULONG vx, vy; //velocity * 1000
	ULONG ax, ay; //acceleration * 1000
	

	BOOLEAN need_start;

	
	int traces;

	FINGER_MOMENT moment_history[MOMENT_BUFFER_SIZE];
	PFINGER_MOMENT moment;
	unsigned int num_moments;
} FINGER_STATUS, *PFINGER_STATUS;

typedef struct _HAND_STATUS
{
	//X and Y axis resolution
	unsigned int x_res, y_res;
	unsigned int x_max, y_max;

	unsigned int num_fingers_down;


	unsigned char traces;
	unsigned int width;

	BOOLEAN clickpad_down;
	BOOLEAN clickpad_down_changed;
	FINGER_STATUS fingers[5];

	PFINGER_STATUS single, thumb, scroll1, scroll2;

	int num_fingers;
} HAND_STATUS, *PHAND_STATUS;




int elantech_packet_check_v4(UCHAR packet[6]);
unsigned int elantech_convert_res(unsigned int val);


void process_packet_status_v4(PHAND_STATUS hand, MOUSE_PACKET_V4 packet);
void process_packet_head_v4(PHAND_STATUS hand, MOUSE_PACKET_V4 packet);
void process_packet_motion_v4(PHAND_STATUS hand, MOUSE_PACKET_V4 packet);

UINT32 distance(INT32 dx, INT32 dy);

#endif