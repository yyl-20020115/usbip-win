/* libusb-win32, Generic Windows USB Library
 * Copyright (c) 2002-2005 Stephan Meyer <ste_meyer@web.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "stub_driver.h"
#include "stub_dbg.h"

void
power_set_device_state(usbip_stub_dev_t *devstub, DEVICE_POWER_STATE device_state, BOOLEAN is_block);

static NTSTATUS
on_power_state_complete(PDEVICE_OBJECT devobj, IRP *irp, void *context)
{
	usbip_stub_dev_t	*devstub = (usbip_stub_dev_t *)context;
	IO_STACK_LOCATION	*irpstack = IoGetCurrentIrpStackLocation(irp);
	POWER_STATE		power_state = irpstack->Parameters.Power.State;
	DEVICE_POWER_STATE	dev_power_state;

	UNREFERENCED_PARAMETER(devobj);

	if (irp->PendingReturned) {
		IoMarkIrpPending(irp);
	}

	if (NT_SUCCESS(irp->IoStatus.Status)) {
		if (irpstack->Parameters.Power.Type == SystemPowerState) {
			/* save current system state */
			devstub->power_state.SystemState = power_state.SystemState;

			/* get supported device power state from the array reported by */
			/* IRP_MN_QUERY_CAPABILITIES */
			dev_power_state = devstub->device_power_states[power_state.SystemState];

			/* set the device power state, but don't block the thread */
			power_set_device_state(devstub, dev_power_state, FALSE);
		}
		else {
			/* DevicePowerState */
			if (power_state.DeviceState <= devstub->power_state.DeviceState) {
				/* device is powered up, */
				/* report device state to Power Manager */
				PoSetPowerState(devstub->self, DevicePowerState, power_state);
			}
			/* save current device state */
			devstub->power_state.DeviceState = power_state.DeviceState;
		}
	}
	else {
		DBGE(DBG_POWER, "failed power\n");
	}

	unlock_dev_removal(devstub);

	return STATUS_SUCCESS;
}

static NTSTATUS
on_filter_power_state_complete(PDEVICE_OBJECT devobj, IRP *irp, void *context)
{
	usbip_stub_dev_t	*devstub = (usbip_stub_dev_t *)context;
	IO_STACK_LOCATION	*irpstack = IoGetCurrentIrpStackLocation(irp);
	POWER_STATE power_state = irpstack->Parameters.Power.State;

	UNREFERENCED_PARAMETER(devobj);

	if (NT_SUCCESS(irp->IoStatus.Status)) {
		if (irpstack->Parameters.Power.Type == SystemPowerState) {
			/* save current system state */
			devstub->power_state.SystemState = power_state.SystemState;
		}
		else {
			/* DevicePowerState */

			if (power_state.DeviceState <= devstub->power_state.DeviceState) {
				/* device is powered up, */
				/* report device state to Power Manager */
				PoSetPowerState(devstub->self, DevicePowerState, power_state);
			}

			/* save current device state */
			devstub->power_state.DeviceState = power_state.DeviceState;
		}
	}
	else {
		DBGE(DBG_POWER, "failed power state\n");
	}

	unlock_dev_removal(devstub);

	return STATUS_SUCCESS;
}

static void
on_power_set_device_state_complete(PDEVICE_OBJECT devobj, UCHAR minor_function, POWER_STATE power_state,
                                   void *context, IO_STATUS_BLOCK *io_status)
{
	UNREFERENCED_PARAMETER(devobj);
	UNREFERENCED_PARAMETER(minor_function);
	UNREFERENCED_PARAMETER(power_state);
	UNREFERENCED_PARAMETER(io_status);

	KeSetEvent((KEVENT *)context, EVENT_INCREMENT, FALSE);
}

void
power_set_device_state(usbip_stub_dev_t *devstub, DEVICE_POWER_STATE device_state, BOOLEAN is_block)
{
	KEVENT		event;
	POWER_STATE	power_state;
	NTSTATUS	status;

	power_state.DeviceState = device_state;

	if (is_block) {
		/* wait for IRP to complete */
		KeInitializeEvent(&event, NotificationEvent, FALSE);

		/* set the device power state and wait for completion */
		status = PoRequestPowerIrp(devstub->pdo,
			IRP_MN_SET_POWER,
			power_state,
			on_power_set_device_state_complete,
			&event, NULL);

		if (status == STATUS_PENDING) {
			KeWaitForSingleObject(&event, Executive, KernelMode, FALSE, NULL);
		}
	}
	else {
		PoRequestPowerIrp(devstub->pdo, IRP_MN_SET_POWER, power_state, NULL, NULL, NULL);
	}
}

/* [trobinso MOD 4/16/2010]
 * If running as a filter, do not act as power policy owner.
 */
NTSTATUS
stub_dispatch_power(usbip_stub_dev_t *devstub, IRP *irp)
{
	IO_STACK_LOCATION	*irpstack = IoGetCurrentIrpStackLocation(irp);
	NTSTATUS	status;

	status = lock_dev_removal(devstub);
	if (NT_ERROR(status)) {
		irp->IoStatus.Status = status;
		PoStartNextPowerIrp(irp);
		IoCompleteRequest(irp, IO_NO_INCREMENT);
		return status;
	}

	if (irpstack->MinorFunction == IRP_MN_SET_POWER) {
		POWER_STATE	power_state = irpstack->Parameters.Power.State;

		if (irpstack->Parameters.Power.Type == SystemPowerState) {
		}
		else
		{
			if (power_state.DeviceState > devstub->power_state.DeviceState) {
				/* device is powered down, report device state to the */
				/* Power Manager before sending the IRP down */
				/* (power up is handled by the completion routine) */
				PoSetPowerState(devstub->self, DevicePowerState, power_state);
			}
		}

		/* TODO: should PoStartNextPowerIrp() be called here or from the */
		/* completion routine? */
		PoStartNextPowerIrp(irp);

		IoCopyCurrentIrpStackLocationToNext(irp);
		if (0 /* !devstub->is_filter && !devstub->power_control_disabled ////TODO */)  {
			IoSetCompletionRoutine(irp,
					       on_power_state_complete,
					       devstub,
					       TRUE, /* on success */
					       TRUE, /* on error   */
					       TRUE);/* on cancel  */
		}
		else
		{
			IoSetCompletionRoutine(irp,
					       on_filter_power_state_complete,
					       devstub,
					       TRUE, /* on success */
					       TRUE, /* on error   */
					       TRUE);/* on cancel  */
		}

		return PoCallDriver(devstub->next_stack_dev, irp);
	}
	else {
		/* pass all other power IRPs down without setting a completion routine */
		PoStartNextPowerIrp(irp);
		IoSkipCurrentIrpStackLocation(irp);
		status = PoCallDriver(devstub->next_stack_dev, irp);
		unlock_dev_removal(devstub);

		return status;
	}
}

