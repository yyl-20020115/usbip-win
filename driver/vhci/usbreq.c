#include "vhci.h"

#include "usbip_proto.h"
#include "usbip_vhci_api.h"
#include "usbreq.h"

extern NTSTATUS
store_urbr(PIRP irp, struct urb_req *urbr);

#ifdef DBG

const char *
dbg_urbr(struct urb_req *urbr)
{
	static char	buf[128];

	if (urbr == NULL)
		return "[null]";
	dbg_snprintf(buf, 128, "[seq:%d]", urbr->seq_num);
	return buf;
}

#endif

void
build_setup_packet(usb_cspkt_t *csp, unsigned char direct_in, unsigned char type, unsigned char recip, unsigned char request)
{
	csp->bmRequestType.B = 0;
	csp->bmRequestType.Type = type;
	if (direct_in)
		csp->bmRequestType.Dir = BMREQUEST_DEVICE_TO_HOST;
	csp->bmRequestType.Recipient = recip;
	csp->bRequest = request;
}

struct urb_req *
find_sent_urbr(pusbip_vpdo_dev_t vpdo, struct usbip_header *hdr)
{
	KIRQL		oldirql;
	PLIST_ENTRY	le;

	KeAcquireSpinLock(&vpdo->lock_urbr, &oldirql);
	for (le = vpdo->head_urbr_sent.Flink; le != &vpdo->head_urbr_sent; le = le->Flink) {
		struct urb_req	*urbr;
		urbr = CONTAINING_RECORD(le, struct urb_req, list_state);
		if (urbr->seq_num == hdr->base.seqnum) {
			RemoveEntryList(le);
			RemoveEntryList(&urbr->list_all);
			KeReleaseSpinLock(&vpdo->lock_urbr, oldirql);
			return urbr;
		}
	}
	KeReleaseSpinLock(&vpdo->lock_urbr, oldirql);

	return NULL;
}

struct urb_req *
find_pending_urbr(pusbip_vpdo_dev_t vpdo)
{
	struct urb_req	*urbr;

	if (IsListEmpty(&vpdo->head_urbr_pending))
		return NULL;

	urbr = CONTAINING_RECORD(vpdo->head_urbr_pending.Flink, struct urb_req, list_state);
	urbr->seq_num = ++(vpdo->seq_num);
	RemoveEntryList(&urbr->list_state);
	InitializeListHead(&urbr->list_state);
	return urbr;
}

static void
remove_cancelled_urbr(pusbip_vpdo_dev_t vpdo, PIRP irp)
{
	KIRQL	oldirql = irp->CancelIrql;
	PLIST_ENTRY	le;

	KeAcquireSpinLockAtDpcLevel(&vpdo->lock_urbr);

	for (le = vpdo->head_urbr.Flink; le != &vpdo->head_urbr; le = le->Flink) {
		struct urb_req	*urbr;

		urbr = CONTAINING_RECORD(le, struct urb_req, list_all);
		if (urbr->irp == irp) {
			RemoveEntryList(le);
			RemoveEntryList(&urbr->list_state);
			if (vpdo->urbr_sent_partial == urbr) {
				vpdo->urbr_sent_partial = NULL;
				vpdo->len_sent_partial = 0;
			}
			KeReleaseSpinLock(&vpdo->lock_urbr, oldirql);

			DBGI(DBG_GENERAL, "urb cancelled: %s\n", dbg_urbr(urbr));
			ExFreeToNPagedLookasideList(&g_lookaside, urbr);
			return;
		}
	}

	KeReleaseSpinLock(&vpdo->lock_urbr, oldirql);

	DBGW(DBG_GENERAL, "no matching urbr\n");
}

static void
cancel_urbr(PDEVICE_OBJECT devobj, PIRP irp)
{
	pusbip_vpdo_dev_t	vpdo;

	vpdo = (pusbip_vpdo_dev_t)devobj->DeviceExtension;
	DBGI(DBG_GENERAL, "irp will be cancelled: %p\n", irp);

	remove_cancelled_urbr(vpdo, irp);

	irp->IoStatus.Status = STATUS_CANCELLED;
	IoCompleteRequest(irp, IO_NO_INCREMENT);
	IoReleaseCancelSpinLock(irp->CancelIrql);
}

static struct urb_req *
create_urbr(pusbip_vpdo_dev_t vpdo, PIRP irp)
{
	struct urb_req	*urbr;

	urbr = ExAllocateFromNPagedLookasideList(&g_lookaside);
	if (urbr == NULL) {
		DBGE(DBG_URB, "create_urbr: out of memory\n");
		return NULL;
	}
	RtlZeroMemory(urbr, sizeof(*urbr));
	urbr->vpdo = vpdo;
	urbr->irp = irp;
	return urbr;
}

static BOOLEAN
insert_pending_or_sent_urbr(pusbip_vpdo_dev_t vpdo, struct urb_req *urbr, BOOLEAN is_pending)
{
	PIRP	irp = urbr->irp;

	IoSetCancelRoutine(irp, cancel_urbr);
	if (irp->Cancel) {
		IoSetCancelRoutine(irp, NULL);
		return FALSE;
	}
	else {
		IoMarkIrpPending(irp);
		if (is_pending)
			InsertTailList(&vpdo->head_urbr_pending, &urbr->list_state);
		else
			InsertTailList(&vpdo->head_urbr_sent, &urbr->list_state);
	}
	return TRUE;
}

NTSTATUS
submit_urbr(pusbip_vpdo_dev_t vpdo, PIRP irp)
{
	struct urb_req	*urbr;
	KIRQL	oldirql;
	PIRP	read_irp;
	NTSTATUS	status = STATUS_PENDING;

	if ((urbr = create_urbr(vpdo, irp)) == NULL)
		return STATUS_INSUFFICIENT_RESOURCES;

	KeAcquireSpinLock(&vpdo->lock_urbr, &oldirql);

	if (vpdo->urbr_sent_partial || vpdo->pending_read_irp == NULL) {
		if (!insert_pending_or_sent_urbr(vpdo, urbr, TRUE)) {
			KeReleaseSpinLock(&vpdo->lock_urbr, oldirql);
			ExFreeToNPagedLookasideList(&g_lookaside, urbr);
			DBGI(DBG_URB, "submit_urbr: urb cancelled\n");
			return STATUS_CANCELLED;
		}
		InsertTailList(&vpdo->head_urbr, &urbr->list_all);
		KeReleaseSpinLock(&vpdo->lock_urbr, oldirql);
		DBGI(DBG_URB, "submit_urbr: urb pending\n");
		return STATUS_PENDING;
	}

	read_irp = vpdo->pending_read_irp;
	vpdo->urbr_sent_partial = urbr;

	urbr->seq_num = ++(vpdo->seq_num);

	KeReleaseSpinLock(&vpdo->lock_urbr, oldirql);

	status = store_urbr(read_irp, urbr);

	KeAcquireSpinLock(&vpdo->lock_urbr, &oldirql);

	if (status == STATUS_SUCCESS) {
		if (vpdo->len_sent_partial == 0) {
			vpdo->urbr_sent_partial = NULL;
			if (!insert_pending_or_sent_urbr(vpdo, urbr, FALSE))
				status = STATUS_CANCELLED;
		}

		if (status == STATUS_SUCCESS) {
			InsertTailList(&vpdo->head_urbr, &urbr->list_all);
			vpdo->pending_read_irp = NULL;
			KeReleaseSpinLock(&vpdo->lock_urbr, oldirql);

			read_irp->IoStatus.Status = STATUS_SUCCESS;
			IoCompleteRequest(read_irp, IO_NO_INCREMENT);
			status = STATUS_PENDING;
		}
		else {
			KeReleaseSpinLock(&vpdo->lock_urbr, oldirql);
			ExFreeToNPagedLookasideList(&g_lookaside, urbr);
		}
	}
	else {
		vpdo->urbr_sent_partial = NULL;
		KeReleaseSpinLock(&vpdo->lock_urbr, oldirql);

		ExFreeToNPagedLookasideList(&g_lookaside, urbr);
		status = STATUS_INVALID_PARAMETER;
	}
	DBGI(DBG_URB, "submit_urbr: urb requested: status:%s\n", dbg_ntstatus(status));
	return status;
}
