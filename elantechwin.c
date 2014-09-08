#include <ntddk.h>
#include <kbdmou.h>
#include <ntddmou.h>
#include <ntdd8042.h>
#include <ntstrsafe.h>

#include "elantechwin.h"


int elantech_packet_check_v4(MOUSE_PACKET_V4 packet)
{
	if ((packet[0] & 0x0c) == 0x04 &&
	    (packet[3] & 0x1f) == 0x11)
		return PACKET_V4_HEAD;

	if ((packet[0] & 0x0c) == 0x04 &&
	    (packet[3] & 0x1f) == 0x12)
		return PACKET_V4_MOTION;

	if ((packet[0] & 0x0c) == 0x04 &&
	    (packet[3] & 0x1f) == 0x10)
		return PACKET_V4_STATUS;

	return PACKET_UNKNOWN;
}

void process_packet_status_v4(PHAND_STATUS hand, MOUSE_PACKET_V4 packet)
{
	unsigned fingers_state;
	int f;
	BOOLEAN down;
	PFINGER_STATUS finger;

	//notify finger state change
	fingers_state = packet[1] & 0x1f;


	//check state of each finger
	for(f = 0; f < ETP_MAX_FINGERS; f++)
	{
		finger = &(hand->fingers[f]);

		//test finger touchdown state
		down = (fingers_state & (1 << f)) > 0;

		//detect if finger status changed
		if(finger->down != down)
		{
			//store finger status and mark that it has changed
			finger->down = down;
			finger->down_changed = TRUE;

			if(finger->down)
			{
				//finger pressed down

				//increment number of fingers down
				hand->num_fingers_down++;

				//mark the finger as needing x_start and y_start coordinates
				finger->need_start = TRUE;

				//store a timestamp for the touch down
				KeQuerySystemTime(&(finger->down_timestamp));
			}
			else
			{
				//finger released

				//decrement number of fingers down
				hand->num_fingers_down--;

				//releasing the touch, so definitely don't need a start coordinate
				finger->need_start = FALSE;

				finger->num_moments = 0;

				finger->x = 0;
				finger->y = 0;

				finger->dx = 0;
				finger->dy = 0;
			}
		}
	}
}

void process_packet_head_v4(PHAND_STATUS hand, MOUSE_PACKET_V4 packet)
{
	int f;
	int x, dx, y, dy;
	BOOLEAN clickpad_down;
	PFINGER_STATUS finger;
	PFINGER_MOMENT prev;

	unsigned int pressure = (packet[1] & 0xf0) | ((packet[4] & 0xf0) >> 4);
	int traces = (packet[0] & 0xf0) >> 4;


	f = ((packet[3] & 0xe0) >> 5) - 1; //what is this really?

	if (f < 0)
		return;


	//helper
	finger = &(hand->fingers[f]);

	//calculate new x and y positions
	x = ((packet[1] & 0x0f) << 8) | packet[2];
	y = ((packet[4] & 0x0f) << 8) | packet[5];


	//detect status of clickpad
	clickpad_down = packet[0] & 0x01;

	//if clickpad status has changed..
	if(hand->clickpad_down != clickpad_down)
	{
		hand->clickpad_down = clickpad_down;
		hand->clickpad_down_changed = TRUE;
		//KeQuerySystemTime(&(hand->clickpad_down_timestamp));
	}

	//if we need absolute start coordinates
	if(finger->need_start)
	{
		//flag as having assigned the start coordinates
		finger->need_start = FALSE;

		//set current position to start coords
		finger->x = x;
		finger->y = y;

		//set start coords
		finger->x_start = x;
		finger->y_start = y;

		//null out the dxdy
		dx = 0;
		dy = 0;
	}
	else
	{
		//calculate dx and dy from previous x and y position
		dx = x - finger->x;
		dy = -1 * (y - finger->y);

		//set absolute coordinates
		finger->x = x;
		finger->y = y;
	}

	finger->dx = dx;
	finger->dy = dy;

	

	finger->traces = traces;


	//push all the data into the current finger moment
	
	//advance the moment pointer
	finger->moment = finger->moment->next;

	//increment number of pointers, but not past the limit
	if(++(finger->num_moments) > MOMENT_BUFFER_SIZE)
		finger->num_moments = MOMENT_BUFFER_SIZE;

	finger->moment->x = x;
	finger->moment->y = y;

	finger->moment->dx = dx;
	finger->moment->dy = dy;

	finger->moment->pressure = pressure;

	//store the moment's timestamp
	KeQuerySystemTime(&(finger->moment->timestamp));

	prev = (PFINGER_MOMENT) finger->moment->prev;

	//calculate dt of the moment and store it
	if(prev->timestamp > 0)
		finger->moment->dt = prev->timestamp - finger->moment->timestamp;
}


void process_packet_motion_v4(PHAND_STATUS hand, MOUSE_PACKET_V4 packet)
{
	int delta_x1 = 0, delta_y1 = 0, delta_x2 = 0, delta_y2 = 0;
	int f, f2;
	int weight;
	BOOLEAN clickpad_down;
	long dx1, dy1, dx2, dy2;

	PFINGER_STATUS finger1, finger2;


	//determine which finger
	f = ((packet[0] & 0xe0) >> 5) - 1;

	if(f < 0)
		return;

	finger1 = &(hand->fingers[f]);



	weight = (packet[0] & 0x10) ? ETP_WEIGHT_VALUE : 1;


	//detect status of clickpad
	clickpad_down = packet[0] & 0x01;

	if(hand->clickpad_down != clickpad_down)
	{
		hand->clickpad_down = clickpad_down;
		hand->clickpad_down_changed = TRUE;
	}

	 //Motion packets give us the delta of x, y values of specific fingers,
	 //but in two's complement. Let the compiler do the conversion for us.
	 //Also _enlarge_ the numbers to int, in case of overflow.

	delta_x1 = (signed char) packet[1];
	delta_y1 = (signed char) packet[2];
	delta_x2 = (signed char) packet[4];
	delta_y2 = (signed char) packet[5];


	//manage finger 1

	//advance the moment pointer
	finger1->moment = finger1->moment->next;

	//increment number of pointers, but not past the limit
	if(++(finger1->num_moments) > MOMENT_BUFFER_SIZE)
		finger1->num_moments = MOMENT_BUFFER_SIZE;

	dx1 = delta_x1 * weight;
	dy1 = delta_y1 * weight;

	finger1->x += dx1;
	finger1->y += dy1;


	//store the xy coords
	finger1->moment->x = finger1->x;
	finger1->moment->y = finger1->y;

	finger1->moment->dx = dx1;
	finger1->moment->dy = dy1;

	//store the moment's timestamp
	KeQuerySystemTime(&(finger1->moment->timestamp));


	//manage finger 2
	f2 = ((packet[3] & 0xe0) >> 5) - 1;
	finger2 = &(hand->fingers[f2]);

	if(f2 >= 0)
	{
		//advance the moment pointer
		finger2->moment = finger2->moment->next;

		//increment number of pointers, but not past the limit
		if(++(finger2->num_moments) > MOMENT_BUFFER_SIZE)
			finger2->num_moments = MOMENT_BUFFER_SIZE;

		dx2 = delta_x2 * weight;
		dy2 = delta_y2 * weight;

		finger2->x += dx2;
		finger2->y += dy2;

		//store the moment
		finger2->moment->x = finger2->x;
		finger2->moment->y = finger2->y;

		finger2->moment->dx = dx2;
		finger2->moment->dy = dy2;

		//store the moment's timestamp
		KeQuerySystemTime(&(finger2->moment->timestamp));
	}
}


unsigned int elantech_convert_res(unsigned int val)
{
	return (val * 10 + 790) * 10 / 254;
}


//fast integer approximate distance calculator
UINT32 distance(INT32 dx, INT32 dy)
{
   UINT32 min, max;

   if ( dx < 0 ) dx = -dx;
   if ( dy < 0 ) dy = -dy;

   if ( dx < dy )
   {
      min = dx;
      max = dy;
   }
   else
   {
      min = dy;
      max = dx;
   }

   // coefficients equivalent to ( 123/128 * max ) and ( 51/128 * min )
   return ((( max << 8 ) + ( max << 3 ) - ( max << 4 ) - ( max << 1 ) +
            ( min << 7 ) - ( min << 5 ) + ( min << 3 ) - ( min << 1 )) >> 8 );
} 