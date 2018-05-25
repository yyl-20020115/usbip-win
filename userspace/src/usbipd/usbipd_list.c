#include "usbipd.h"

#include "usbip_network.h"
#include "usbip_host.h"

static LIST_HEAD(edev_list);
static int	n_edevs;

static int
send_reply_devlist_devices(SOCKET connfd)
{
	struct list_head	*p;

	list_for_each(p, &edev_list) {
		struct usbip_exported_device	*edev;
		struct usbip_usb_device			pdu_udev;
		int	rc, i;

		edev = list_entry(p, struct usbip_exported_device, node);
		dump_usb_device(&edev->udev);
		memcpy(&pdu_udev, &edev->udev, sizeof(pdu_udev));
		usbip_net_pack_usb_device(1, &pdu_udev);

		rc = usbip_net_send(connfd, &pdu_udev, sizeof(pdu_udev));
		if (rc < 0) {
			dbg("usbip_net_send failed: pdu_udev");
			return -1;
		}

		for (i = 0; i < edev->udev.bNumInterfaces; i++) {
			struct usbip_usb_interface		pdu_uinf;

			dump_usb_interface(&edev->uinf[i]);
			memcpy(&pdu_uinf, &edev->uinf[i], sizeof(pdu_uinf));
			usbip_net_pack_usb_interface(1, &pdu_uinf);

			rc = usbip_net_send(connfd, &pdu_uinf,
				sizeof(pdu_uinf));
			if (rc < 0) {
				err("usbip_net_send failed: pdu_uinf");
				return -1;
			}
		}
	}
	return 0;
}

static int
send_reply_devlist(SOCKET connfd)
{
	struct op_devlist_reply			reply;
	int	rc;

	info("exportable devices: %d", n_edevs);

	reply.ndev = n_edevs;

	rc = usbip_net_send_op_common(connfd, OP_REP_DEVLIST, ST_OK);
	if (rc < 0) {
		dbg("usbip_net_send_op_common failed: %#0x", OP_REP_DEVLIST);
		return -1;
	}
	PACK_OP_DEVLIST_REPLY(1, &reply);

	rc = usbip_net_send(connfd, &reply, sizeof(reply));
	if (rc < 0) {
		dbg("usbip_net_send failed: %#0x", OP_REP_DEVLIST);
		return -1;
	}

	if (send_reply_devlist_devices(connfd) < 0)
		return -1;

	return 0;
}

int
recv_request_devlist(SOCKET connfd)
{
	struct op_devlist_request	req;
	int	rc;

	memset(&req, 0, sizeof(req));

	rc = usbip_net_recv(connfd, &req, sizeof(req));
	if (rc < 0) {
		dbg("usbip_net_recv failed: devlist request");
		return -1;
	}

	rc = send_reply_devlist(connfd);
	if (rc < 0) {
		dbg("send_reply_devlist failed");
		return -1;
	}

	return 0;
}

struct usbip_exported_device *
find_edev(const char *busid)
{
	struct list_head	*p;

	list_for_each(p, &edev_list) {
		struct usbip_exported_device *edev;

		edev = list_entry(p, struct usbip_exported_device, node);
		if (!strncmp(busid, edev->udev.busid, USBIP_BUS_ID_SIZE)) {
			return edev;
		}
	}
	return NULL;
}