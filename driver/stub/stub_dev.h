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
	POWER_STATE		power_state;
	DEVICE_POWER_STATE	device_power_states[PowerSystemMaximum];
	BOOLEAN			power_control_disabled;
	BOOLEAN			surprise_removal_ok;
	int	id;

	char	id_hw[256];
	struct {
		USBD_CONFIGURATION_HANDLE	handle;
		int value;
		int index;
		///libusb_interface_t	interfaces[LIBUSB_MAX_NUMBER_OF_INTERFACES];
		PUSB_CONFIGURATION_DESCRIPTOR	descriptor;
		int	total_size;
	} config;

	UNICODE_STRING	interface_name;
} usbip_stub_dev_t;

void init_dev_removal_lock(usbip_stub_dev_t *devstub);
NTSTATUS lock_dev_removal(usbip_stub_dev_t *devstub);
void unlock_dev_removal(usbip_stub_dev_t *devstub);
void unlock_wait_dev_removal(usbip_stub_dev_t *devstub);

void remove_devlink(usbip_stub_dev_t *devstub);
