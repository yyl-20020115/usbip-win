#pragma once

struct usbip_header_basic {
#define USBIP_CMD_SUBMIT	0x0001
#define USBIP_CMD_UNLINK	0x0002
#define USBIP_RET_SUBMIT	0x0003
#define USBIP_RET_UNLINK	0x0004
#define USBIP_RESET_DEV		0xFFFF
	unsigned int command;

	/* sequencial number which identifies requests.
	* incremented per connections */
	unsigned int seqnum;

	unsigned int devid;

#define USBIP_DIR_OUT	0
#define USBIP_DIR_IN	1
	unsigned int direction;
	unsigned int ep;     /* endpoint number */
};

/*
* An additional header for a CMD_SUBMIT packet.
*/
struct usbip_header_cmd_submit {
	/* these values are basically the same as in a URB. */

	/* the same in a URB. */
	unsigned int transfer_flags;

	/* set the following data size (out),
	* or expected reading data size (in) */
	int transfer_buffer_length;

	/* it is difficult for usbip to sync frames (reserved only?) */
	int start_frame;

	/* the number of iso descriptors that follows this header */
	int number_of_packets;

	/* the maximum time within which this request works in a host
	* controller of a server side */
	int interval;

	/* set setup packet data for a CTRL request */
	unsigned char setup[8];
};

/*
* An additional header for a RET_SUBMIT packet.
*/
struct usbip_header_ret_submit {
	int status;
	int actual_length; /* returned data length */
	int start_frame; /* ISO and INT */
	int number_of_packets;  /* ISO only */
	int error_count; /* ISO only */
};

/*
* An additional header for a CMD_UNLINK packet.
*/
struct usbip_header_cmd_unlink {
	unsigned int seqnum; /* URB's seqnum which will be unlinked */
};


/*
* An additional header for a RET_UNLINK packet.
*/
struct usbip_header_ret_unlink {
	int status;
};


/* the same as usb_iso_packet_descriptor but packed for pdu */
struct usbip_iso_packet_descriptor {
	unsigned int offset;
	unsigned int length;            /* expected length */
	unsigned int actual_length;
	unsigned int status;
};


/*
* All usbip packets use a common header to keep code simple.
*/
struct usbip_header {
	struct usbip_header_basic base;

	union {
		struct usbip_header_cmd_submit	cmd_submit;
		struct usbip_header_ret_submit	ret_submit;
		struct usbip_header_cmd_unlink	cmd_unlink;
		struct usbip_header_ret_unlink	ret_unlink;
	} u;
};
