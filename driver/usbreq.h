#pragma once

#include <ntddk.h>
#include <usbdi.h>

#include "device.h"

#define MAKE_PIPE(ep, type, interval) ((USBD_PIPE_HANDLE)((ep) | ((interval) << 8) | ((type) << 16)))

#define PIPE2DIRECT(handle)	(((INT_PTR)(handle) & 0x80) ? USBIP_DIR_IN : USBIP_DIR_OUT)
#define PIPE2ADDR(handle)	((unsigned char)((INT_PTR)(handle) & 0x7f))
#define PIPE2TYPE(handle)	((unsigned char)(((INT_PTR)(handle) & 0xff0000) >> 16))
#define PIPE2INTERVAL(handle)	((unsigned char)(((INT_PTR)(handle) & 0xff00) >> 8))

struct urb_req {
	PPDO_DEVICE_DATA	pdodata;
	PIRP	irp;
	KEVENT	*event;
	unsigned long	seq_num;
	BOOLEAN		sent;
	LIST_ENTRY	list;
};

struct usb_ctrl_setup {
	unsigned char	bRequestType;
	unsigned char	bRequest;
	unsigned short	wValue;
	unsigned short	wIndex;
	unsigned short	wLength;
};

extern void
show_pipe(unsigned int num, PUSBD_PIPE_INFORMATION pipe);

extern void
set_pipe(PUSBD_PIPE_INFORMATION pipe, PUSB_ENDPOINT_DESCRIPTOR ep_desc, unsigned char speed);

extern void
build_setup_packet(struct usb_ctrl_setup *setup,
	unsigned char direct_in, unsigned char type, unsigned char recip, unsigned char request);

extern void *
seek_to_one_intf_desc(PUSB_CONFIGURATION_DESCRIPTOR config, unsigned int *offset, unsigned int num, unsigned int alternatesetting);

extern void *
seek_to_next_desc(PUSB_CONFIGURATION_DESCRIPTOR config, unsigned int *offset, unsigned char type);
