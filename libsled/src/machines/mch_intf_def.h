#include "machine_empty.h"

#define INITIAL_STATE ST_INTF_CLOSED

BEGIN_STATES
	STATE(ST_INTF_CLOSED)
	STATE(ST_INTF_OPENED)
	STATE(ST_INTF_OPENING)
	STATE(ST_INTF_CLOSING)
END_STATES

BEGIN_EVENTS
	EVENT(EV_INTF_OPEN)
	EVENT(EV_INTF_CLOSE)
	EVENT(EV_INTF_OPENED)
	EVENT(EV_INTF_CLOSED)
END_EVENTS

BEGIN_CALLBACKS
	CALLBACK(opened)
	CALLBACK(closed)
END_CALLBACKS

BEGIN_FIELDS
	FIELD(intf_t *, interface)
END_FIELDS

GENERATE_DEFAULT_FUNCTIONS
