#pragma once

#include <ntddk.h>
#include <ntstrsafe.h>
#include <usbdi.h>
#include <usbdlib.h>

#include "stub_devconf.h"

#define N_DEVICES_USBIP_STUB	32

typedef struct {
	long	count;
	int	remove_pending;
	KEVENT	event;
} usbip_stub_remove_lock_t;

typedef struct {
	PDEVICE_OBJECT	self;
	PDEVICE_OBJECT	pdo;
	PDEVICE_OBJECT	next_stack_dev;
	usbip_stub_remove_lock_t	remove_lock;
	BOOLEAN		is_started;
	int	id;

	char	id_hw[256];

	devconf_t	*devconf;

	UNICODE_STRING	interface_name;

	USBD_HANDLE	hUSBD;

	KSPIN_LOCK	lock_stub_res;
	PIRP		irp_stub_read;
	BOOLEAN		is_pending_stub_res;
	unsigned long	stub_res_seqnum;
	int		stub_res_err;
	PVOID		stub_res_data;
	int		stub_res_data_len;
} usbip_stub_dev_t;

void init_dev_removal_lock(usbip_stub_dev_t *devstub);
NTSTATUS lock_dev_removal(usbip_stub_dev_t *devstub);
void unlock_dev_removal(usbip_stub_dev_t *devstub);
void unlock_wait_dev_removal(usbip_stub_dev_t *devstub);

void remove_devlink(usbip_stub_dev_t *devstub);
