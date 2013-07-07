
#include "interface.h"
#include "interface_internal.h"

#include <assert.h>
#include <event2/event.h>

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <time.h>

#include <sys/stat.h>


void intf_debug_print_status(int status)
{
  if(status & 0x01) fprintf(stderr, "Chip-send-buffer full.\n");
  if(status & 0x02) fprintf(stderr, "Chip-receive buffer overrun.\n");
  if(status & 0x04) fprintf(stderr, "Bus warning.\n");
  if(status & 0x08) fprintf(stderr, "Bus passive.\n");
  if(status & 0x10) fprintf(stderr, "Bus off.\n");
  if(status & 0x20) fprintf(stderr, "Receive buffer is empty.\n");
  if(status & 0x40) fprintf(stderr, "Receive buffer overrun.\n");
  if(status & 0x80) fprintf(stderr, "Send-buffer is full.\n");
}


void intf_clear_sdo_callbacks(intf_t *intf)
{
	intf->sdo_callback_data = NULL;
	intf->read_callback = NULL;
	intf->write_callback = NULL;
	intf->abort_callback = NULL;
}


/**
 * Setup CAN Interface
 */
intf_t *intf_create(event_base *ev_base)
{
	assert(ev_base);

	intf_t *intf = new intf_t();

	intf->ev_base = ev_base;
	intf->handle = 0;
	intf->fd = 0;

	intf->read_event = NULL;

	intf->payload = NULL;

	// Initialize callback functions
	intf->nmt_state_handler = NULL;
	intf->tpdo_handler = NULL;
	intf->close_handler = NULL;

	intf_clear_sdo_callbacks(intf);

  return intf;
}


/**
 * Destroy CAN Interface
 */
void intf_destroy(intf_t **intf)
{
	intf_close(*intf);
	free(*intf);
	*intf = NULL;
}


/**
 * Open connection to device
 */
int intf_open(intf_t *intf)
{
	assert(intf);

	#ifdef WIN32
	return -1;
	#else
	const char *device = "/dev/pcanusb0";

	// Device is already open
	if(intf->handle) {
		return 0;
	}

	// Check whether the device exists
	struct stat buf;
	if(stat(device, &buf) == -1)
	{
		fprintf(stderr, "Error locating device node (%s)\n", device);
		return -1;
	}

	// Device should be a character device
	if(!S_ISCHR(buf.st_mode))
	{
		fprintf(stderr, "Device node is not a character device (%s)\n", device);
		return -1;
	}

	// Try to open interface
	intf->handle = LINUX_CAN_Open(device, O_RDWR);

	if(!intf->handle) {
		fprintf(stderr, "Opening of CAN device failed\n");
		return -1;
	}

	// Initialize interface
	DWORD result = CAN_Init(intf->handle, CAN_BAUD_1M, CAN_INIT_TYPE_ST);

	if(result != CAN_ERR_OK && result != CAN_ERR_QRCVEMPTY)
	{
		fprintf(stderr, "Initializing of CAN device failed\n");
		intf_close(intf);
		return -1;
	}

	// Register handle with libevent
	intf->fd = LINUX_CAN_FileHandle(intf->handle);
	intf->read_event = event_new(intf->ev_base, intf->fd, EV_READ | EV_PERSIST, intf_on_read, (void *) intf);
	event_add(intf->read_event, NULL);

	return 0;
#endif
}


int intf_is_open(intf_t *intf)
{
	assert(intf);

	if(intf->handle)
		return 1;
	else
		return 0;
}


/**
 * Close the device connection.
 */
int intf_close(intf_t *intf)
{
	// Remove event
	if(intf->read_event) {
		event_del(intf->read_event);
		event_free(intf->read_event);
		intf->read_event = NULL;
	}

	#ifdef WIN32
	#else
	// Close connection
	if(intf->handle) {
		DWORD result = CAN_Close(intf->handle);
		if(result != CAN_ERR_OK) {
			fprintf(stderr, "Closing of CAN device failed\n");
			return -1;
		}

		intf->handle = 0;
		intf->fd = 0;
	}
	#endif

	if(intf->close_handler) {
		intf->close_handler(intf, intf->payload);
	}

	return 0;
}


/**
 * Writes a single message
 */
int intf_write(intf_t *intf, can_message_t msg)
{
	assert(intf);

	TPCANMsg cmsg;
	cmsg.ID = msg.id;
	cmsg.MSGTYPE = msg.type;
	cmsg.LEN = msg.len;

	for(int i = 0; i < 8; i++)
		cmsg.DATA[i] = msg.data[i];

	printf("Writing %04x %02x %02x (%02x %02x %02x %02x %02x %02x %02x %02x)\n", cmsg.ID, cmsg.MSGTYPE, cmsg.LEN, cmsg.DATA[0], cmsg.DATA[1], cmsg.DATA[2], cmsg.DATA[3], cmsg.DATA[4], cmsg.DATA[5], cmsg.DATA[6], cmsg.DATA[7]);

	#ifdef WIN32
	DWORD result = -1;
	#else
	DWORD result = CAN_Write(intf->handle, &cmsg);
	#endif

	if(result == -1) {
		fprintf(stderr, "Error while sending message: %s\n", strerror(errno));
		return -1;
	}

	return 0;
}


/**
 * Sends an NMT command
 */
int intf_send_nmt_command(intf_t *intf, uint8_t command)
{
	assert(intf);

	can_message_t msg;
	msg.id = 0;
	msg.type = mt_standard;
	msg.len = 2;
	msg.data[0] = command;
	msg.data[1] = 1;

	for(int i = 2; i < 8; i++)
		msg.data[i] = 0;

	return intf_write(intf, msg);
}


/**
 * Send read request.
 */
int intf_send_read_req(intf_t *intf, uint16_t index, uint8_t subindex,
	intf_read_callback_t read_callback, intf_abort_callback_t abort_callback, void *data)
{
	assert(intf);

	can_message_t msg;
	msg.id = (0x0C << 7) + 1;
	msg.type = mt_standard;

	msg.len = 8;
	msg.data[0] = 0x40;
	msg.data[1] = (index & 0x00FF);
	msg.data[2] = (index & 0xFF00) >> 8;
	msg.data[3] = subindex;
	msg.data[4] = 0;
	msg.data[5] = 0;
	msg.data[6] = 0;
	msg.data[7] = 0;

	intf->read_callback = read_callback;
	intf->write_callback = NULL;
	intf->abort_callback = abort_callback;
	intf->sdo_callback_data = data;

	return intf_write(intf, msg);
}

/**
 * Send write request.
 */
int intf_send_write_req(intf_t *intf, uint16_t index, uint8_t subindex, uint32_t value, uint8_t size,
	intf_write_callback_t write_callback, intf_abort_callback_t abort_callback, void *data)
{
	assert(intf);

	can_message_t msg;
	msg.id = (0x0C << 7) + 1;
	msg.type = mt_standard;

	msg.len = 8;
	msg.data[0] = 0x2F - ((size - 1) * 4);
	msg.data[1] = (index & 0x00FF);
	msg.data[2] = (index & 0xFF00) >> 8;
	msg.data[3] = subindex;
	msg.data[4] = (value & 0x000000FF);
	msg.data[5] = (value & 0x0000FF00) >> 8;
	msg.data[6] = (value & 0x00FF0000) >> 16;
	msg.data[7] = (value & 0xFF000000) >> 24;

	intf->read_callback = NULL;
	intf->write_callback = write_callback;
	intf->abort_callback = abort_callback;
	intf->sdo_callback_data = data;

  return intf_write(intf, msg);
}


/**
 * Parses CAN message and invokes callbacks.
 */
void intf_dispatch_msg(intf_t *intf, can_message_t msg)
{
	assert(intf);
	int function = msg.id >> 7;

	if(function == 0x01) {
		// Emergency messages are currently not handled.
	}

	// TPDOs
	if(function == 0x03 || function == 0x05 || function == 0x07 || function == 0x09) {
		if(intf->tpdo_handler)
			intf->tpdo_handler(intf, intf->payload, (function-1)/2, msg.data);
	}

	// Node guard message
	if(function == 0x0E && intf->nmt_state_handler) {
		uint8_t state = msg.data[0] & 0x7F;
		intf->nmt_state_handler(intf, intf->payload, state);
	}

	// SDO response
	if(function == 0x0B) {
		uint16_t index = msg.data[1] + (msg.data[2] << 8);
		uint8_t subindex = msg.data[3];
		uint32_t value;

		switch(msg.data[0]) {
			case 0x80:
			case 0x43:
				value = msg.data[4] + (msg.data[5] << 8) + (msg.data[6] << 16) + (msg.data[7] << 24);
				break;
			case 0x47: value = msg.data[4] + (msg.data[5] << 8) + (msg.data[6] << 16); break;
			case 0x4B: value = msg.data[4] + (msg.data[5] << 8); break;
			case 0x4F: value = msg.data[4]; break;
		}

		// Write response
		if(msg.data[0] == 0x60) {
			if(intf->write_callback) {
				intf->write_callback(intf->sdo_callback_data, index, subindex);
			} else {
				fprintf(stderr, "Received write response, but no callback function has been set.\n");
			}
		}

		// Read response
		if(msg.data[0] == 0x43 || msg.data[0] == 0x47 || msg.data[0] == 0x4B || msg.data[0] == 0x4F) {
			if(intf->read_callback) {
				intf->read_callback(intf->sdo_callback_data, index, subindex, value);
			} else {
				fprintf(stderr, "Received read response, but no callback function has been set.\n");
			}
		}

		// Abort response
		if(msg.data[0] == 0x80) {
			if(intf->abort_callback) {
				intf->abort_callback(intf->sdo_callback_data, index, subindex, value);
			} else {
				fprintf(stderr, "Received abort response, but no callback function has been set.\n");
			}
		}
	}
}


/**
 * Called when data is pending
 */
void intf_on_read(evutil_socket_t fd, short events, void *intf_v)
{
	#ifdef WIN32
	#else
	intf_t *intf = (intf_t *) intf_v;

	// We've been closed down, stop.
	if(!intf || !intf->fd)
		return;

	TPCANRdMsg message;
	DWORD result;

	result = LINUX_CAN_Read(intf->handle, &message);

	/*count++;
	if(count % 1000 == 0) {
		double delta = get_time() - start;
		printf("%d messages in %f seconds: %.0f Hz (%.4f sec/msg)\n", count, delta, double(count)/delta, delta/double(count));
	}*/

	// Receive queue was empty, no message read
	if(result == CAN_ERR_QRCVEMPTY) {
		fprintf(stderr, "CAN Queue was empty, but intf_on_read() was called.\n");
		return;
	}

	// There was an error
	if(result != CAN_ERR_OK) {
		fprintf(stderr, "Error while reading from CAN bus, closing down.\n");
		intf_close(intf);
		return;
	}

	// A status message was received
	if(message.Msg.MSGTYPE & MSGTYPE_STATUS == MSGTYPE_STATUS)
	{
		int32_t status = int32_t(CAN_Status(intf->handle));

		if(status < 0) {
			fprintf(stderr, "Received invalid status message (%x), closing down.\n", status);
			intf_close(intf);
			return;
		}

		if(! (status == 0x20 && status == 0x00)) {
			intf_debug_print_status(status);
			//intf_close(intf);
		} else {
			printf("Status: %x\n", status);
		}

		return;
	}

	uint64_t timestamp = message.dwTime * 1000 + message.wUsec;

	// Tell the world we've received a message?
	can_message_t msg;
	msg.id = message.Msg.ID;
	msg.type = message.Msg.MSGTYPE;
	msg.len = message.Msg.LEN;

	for(int i = 0; i < 8; i++)
		msg.data[i] = message.Msg.DATA[i];

	intf_dispatch_msg(intf, msg);

	return;
	#endif
}


void intf_set_callback_payload(intf_t *intf, void *payload)
{
	intf->payload = payload;
}


void intf_set_nmt_state_handler(intf_t *intf, intf_nmt_state_handler_t handler)
{
	intf->nmt_state_handler = handler;
}


void intf_set_tpdo_handler(intf_t *intf, intf_tpdo_handler_t handler)
{
	intf->tpdo_handler = handler;
}


void intf_set_close_handler(intf_t *intf, intf_close_handler_t handler)
{
  intf->close_handler = handler;
}
