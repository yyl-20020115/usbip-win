#pragma once

#include <usbdi.h>

typedef PUSB_CONFIGURATION_DESCRIPTOR	devconf_t;

extern void
show_pipe(unsigned int num, PUSBD_PIPE_INFORMATION pipe);

extern void
set_pipe(PUSBD_PIPE_INFORMATION pipe, PUSB_ENDPOINT_DESCRIPTOR ep_desc, unsigned char speed);

extern devconf_t
alloc_devconf_from_urb(struct _URB_CONTROL_DESCRIPTOR_REQUEST *urb_desc);

extern NTSTATUS
select_devconf(struct _URB_SELECT_CONFIGURATION *urb_selc, devconf_t devconf, UCHAR speed);

extern NTSTATUS
select_interface(struct _URB_SELECT_INTERFACE *urb_seli, devconf_t devconf, UCHAR speed);

PUSB_COMMON_DESCRIPTOR
find_usbconf_desc(devconf_t devconf, unsigned int *poffset, unsigned char type);

PUSB_INTERFACE_DESCRIPTOR
find_intf_desc(devconf_t devconf, unsigned int *poffset, unsigned int num, unsigned int alternatesetting);
