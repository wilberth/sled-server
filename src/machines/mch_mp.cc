
#include <stdlib.h>
#include <stdio.h>

#include "../interface.h"
#include "mch_mp.h"


struct mch_mp_t {
	mch_mp_state_t state;
	intf_t *interface;
};


/**
 * Creates a state machine that keeps track of profile position mode.
 */
mch_mp_t *mch_mp_create(intf_t *interface)
{
	mch_mp_t *machine = new mch_mp_t();

	machine->interface = interface;
	machine->state = ST_MP_DISABLED;

	return machine;
}


/**
 * Destroys the PP state machine.
 */
void mch_mp_destroy(mch_mp_t **machine)
{
	free(*machine);
	*machine = NULL;
}


mch_mp_state_t mch_mp_active_state(mch_mp_t *machine)
{
	return machine->state;
}


mch_mp_state_t mch_mp_next_state_given_event(mch_mp_t *machine, mch_mp_event_t event)
{
	if(!machine->state == ST_MP_DISABLED && event == EV_MP_DS_INOPERATIONAL)
		return ST_MP_DISABLED;

	switch(machine->state) {
		case ST_MP_DISABLED:
			if(event == EV_MP_DS_OPERATIONAL)
				return ST_MP_UNKNOWN;
			break;

		case ST_MP_UNKNOWN:
			if(event == EV_MP_HOMED)
				return ST_MP_SWITCH_MODE_PP;
			if(event == EV_MP_NOTHOMED)
				return ST_MP_SWITCH_MODE_HOMING;
			break;

		case ST_MP_SWITCH_MODE_HOMING:
			if(event == EV_MP_MODE_HOMING)
				return ST_MP_HOMING;
			break;

		case ST_MP_SWITCH_MODE_PP:
			if(event == EV_MP_MODE_PP)
				return ST_MP_PP_IDLE;
			break;

		case ST_MP_HOMING:
			if(event == EV_MP_HOMED)
				return ST_MP_SWITCH_MODE_PP;
			break;
	}

	return machine->state;
}


void mch_mp_send_mode_switch(mch_mp_t *machine, uint8_t mode)
{
	intf_send_write_req(machine->interface, 0x6060, 0x00, mode, 0x01);
}


void mch_mp_send_control_word(mch_mp_t *machine, uint16_t control_word)
{
	intf_send_write_req(machine->interface, 0x6040, 0x00, control_word, 0x02);
}


void mch_mp_on_enter(mch_mp_t *machine)
{
	switch(machine->state) {
		case ST_MP_UNKNOWN:
			break;

		case ST_MP_SWITCH_MODE_HOMING:
			mch_mp_send_mode_switch(machine, 0x06);
			break;

		case ST_MP_SWITCH_MODE_PP:
			mch_mp_send_mode_switch(machine, 0x08);
			break;

		case ST_MP_HOMING:
			mch_mp_send_control_word(machine, 0x1F);
			break;

	}
}


void mch_mp_on_exit(mch_mp_t *machine)
{
	switch(machine->state) {
		case ST_MP_HOMING:
			mch_mp_send_control_word(machine, 0x0F);
			break;
	}
}


const char *mch_mp_statename(mch_mp_state_t state)
{
	switch(state) {
		case ST_MP_DISABLED: return "ST_DS_DISABLED";
		case ST_MP_UNKNOWN: return "ST_DS_UNKNOWN";
		case ST_MP_SWITCH_MODE_HOMING: return "ST_MP_SWITCH_MODE_HOMING";
		case ST_MP_HOMING: return "ST_MP_HOMING";

		case ST_MP_SWITCH_MODE_PP: return "ST_MP_SWITCH_MODE_PP";
		case ST_MP_PP_IDLE: return "ST_MP_PP_IDLE";
		case ST_MP_PP_MOVING: return "ST_MP_PP_MOVING";
	}

	return "Invalid state";
}


void mch_mp_handle_event(mch_mp_t *machine, mch_mp_event_t event)
{
	mch_mp_state_t next_state = mch_mp_next_state_given_event(machine, event);

	if(!(machine->state == next_state)) {
		mch_mp_on_exit(machine);
		machine->state = next_state;
		printf("MP machine changed state: %s\n", mch_mp_statename(machine->state));
		mch_mp_on_enter(machine);
	}
}

