#pragma once

#include <ntddk.h>
#include <ntstrsafe.h>
#include <usbdi.h>

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

	int	n_conf_descs;
	PUSB_CONFIGURATION_DESCRIPTOR	*conf_descs;

	UNICODE_STRING	interface_name;

	/* for pending read irp management */
	KSPIN_LOCK	lock_irp_read;
	PIRP	irp_read;
	int	wait_reader;
	KEVENT	event_read;
} usbip_stub_dev_t;

void init_dev_removal_lock(usbip_stub_dev_t *devstub);
NTSTATUS lock_dev_removal(usbip_stub_dev_t *devstub);
void unlock_dev_removal(usbip_stub_dev_t *devstub);
void unlock_wait_dev_removal(usbip_stub_dev_t *devstub);

void remove_devlink(usbip_stub_dev_t *devstub);
void cleanup_conf_descs(usbip_stub_dev_t *devstub);
