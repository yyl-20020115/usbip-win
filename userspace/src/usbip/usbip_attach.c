/*
 * Copyright (C) 2011 matt mooney <mfm@muteddisk.com>
 *               2005-2007 Takahiro Hirofuchi
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "usbip_windows.h"

#include "usbip_common.h"
#include "usbip_network.h"
#include "usbip_vhci.h"
#include "usbip_proto.h"
#include "usbip_forward.h"

static const char usbip_attach_usage_string[] =
	"usbip attach <args>\n"
	"    -r, --remote=<host>    The machine with exported USB devices\n"
	"    -b, --busid=<busid>    Busid of the device on <host>\n";

void usbip_attach_usage(void)
{
	printf("usage: %s", usbip_attach_usage_string);
}

/*
 * Sadly, udev structure from linux does not have an interface descriptor.
 * So we should get interface class number via GET_DESCRIPTOR usb command.
 */
static void
supplement_udev(SOCKET sockfd, UINT32 devid, struct usbip_usb_device *udev)
{
	struct usbip_header	uhdr;
	unsigned char	buf[18];
	unsigned	alen;

	memset(&uhdr, 0, sizeof(uhdr));

	uhdr.base.command = htonl(USBIP_CMD_SUBMIT);
	/* sufficient large enough seq used to avoid conflict with normal vhci operation */
	uhdr.base.seqnum = htonl(0x7fffffff);
	uhdr.base.direction = htonl(USBIP_DIR_IN);
	uhdr.base.devid = htonl(devid);

	uhdr.u.cmd_submit.transfer_buffer_length = htonl(18);
	uhdr.u.cmd_submit.setup[0] = 0x80;	/* IN/control port */
	uhdr.u.cmd_submit.setup[1] = 6;		/* GetDescriptor */
	uhdr.u.cmd_submit.setup[6] = 18;	/* Length */
	uhdr.u.cmd_submit.setup[3] = 2;		/* Configuration Descriptor */

	if (usbip_net_send(sockfd, &uhdr, sizeof(uhdr)) < 0) {
		err("get_desc: failed to send usbip header\n");
		return;
	}
	if (usbip_net_recv(sockfd, &uhdr, sizeof(uhdr)) < 0) {
		err("get_desc: failed to recv usbip header\n");
		return;
	}
	if (uhdr.u.ret_submit.status != 0) {
		err("get_desc: command submit error\n");
		return;
	}
	alen = ntohl(uhdr.u.ret_submit.actual_length);
	if (alen < 18) {
		err("get_desc: too short response\n");
		return;
	}
	if (usbip_net_recv(sockfd, buf, 18) < 0) {
		err("get_desc: failed to recv usbip payload\n");
		return;
	}
	udev->bDeviceClass = buf[14];
	udev->bDeviceSubClass = buf[15];
	udev->bDeviceProtocol = buf[16];
}

static void
try_to_tweak_udev(SOCKET sockfd, struct usbip_usb_device *udev)
{
	if (udev->bDeviceClass != 0 && udev->bDeviceSubClass != 0 && udev->bDeviceProtocol == 0)
		return;
	if (udev->bNumConfigurations != 1)
		return;
	supplement_udev(sockfd, udev->busnum << 16 | udev->devnum, udev);
}

static int import_device(SOCKET sockfd, struct usbip_usb_device *udev, HANDLE *phdev)
{
	HANDLE hdev;
	int rc;
	int port;

	hdev = usbip_vhci_driver_open();
	if (hdev == INVALID_HANDLE_VALUE) {
		err("open vhci driver");
		return 1;
	}

	port = usbip_vhci_get_free_port(hdev);
	if (port <= 0) {
		err("no free port");
		usbip_vhci_driver_close(hdev);
		return 1;
	}

	dbg("got free port %d", port);

	rc = usbip_vhci_attach_device(hdev, port, udev);

	if (rc < 0) {
		err("import device");
		usbip_vhci_driver_close(hdev);
		return 1;
	}

	*phdev = hdev;

	return port;
}

static int query_import_device(SOCKET sockfd, const char *busid, HANDLE *phdev)
{
	int rc;
	struct op_import_request request;
	struct op_import_reply   reply;
	uint16_t code = OP_REP_IMPORT;

	memset(&request, 0, sizeof(request));
	memset(&reply, 0, sizeof(reply));

	/* send a request */
	rc = usbip_net_send_op_common(sockfd, OP_REQ_IMPORT, 0);
	if (rc < 0) {
		err("send op_common");
		return 1;
	}

	strncpy_s(request.busid, USBIP_BUS_ID_SIZE, busid, sizeof(request.busid));

	PACK_OP_IMPORT_REQUEST(0, &request);

	rc = usbip_net_send(sockfd, (void *)&request, sizeof(request));
	if (rc < 0) {
		err("send op_import_request");
		return 1;
	}

	/* recieve a reply */
	rc = usbip_net_recv_op_common(sockfd, &code);
	if (rc < 0) {
		err("recv op_common");
		return 1;
	}

	rc = usbip_net_recv(sockfd, (void *)&reply, sizeof(reply));
	if (rc < 0) {
		err("recv op_import_reply");
		return 1;
	}

	PACK_OP_IMPORT_REPLY(0, &reply);

	/* check the reply */
	if (strncmp(reply.udev.busid, busid, sizeof(reply.udev.busid))) {
		err("recv different busid %s", reply.udev.busid);
		return 1;
	}

	/* Many devices have 0 usb class number in a device descriptor.
	 * 0 value means that class number is determined at interface level.
	 * USB class, subclass and protocol numbers should be setup before importing.
	 * Because windows vhci driver builds a device compatible id with those numbers.
	 */
	try_to_tweak_udev(sockfd, &reply.udev);

	/* import a device */
	return import_device(sockfd, &reply.udev, phdev);
}

static int
attach_device(const char *host, const char *busid)
{
	SOCKET	sockfd;
	int	rhport;
	HANDLE	hdev = INVALID_HANDLE_VALUE;

	sockfd = usbip_net_tcp_connect(host, usbip_port_string);
	if (sockfd == INVALID_SOCKET) {
		err("tcp connect");
		return 1;
	}

	rhport = query_import_device(sockfd, busid, &hdev);
	if (rhport < 0) {
		err("query");
		return 1;
	}

	usbip_forward(hdev, (HANDLE)sockfd, FALSE);

	usbip_vhci_detach_device(hdev, rhport);

	usbip_vhci_driver_close(hdev);

	closesocket(sockfd);

	return 0;
}

int usbip_attach(int argc, char *argv[])
{
	static const struct option opts[] = {
		{ "remote", required_argument, NULL, 'r' },
		{ "busid", required_argument, NULL, 'b' },
		{ NULL, 0, NULL, 0 }
	};
	char *host = NULL;
	char *busid = NULL;
	int opt;
	int ret = -1;

	for (;;) {
		opt = getopt_long(argc, argv, "r:b:", opts, NULL);

		if (opt == -1)
			break;

		switch (opt) {
		case 'r':
			host = optarg;
			break;
		case 'b':
			busid = optarg;
			break;
		default:
			goto err_out;
		}
	}

	if (!host || !busid)
		goto err_out;

	ret = attach_device(host, busid);
	goto out;

err_out:
	usbip_attach_usage();
out:
	return ret;
}
