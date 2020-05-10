#include "vhci.h"

VOID
add_ref_vpdo(pusbip_vpdo_dev_t vpdo)
{
	InterlockedIncrement(&vpdo->n_refs);
}

VOID
del_ref_vpdo(pusbip_vpdo_dev_t vpdo)
{
	InterlockedDecrement(&vpdo->n_refs);
}

PAGEABLE NTSTATUS
destroy_vpdo(pusbip_vpdo_dev_t vpdo)
{
	PAGED_CODE();

	if (vpdo->winstid != NULL)
		ExFreePoolWithTag(vpdo->winstid, USBIP_VHCI_POOL_TAG);

	// VHCI does not queue any irps at this time so we have nothing to do.
	// Free any resources.

	//FIXME
	if (vpdo->fo) {
		vpdo->fo->FsContext = NULL;
		vpdo->fo = NULL;
	}
	DBGI(DBG_PNP, "Deleting vpdo: 0x%p\n", vpdo);
	IoDeleteDevice(vpdo->common.Self);
	return STATUS_SUCCESS;
}
