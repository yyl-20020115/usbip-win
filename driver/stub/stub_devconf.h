#pragma once

#include <ntddk.h>
#include <usbspec.h>

#include "devconf.h"

typedef struct {
	int	n_devconfs;
	devconf_t	devconfs[1];
} devconfs_t;

BOOLEAN is_iso_transfer(devconfs_t *devconfs, int ep);

void free_devconfs(devconfs_t *devconfs);

devconf_t get_devconf(devconfs_t *devconfs, USHORT idx);

void set_conf_info(devconfs_t *devconfs, USHORT idx, USBD_CONFIGURATION_HANDLE hconf, PUSBD_INTERFACE_INFORMATION intf);

int get_n_intfs(devconfs_t *devconfs, USHORT idx);
int get_n_endpoints(devconfs_t *devconfs, USHORT idx);