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
#include "usbip_forward.h"
#include "usbip_wudev.h"

static const char usbip_attach_usage_string[] =
	"usbip attach <args>\n"
	"    -r, --remote=<host>    The machine with exported USB devices\n"
	"    -b, --busid=<busid>    Busid of the device on <host>\n"
	"    -i, --instid=<instid>  (Optional) Serial number to use as instance ID\n";

void usbip_attach_usage(void)
{
	printf("usage: %s", usbip_attach_usage_string);
}

static int
import_device(SOCKET sockfd, usbip_wudev_t *wudev, const char *instid, HANDLE *phdev)
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

	rc = usbip_vhci_attach_device(hdev, port, instid, wudev);

	if (rc < 0) {
		err("import device");
		usbip_vhci_driver_close(hdev);
		return 1;
	}

	*phdev = hdev;

	return port;
}

static int query_import_device(SOCKET sockfd, const char *busid, HANDLE *phdev, const char *instid)
{
	int rc;
	struct op_import_request request;
	struct op_import_reply   reply;
	usbip_wudev_t	wuDev;
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

	get_wudev(sockfd, &wuDev, &reply.udev);

	/* import a device */
	return import_device(sockfd, &wuDev, instid, phdev);
}

static int
attach_device(const char *host, const char *busid, const char *instid)
{
	SOCKET	sockfd;
	int	rhport;
	HANDLE	hdev = INVALID_HANDLE_VALUE;

	sockfd = usbip_net_tcp_connect(host, usbip_port_string);
	if (sockfd == INVALID_SOCKET) {
		err("tcp connect");
		return 1;
	}

	rhport = query_import_device(sockfd, busid, &hdev, instid);
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
		{ "instid", optional_argument, NULL, 'i' },
		{ NULL, 0, NULL, 0 }
	};
	char *host = NULL;
	char *busid = NULL;
	char *instid = NULL;
	int opt;
	int ret = -1;

	for (;;) {
		opt = getopt_long(argc, argv, "r:b:i:", opts, NULL);

		if (opt == -1)
			break;

		switch (opt) {
		case 'r':
			host = optarg;
			break;
		case 'b':
			busid = optarg;
			break;
		case 'i':
			instid = optarg;
			break;
		default:
			goto err_out;
		}
	}

	if (!host || !busid)
		goto err_out;

	ret = attach_device(host, busid, instid);
	goto out;

err_out:
	usbip_attach_usage();
out:
	return ret;
}
