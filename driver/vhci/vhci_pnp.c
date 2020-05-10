#include "vhci.h"

#include "usbip_vhci_api.h"
#include "vhci_pnp.h"
#include "usbreq.h"

extern PAGEABLE NTSTATUS
vhci_pnp_vpdo(PDEVICE_OBJECT devobj, PIRP Irp, PIO_STACK_LOCATION IrpStack, pusbip_vpdo_dev_t vpdo);
extern PAGEABLE NTSTATUS
vhci_pnp_vhub(PDEVICE_OBJECT devobj, PIRP Irp, PIO_STACK_LOCATION IrpStack, pusbip_vhub_dev_t vhub);

PAGEABLE NTSTATUS
vhci_pnp(__in PDEVICE_OBJECT devobj, __in PIRP Irp)
{
	pdev_common_t		devcom;
	PIO_STACK_LOCATION	irpStack;
	NTSTATUS	status;

	PAGED_CODE();

	DBGI(DBG_GENERAL | DBG_PNP, "vhci_pnp: Enter\n");

	irpStack = IoGetCurrentIrpStackLocation(Irp);
	ASSERT(IRP_MJ_PNP == irpStack->MajorFunction);

	devcom = (pdev_common_t)devobj->DeviceExtension;

	// If the device has been removed, the driver should
	// not pass the IRP down to the next lower driver.
	if (devcom->DevicePnPState == Deleted) {
		Irp->IoStatus.Status = status = STATUS_NO_SUCH_DEVICE;
		IoCompleteRequest(Irp, IO_NO_INCREMENT);
		return status;
	}

	if (devcom->is_vhub) {
		DBGI(DBG_PNP, "vhub: minor: %s, IRP:0x%p\n", dbg_pnp_minor(irpStack->MinorFunction), Irp);

		// Request is for the vhub
		status = vhci_pnp_vhub(devobj, Irp, irpStack, (pusbip_vhub_dev_t)devcom);
	}
	else {
		DBGI(DBG_PNP, "vpdo: minor: %s, IRP: 0x%p\n", dbg_pnp_minor(irpStack->MinorFunction), Irp);

		// Request is for the child vpdo.
		status = vhci_pnp_vpdo(devobj, Irp, irpStack, (pusbip_vpdo_dev_t)devcom);
	}

	DBGI(DBG_GENERAL | DBG_PNP, "vhci_pnp: Leave: %s\n", dbg_ntstatus(status));

	return status;
}
