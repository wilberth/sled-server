#include "machine_empty.h"

#define INITIAL_STATE ST_NET_DISABLED

BEGIN_EVENTS
	EVENT(EV_NET_INTF_OPENED)		// From interface machine (mch_intf.cc)
	EVENT(EV_NET_INTF_CLOSED)		// From interface machine (mch_intf.cc)

	EVENT(EV_NET_UPLOAD_COMPLETE)	// Internal
	EVENT(EV_NET_UPLOAD_FAILED)		// Internal

	EVENT(EV_NET_STOPPED)			// From CANOpen (interface.cc)
	EVENT(EV_NET_OPERATIONAL)		// From CANOpen (interface.cc)
	EVENT(EV_NET_PREOPERATIONAL)	// From CANOpen (interface.cc)

	EVENT(EV_NET_WATCHDOG_FAILED)	// From sled.cc
END_EVENTS

BEGIN_STATES
	STATE(ST_NET_DISABLED)

	STATE(ST_NET_UNKNOWN)
	STATE(ST_NET_STOPPED)
	STATE(ST_NET_PREOPERATIONAL)
	STATE(ST_NET_OPERATIONAL)

	STATE(ST_NET_ENTERPREOPERATIONAL)
	STATE(ST_NET_UPLOADCONFIG)
	STATE(ST_NET_STARTREMOTENODE)
END_STATES

BEGIN_FIELDS
	FIELD(intf_t *, interface)
	FIELD(mch_sdo_t *, mch_sdo)
END_FIELDS

BEGIN_CALLBACKS
	CALLBACK(sdos_enabled)
	CALLBACK(sdos_disabled)
	CALLBACK(enter_operational)
	CALLBACK(leave_operational)
END_CALLBACKS

GENERATE_DEFAULT_FUNCTIONS
