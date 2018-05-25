/*
 *
 * Copyright (C) 2005-2007 Takahiro Hirofuchi
 */

#include <winsock2.h>

#include "usbip_windows.h"

#include "usbip_common.h"

int init_socket(void)
{
    WSADATA wsaData;
    int err;

    err = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (err != 0) {
        err("WSAStartup failed with error: %d\n", err);
        return -1;
    }

    if (LOBYTE(wsaData.wVersion) != 2 || HIBYTE(wsaData.wVersion) != 2) {
        err("Could not find a usable version of Winsock.dll\n");
        WSACleanup();
        return -1;
    }
	return 0;
}

int cleanup_socket(void)
{
	WSACleanup();
	return 0;
}
