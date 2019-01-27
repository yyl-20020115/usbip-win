#include "vhci.h"

#include "vhci_dev.h"

static NTSTATUS
vhci_power_vhub(pusbip_vhub_dev_t vhub, PIRP Irp)
{
	PIO_STACK_LOCATION	stack;
	POWER_STATE		powerState;
	POWER_STATE_TYPE	powerType;
	NTSTATUS		status;

	stack = IoGetCurrentIrpStackLocation(Irp);
	powerType = stack->Parameters.Power.Type;
	powerState = stack->Parameters.Power.State;

	inc_io_vhub(vhub);

	// If the device is not stated yet, just pass it down.
	if (vhub->common.DevicePnPState == NotStarted) {
		PoStartNextPowerIrp(Irp);
		IoSkipCurrentIrpStackLocation(Irp);
		status = PoCallDriver(vhub->NextLowerDriver, Irp);
		dec_io_vhub(vhub);
		return status;

	}

	if (stack->MinorFunction == IRP_MN_SET_POWER) {
		DBGI(DBG_POWER, "\tRequest to set %s state to %s\n",
			((powerType == SystemPowerState) ? "System" : "Device"),
			((powerType == SystemPowerState) ? \
				dbg_system_power(powerState.SystemState) : \
				dbg_device_power(powerState.DeviceState)));
	}

	PoStartNextPowerIrp(Irp);
	IoSkipCurrentIrpStackLocation(Irp);
	status = PoCallDriver(vhub->NextLowerDriver, Irp);
	dec_io_vhub(vhub);
	return status;
}

static NTSTATUS
vhci_power_vpdo(pusbip_vpdo_dev_t vpdo, PIRP Irp)
{
	PIO_STACK_LOCATION	stack;
	POWER_STATE		powerState;
	POWER_STATE_TYPE	powerType;
	NTSTATUS		status;

	stack = IoGetCurrentIrpStackLocation(Irp);
	powerType = stack->Parameters.Power.Type;
	powerState = stack->Parameters.Power.State;

	switch (stack->MinorFunction) {
	case IRP_MN_SET_POWER:
		DBGI(DBG_POWER, "\tSetting %s power state to %s\n",
			((powerType == SystemPowerState) ? "System" : "Device"),
			((powerType == SystemPowerState) ? \
				dbg_system_power(powerState.SystemState) : \
				dbg_device_power(powerState.DeviceState)));

		switch (powerType) {
		case DevicePowerState:
			PoSetPowerState(vpdo->common.Self, powerType, powerState);
			vpdo->common.DevicePowerState = powerState.DeviceState;
			status = STATUS_SUCCESS;
			break;

		case SystemPowerState:
			vpdo->common.SystemPowerState = powerState.SystemState;
			status = STATUS_SUCCESS;
			break;

		default:
			status = STATUS_NOT_SUPPORTED;
			break;
		}
		break;
	case IRP_MN_QUERY_POWER:
		status = STATUS_SUCCESS;
		break;
	case IRP_MN_WAIT_WAKE:
		// We cannot support wait-wake because we are root-enumerated
		// driver, and our parent, the PnP manager, doesn't support wait-wake.
		// If you are a bus enumerated device, and if  your parent bus supports
		// wait-wake,  you should send a wait/wake IRP (PoRequestPowerIrp)
		// in response to this request.
		// If you want to test the wait/wake logic implemented in the function
		// driver (USBIP.sys), you could do the following simulation:
		// a) Mark this IRP pending.
		// b) Set a cancel routine.
		// c) Save this IRP in the device extension
		// d) Return STATUS_PENDING.
		// Later on if you suspend and resume your system, your vhci_power()
		// will be called to power the bus. In response to IRP_MN_SET_POWER, if the
		// powerstate is PowerSystemWorking, complete this Wake IRP.
		// If the function driver, decides to cancel the wake IRP, your cancel routine
		// will be called. There you just complete the IRP with STATUS_CANCELLED.
	case IRP_MN_POWER_SEQUENCE:
	default:
		status = STATUS_NOT_SUPPORTED;
		break;
	}

	if (status != STATUS_NOT_SUPPORTED) {
		Irp->IoStatus.Status = status;
	}

	PoStartNextPowerIrp(Irp);
	status = Irp->IoStatus.Status;
	IoCompleteRequest(Irp, IO_NO_INCREMENT);

	return status;
}

NTSTATUS
vhci_power(__in PDEVICE_OBJECT devobj, __in PIRP Irp)
{
	pdev_common_t	devcom;
	PIO_STACK_LOCATION	irpStack;
	NTSTATUS		status;

	DBGI(DBG_GENERAL | DBG_POWER, "vhci_power: Enter\n");

	status = STATUS_SUCCESS;
	irpStack = IoGetCurrentIrpStackLocation (Irp);
	ASSERT (IRP_MJ_POWER == irpStack->MajorFunction);

	devcom = (pdev_common_t)devobj->DeviceExtension;

	// If the device has been removed, the driver should
	// not pass the IRP down to the next lower driver.
	if (devcom->DevicePnPState == Deleted) {
		PoStartNextPowerIrp (Irp);
		Irp->IoStatus.Status = status = STATUS_NO_SUCH_DEVICE ;
		IoCompleteRequest(Irp, IO_NO_INCREMENT);
		return status;
	}

	if (devcom->is_vhub) {
		DBGI(DBG_POWER, "vhub: minor: %s IRP:0x%p %s %s\n",
		     dbg_power_minor(irpStack->MinorFunction), Irp,
		     dbg_system_power(devcom->SystemPowerState),
		     dbg_device_power(devcom->DevicePowerState));

		status = vhci_power_vhub((pusbip_vhub_dev_t)devobj->DeviceExtension, Irp);
	} else {
		DBGI(DBG_POWER, "vpdo: minor: %s IRP:0x%p %s %s\n",
			 dbg_power_minor(irpStack->MinorFunction), Irp,
			 dbg_system_power(devcom->SystemPowerState),
			 dbg_device_power(devcom->DevicePowerState));

		status = vhci_power_vpdo((pusbip_vpdo_dev_t)devobj->DeviceExtension, Irp);
	}

	return status;
}