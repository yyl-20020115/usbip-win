#include "usbipd.h"

#include "usbip_network.h"
#include "usbip_host.h"

extern struct usbip_exported_device *find_edev(const char *busid);

int
recv_request_import(SOCKET sockfd)
{
	struct op_import_request req;
	struct usbip_exported_device *edev;
	struct usbip_usb_device pdu_udev;
	BOOL	error = FALSE;
	int rc;

	memset(&req, 0, sizeof(req));

	rc = usbip_net_recv(sockfd, &req, sizeof(req));
	if (rc < 0) {
		dbg("usbip_net_recv failed: import request");
		return -1;
	}
	PACK_OP_IMPORT_REQUEST(0, &req);

	edev = find_edev(req.busid);
	if (edev != NULL) {
		/* should set TCP_NODELAY for usbip */
		usbip_net_set_nodelay(sockfd);

		/* export device needs a TCP/IP socket descriptor */
		rc = usbip_export_device(edev, sockfd);
		if (rc < 0)
			error = TRUE;
	} else {
		info("requested device not found: %s", req.busid);
		error = TRUE;
	}

	rc = usbip_net_send_op_common(sockfd, OP_REP_IMPORT, !error ? ST_OK : ST_NA);
	if (rc < 0) {
		dbg("usbip_net_send_op_common failed: %#0x", OP_REP_IMPORT);
		return -1;
	}

	if (error) {
		dbg("import request busid %s: failed", req.busid);
		return -1;
	}

	memcpy(&pdu_udev, &edev->udev, sizeof(pdu_udev));
	usbip_net_pack_usb_device(1, &pdu_udev);

	rc = usbip_net_send(sockfd, &pdu_udev, sizeof(pdu_udev));
	if (rc < 0) {
		dbg("usbip_net_send failed: devinfo");
		return -1;
	}

	dbg("import request busid %s: complete", req.busid);

	return 0;
}
