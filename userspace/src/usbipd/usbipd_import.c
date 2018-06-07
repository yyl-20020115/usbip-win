#include "usbipd.h"

#include "usbip_network.h"
#include "usbipd_stub.h"
#include "usbip_setupdi.h"

int
recv_request_import(SOCKET sockfd)
{
	struct op_import_request req;
	struct usbip_usb_device	udev;
	devno_t	devno;
	int rc;

	memset(&req, 0, sizeof(req));

	rc = usbip_net_recv(sockfd, &req, sizeof(req));
	if (rc < 0) {
		dbg("usbip_net_recv failed: import request");
		return -1;
	}
	PACK_OP_IMPORT_REQUEST(0, &req);

	devno = get_devno_from_busid(req.busid);
	if (devno == 0) {
		err("invalid bus id: %s", req.busid);
		return -1;
	}

	/* should set TCP_NODELAY for usbip */
	usbip_net_set_nodelay(sockfd);

	/* export device needs a TCP/IP socket descriptor */
	rc = usbip_export_device(devno, sockfd);
	if (rc < 0) {
		err("failed to export device: %s, err:%d", req.busid, rc);
		usbip_net_send_op_common(sockfd, OP_REP_IMPORT, ST_NA);
		return -1;
	}

	rc = usbip_net_send_op_common(sockfd, OP_REP_IMPORT, ST_OK);
	if (rc < 0) {
		dbg("usbip_net_send_op_common failed: %#0x", OP_REP_IMPORT);
		return -1;
	}

	build_udev(devno, &udev);
	usbip_net_pack_usb_device(1, &udev);

	rc = usbip_net_send(sockfd, &udev, sizeof(udev));
	if (rc < 0) {
		dbg("usbip_net_send failed: devinfo");
		return -1;
	}

	dbg("import request busid %s: complete", req.busid);

	return 0;
}
