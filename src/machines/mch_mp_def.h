#include "machine_empty.h"

#define INITIAL_STATE ST_MP_DISABLED

BEGIN_STATES
	STATE(ST_MP_DISABLED)

	STATE(ST_MP_SWITCH_MODE_HOMING)
	STATE(ST_MP_HH_UNKNOWN)
	STATE(ST_MP_HH_HOMING)

	STATE(ST_MP_SWITCH_MODE_PP)
	STATE(ST_MP_PP_IDLE)
	STATE(ST_MP_PP_MOVING)
END_STATES

BEGIN_EVENTS
	EVENT(EV_MP_DS_OPERATIONAL)		// From DS machine (mch_ds.cc)
	EVENT(EV_MP_DS_INOPERATIONAL)	// From DS machine (mch_ds.cc)

	EVENT(EV_MP_MODE_HOMING)
	EVENT(EV_MP_MODE_PP)

	EVENT(EV_MP_HOMED)
	EVENT(EV_MP_NOTHOMED)
END_EVENTS

BEGIN_FIELDS
	FIELD(intf_t *, interface)
END_FIELDS

BEGIN_CALLBACKS
END_CALLBACKS

GENERATE_DEFAULT_FUNCTIONS
