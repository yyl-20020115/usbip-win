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

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <getopt.h>

#include "usbip_windows.h"

#include "usbip_common.h"
#include "usbip_network.h"
#include "usbip.h"

static const char usbip_attach_usage_string[] =
	"usbip attach <args>\n"
	"    -h, --host=<host>      The machine with exported USB devices\n"
	"    -b, --busid=<busid>    Busid of the device on <host>\n";

void usbip_attach_usage(void)
{
	printf("usage: %s", usbip_attach_usage_string);
}

static int import_device(SOCKET sockfd, struct usbip_usb_device *udev,
	struct usbip_usb_interface *uinf0,
	HANDLE *devfd)
{
	HANDLE fd;
	int port, ret;

	fd = usbip_vbus_open();
	if (INVALID_HANDLE_VALUE == fd) {
		err("open vbus driver");
		return -1;
	}

	port = usbip_vbus_get_free_port(fd);
	if (port <= 0) {
		err("no free port");
		CloseHandle(fd);
		return -1;
	}

	dbg("call from attch here\n");
	ret = usbip_vbus_attach_device(fd, port, udev, uinf0);
	dbg("return from attch here\n");

	if (ret < 0) {
		err("import device");
		CloseHandle(fd);
		return -1;
	}
	dbg("devfd:%p\n", devfd);
	*devfd = fd;

	return port;
}

static int query_import_device(SOCKET sockfd, char *busid,
	struct usbip_usb_interface *uinf0, HANDLE * fd)
{
	int ret;
	struct op_import_request request;
	struct op_import_reply   reply;
	uint16_t code = OP_REP_IMPORT;

	memset(&request, 0, sizeof(request));
	memset(&reply, 0, sizeof(reply));

	/* send a request */
	ret = usbip_send_op_common(sockfd, OP_REQ_IMPORT, 0);
	if (ret < 0) {
		err("send op_common");
		return -1;
	}

	strncpy_s(request.busid, SYSFS_BUS_ID_SIZE, busid, sizeof(request.busid));
	request.busid[sizeof(request.busid) - 1] = 0;

	PACK_OP_IMPORT_REQUEST(0, &request);

	ret = usbip_send(sockfd, (void *)&request, sizeof(request));
	if (ret < 0) {
		err("send op_import_request");
		return -1;
	}

	/* recieve a reply */
	ret = usbip_recv_op_common(sockfd, &code);
	if (ret < 0) {
		err("recv op_common");
		return -1;
	}

	ret = usbip_recv(sockfd, (void *)&reply, sizeof(reply));
	if (ret < 0) {
		err("recv op_import_reply");
		return -1;
	}

	PACK_OP_IMPORT_REPLY(0, &reply);

	/* check the reply */
	if (strncmp(reply.udev.busid, busid, sizeof(reply.udev.busid))) {
		err("recv different busid %s", reply.udev.busid);
		return -1;
	}

	/* import a device */
	return import_device(sockfd, &reply.udev, uinf0, fd);
}

static int query_interface0(SOCKET sockfd, char * busid, struct usbip_usb_interface * uinf0)
{
	int ret;
	struct op_devlist_reply rep;
	uint16_t code = OP_REP_DEVLIST;
	uint32_t i, j;
	char product_name[100];
	char class_name[100];
	struct usbip_usb_device udev;
	struct usbip_usb_interface uinf;
	int found = 0;

	memset(&rep, 0, sizeof(rep));

	ret = usbip_send_op_common(sockfd, OP_REQ_DEVLIST, 0);
	if (ret < 0) {
		err("send op_common");
		return -1;
	}

	ret = usbip_recv_op_common(sockfd, &code);
	if (ret < 0) {
		err("recv op_common");
		return -1;
	}

	ret = usbip_recv(sockfd, (void *)&rep, sizeof(rep));
	if (ret < 0) {
		err("recv op_devlist");
		return -1;
	}

	PACK_OP_DEVLIST_REPLY(0, &rep);
	dbg("exportable %d devices", rep.ndev);

	for (i = 0; i < rep.ndev; i++) {

		memset(&udev, 0, sizeof(udev));

		ret = usbip_recv(sockfd, (void *)&udev, sizeof(udev));
		if (ret < 0) {
			err("recv usbip_usb_device[%d]", i);
			return -1;
		}
		pack_usb_device(0, &udev);
		usbip_names_get_product(product_name, sizeof(product_name),
			udev.idVendor, udev.idProduct);
		usbip_names_get_class(class_name, sizeof(class_name), udev.bDeviceClass,
			udev.bDeviceSubClass, udev.bDeviceProtocol);

		dbg("%8s: %s", udev.busid, product_name);
		dbg("%8s: %s", " ", udev.path);
		dbg("%8s: %s", " ", class_name);

		for (j = 0; j < udev.bNumInterfaces; j++) {

			ret = usbip_recv(sockfd, (void *)&uinf, sizeof(uinf));
			if (ret < 0) {
				err("recv usbip_usb_interface[%d]", j);
				return -1;
			}

			pack_usb_interface(0, &uinf);
			if (!strcmp(udev.busid, busid) && j == 0) {
				memcpy(uinf0, &uinf, sizeof(uinf));
				found = 1;
			}
			usbip_names_get_class(class_name, sizeof(class_name),
				uinf.bInterfaceClass,
				uinf.bInterfaceSubClass,
				uinf.bInterfaceProtocol);

			dbg("%8s: %2d - %s", " ", j, class_name);
		}

		dbg(" ");
	}
	if (found)
		return 0;
	return -1;
}

static int attach_device(char * host, char * busid)
{
	SOCKET sockfd;
	int rhport;
	HANDLE devfd = INVALID_HANDLE_VALUE;
	struct usbip_usb_interface uinf;

	sockfd = usbip_net_tcp_connect(host, USBIP_PORT_STRING);
	if (INVALID_SOCKET == sockfd) {
		err("tcp connect");
		return 0;
	}
	if (query_interface0(sockfd, busid, &uinf)) {
		err("cannot find device");
		return 0;
	}
	closesocket(sockfd);
	sockfd = usbip_net_tcp_connect(host, USBIP_PORT_STRING);
	if (INVALID_SOCKET == sockfd) {
		err("tcp connect");
		return 0;
	}
	rhport = query_import_device(sockfd, busid, &uinf, &devfd);
	if (rhport < 0) {
		err("query");
		return 0;
	}
	info("new usb device attached to usbvbus port %d\n", rhport);
	usbip_vbus_forward(sockfd, devfd);

	dbg("detaching device");
	usbip_vbus_detach_device(devfd, rhport);

	dbg("closing connection to device");
	CloseHandle(devfd);

	dbg("closing connection to peer");
	closesocket(sockfd);

	dbg("done");
	return 1;
}

int usbip_attach(int argc, char *argv[])
{
	static const struct option opts[] = {
		{ "host", required_argument, NULL, 'h' },
		{ "busid", required_argument, NULL, 'b' },
		{ NULL, 0, NULL, 0 }
	};
	char *host = NULL;
	char *busid = NULL;
	int opt;
	int ret = -1;

	for (;;) {
		opt = getopt_long(argc, argv, "h:b:", opts, NULL);

		if (opt == -1)
			break;

		switch (opt) {
		case 'h':
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