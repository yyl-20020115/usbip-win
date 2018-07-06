#include "pdu.h"

#include <usb.h>
#include <usbdi.h>

#define EPIPE		32
#define EOVERFLOW	75
#define EREMOTEIO	121

USBD_STATUS
to_usbd_status(int usbip_status)
{
	switch (usbip_status) {
	case 0:
		return USBD_STATUS_SUCCESS;
		/* I guess it */
	case -EPIPE:
		return USBD_STATUS_ENDPOINT_HALTED;
	case -EOVERFLOW:
		return USBD_STATUS_DATA_OVERRUN;
	case -EREMOTEIO:
		return USBD_STATUS_ERROR_SHORT_TRANSFER;
	default:
		return USBD_STATUS_ERROR;
	}
}

int
to_usbip_status(USBD_STATUS status)
{
	switch (status) {
	case 0:
		return 0;
	case USBD_STATUS_STALL_PID:
		return -EPIPE;
	default:
		return -1;
	}
}