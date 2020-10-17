#include "vhci_driver.h"
#include "vhci_plugin.tmh"

#include "usbip_vhci_api.h"
#include "devconf.h"

extern VOID
setup_ep_callbacks(PUDECX_USB_DEVICE_STATE_CHANGE_CALLBACKS pcallbacks);

extern NTSTATUS
add_ep(pctx_vusb_t vusb, PUDECXUSBENDPOINT_INIT *pepinit, PUSB_ENDPOINT_DESCRIPTOR dscr_ep);

static BOOLEAN
setup_vusb(UDECXUSBDEVICE ude_usbdev)
{
	pctx_vusb_t	vusb = TO_VUSB(ude_usbdev);
	WDF_OBJECT_ATTRIBUTES       attrs, attrs_hmem;
	NTSTATUS	status;

	WDF_OBJECT_ATTRIBUTES_INIT(&attrs);
	attrs.ParentObject = ude_usbdev;

	status = WdfWaitLockCreate(&attrs, &vusb->lock);
	if (NT_ERROR(status)) {
		TRE(PLUGIN, "failed to create wait lock: %!STATUS!", status);
		return FALSE;
	}

	WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attrs_hmem, urb_req_t);
	attrs_hmem.ParentObject = ude_usbdev;

	status = WdfLookasideListCreate(&attrs, sizeof(urb_req_t), PagedPool, &attrs_hmem, 0, &vusb->lookaside_urbr);
	if (NT_ERROR(status)) {
		TRE(PLUGIN, "failed to create urbr memory: %!STATUS!", status);
		return FALSE;
	}

	vusb->ude_usbdev = ude_usbdev;
	vusb->pending_req_read = NULL;
	vusb->urbr_sent_partial = NULL;
	vusb->len_sent_partial = 0;
	vusb->seq_num = 0;
	vusb->invalid = FALSE;

	InitializeListHead(&vusb->head_urbr);
	InitializeListHead(&vusb->head_urbr_pending);
	InitializeListHead(&vusb->head_urbr_sent);

	return TRUE;
}

static NTSTATUS
vusb_d0_entry(_In_ WDFDEVICE hdev, _In_ UDECXUSBDEVICE ude_usbdev)
{
	UNREFERENCED_PARAMETER(hdev);
	UNREFERENCED_PARAMETER(ude_usbdev);

	TRD(VUSB, "Enter");

	return STATUS_NOT_SUPPORTED;
}

static NTSTATUS
vusb_d0_exit(_In_ WDFDEVICE hdev, _In_ UDECXUSBDEVICE ude_usbdev, UDECX_USB_DEVICE_WAKE_SETTING setting)
{
	UNREFERENCED_PARAMETER(hdev);
	UNREFERENCED_PARAMETER(ude_usbdev);
	UNREFERENCED_PARAMETER(setting);

	TRD(VUSB, "Enter");

	return STATUS_NOT_SUPPORTED;
}

static NTSTATUS
vusb_set_function_suspend_and_wake(_In_ WDFDEVICE UdecxWdfDevice, _In_ UDECXUSBDEVICE UdecxUsbDevice,
	_In_ ULONG Interface, _In_ UDECX_USB_DEVICE_FUNCTION_POWER FunctionPower)
{
	UNREFERENCED_PARAMETER(UdecxWdfDevice);
	UNREFERENCED_PARAMETER(UdecxUsbDevice);
	UNREFERENCED_PARAMETER(Interface);
	UNREFERENCED_PARAMETER(FunctionPower);

	TRD(VUSB, "Enter");

	return STATUS_NOT_SUPPORTED;
}

static PUDECXUSBDEVICE_INIT
build_vusb_pdinit(pctx_vhci_t vhci, UDECX_ENDPOINT_TYPE eptype, UDECX_USB_DEVICE_SPEED speed)
{
	PUDECXUSBDEVICE_INIT	pdinit;
	UDECX_USB_DEVICE_STATE_CHANGE_CALLBACKS	callbacks;

	pdinit = UdecxUsbDeviceInitAllocate(vhci->hdev);

	UDECX_USB_DEVICE_CALLBACKS_INIT(&callbacks);

	setup_ep_callbacks(&callbacks);
	callbacks.EvtUsbDeviceLinkPowerEntry = vusb_d0_entry;
	callbacks.EvtUsbDeviceLinkPowerExit = vusb_d0_exit;
	callbacks.EvtUsbDeviceSetFunctionSuspendAndWake = vusb_set_function_suspend_and_wake;

	UdecxUsbDeviceInitSetStateChangeCallbacks(pdinit, &callbacks);
	UdecxUsbDeviceInitSetSpeed(pdinit, speed);

	UdecxUsbDeviceInitSetEndpointsType(pdinit, eptype);

	return pdinit;
}

static void
setup_descriptors(PUDECXUSBDEVICE_INIT pdinit, pvhci_pluginfo_t pluginfo)
{
	NTSTATUS	status;
	USHORT		conf_dscr_fullsize;

	status = UdecxUsbDeviceInitAddDescriptor(pdinit, pluginfo->dscr_dev, 18);
	if (NT_ERROR(status)) {
		TRW(PLUGIN, "failed to add a device descriptor to device init");
	}
	conf_dscr_fullsize = *((PUSHORT)pluginfo->dscr_conf + 1);
	status = UdecxUsbDeviceInitAddDescriptor(pdinit, pluginfo->dscr_conf, conf_dscr_fullsize);
	if (NT_ERROR(status)) {
		TRW(PLUGIN, "failed to add a configuration descriptor to device init");
	}
}

static VOID
vusb_cleanup(_In_ WDFOBJECT ude_usbdev)
{
	UNREFERENCED_PARAMETER(ude_usbdev);
	TRD(VUSB, "Enter");
}

static void
create_endpoints(UDECXUSBDEVICE ude_usbdev, pvhci_pluginfo_t pluginfo)
{
	pctx_vusb_t vusb;
	PUDECXUSBENDPOINT_INIT	epinit;
	PUSB_CONFIGURATION_DESCRIPTOR	dsc_conf = (PUSB_CONFIGURATION_DESCRIPTOR)pluginfo->dscr_conf;
	PUSB_ENDPOINT_DESCRIPTOR	dsc_ep;
	PVOID	start;

	vusb = TO_VUSB(ude_usbdev);
	vusb->ude_usbdev = ude_usbdev;
	epinit = UdecxUsbSimpleEndpointInitAllocate(ude_usbdev);

	add_ep(vusb, &epinit, NULL);

	start = dsc_conf;
	while ((dsc_ep = dsc_next_ep(dsc_conf, start)) != NULL) {
		epinit = UdecxUsbSimpleEndpointInitAllocate(ude_usbdev);
		add_ep(vusb, &epinit, dsc_ep);
		start = dsc_ep;
	}
}

static UDECX_ENDPOINT_TYPE
get_eptype(pvhci_pluginfo_t pluginfo)
{
	PUSB_DEVICE_DESCRIPTOR	dsc_dev = (PUSB_DEVICE_DESCRIPTOR)pluginfo->dscr_dev;
	PUSB_CONFIGURATION_DESCRIPTOR	dsc_conf = (PUSB_CONFIGURATION_DESCRIPTOR)pluginfo->dscr_conf;

	if (dsc_dev->bNumConfigurations > 1 || dsc_conf->bNumInterfaces > 1)
		return UdecxEndpointTypeDynamic;
	if (dsc_conf_get_n_intfs(dsc_conf) > 1)
		return UdecxEndpointTypeDynamic;
	return UdecxEndpointTypeSimple;
}

static UDECX_USB_DEVICE_SPEED
get_device_speed(pvhci_pluginfo_t pluginfo)
{
	unsigned short	bcdUSB = *(unsigned short *)(pluginfo->dscr_dev + 2);

	switch (bcdUSB) {
	case 0x0100:
		return UdecxUsbLowSpeed;
	case 0x0110:
		return UdecxUsbFullSpeed;
	case 0x0200:
		return UdecxUsbHighSpeed;
	case 0x0300:
		return UdecxUsbSuperSpeed;
	default:
		TRE(PLUGIN, "unknown bcdUSB:%x", (ULONG)bcdUSB);
		return UdecxUsbLowSpeed;
	}
}

static pctx_vusb_t
vusb_plugin(pctx_vhci_t vhci, pvhci_pluginfo_t pluginfo)
{
	pctx_vusb_t	vusb;
	PUDECXUSBDEVICE_INIT	pdinit;
	UDECX_ENDPOINT_TYPE	eptype;
	UDECX_USB_DEVICE_SPEED	speed;
	UDECX_USB_DEVICE_PLUG_IN_OPTIONS	opts;
	UDECXUSBDEVICE	ude_usbdev;
	WDF_OBJECT_ATTRIBUTES       attrs;
	NTSTATUS	status;

	eptype = get_eptype(pluginfo);
	speed = get_device_speed(pluginfo);
	pdinit = build_vusb_pdinit(vhci, eptype, speed);
	setup_descriptors(pdinit, pluginfo);

	WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attrs, ctx_vusb_t);
	attrs.EvtCleanupCallback = vusb_cleanup;

	status = UdecxUsbDeviceCreate(&pdinit, &attrs, &ude_usbdev);
	if (NT_ERROR(status)) {
		TRE(PLUGIN, "failed to create usb device: %!STATUS!", status);
		UdecxUsbDeviceInitFree(pdinit);
		return NULL;
	}

	vusb = TO_VUSB(ude_usbdev);
	vusb->vhci = vhci;

	if (eptype == UdecxEndpointTypeSimple) {
		vusb->is_simple_ep_alloc = TRUE;
		create_endpoints(ude_usbdev, pluginfo);
	}
	else {
		vusb->is_simple_ep_alloc = FALSE;
		vusb->ep_default = NULL;
	}

	UDECX_USB_DEVICE_PLUG_IN_OPTIONS_INIT(&opts);
	opts.Usb20PortNumber = pluginfo->port;

	if (!setup_vusb(ude_usbdev)) {
		WdfObjectDelete(ude_usbdev);
		return NULL;
	}

	status = UdecxUsbDevicePlugIn(ude_usbdev, &opts);
	if (NT_ERROR(status)) {
		TRE(PLUGIN, "failed to plugin a new device %!STATUS!", status);
		WdfObjectDelete(ude_usbdev);
		return NULL;
	}

	vusb->devid = pluginfo->devid;
	vusb->default_conf_value = pluginfo->dscr_conf[5];

	return vusb;
}

NTSTATUS
plugin_vusb(pctx_vhci_t vhci, WDFREQUEST req, pvhci_pluginfo_t pluginfo)
{
	pctx_vusb_t	vusb;
	NTSTATUS	status = STATUS_UNSUCCESSFUL;

	WdfWaitLockAcquire(vhci->lock, NULL);

	if (vhci->vusbs[pluginfo->port - 1] != NULL) {
		WdfWaitLockRelease(vhci->lock);
		return STATUS_OBJECT_NAME_COLLISION;
	}

	vusb = vusb_plugin(vhci, pluginfo);
	if (vusb != NULL) {
		WDFFILEOBJECT	fo = WdfRequestGetFileObject(req);
		if (fo != NULL) {
			pctx_vusb_t	*pvusb = TO_PVUSB(fo);
			*pvusb = vusb;
		}
		else {
			TRE(PLUGIN, "empty fileobject. setup failed");
		}
		vhci->vusbs[pluginfo->port - 1] = vusb;
		status = STATUS_SUCCESS;
	}

	WdfWaitLockRelease(vhci->lock);

	if (vusb->is_simple_ep_alloc) {
		/* UDE framework ignores SELECT CONF & INTF for a simple type */
		submit_req_select(vusb->ep_default, NULL, TRUE, vusb->default_conf_value, 0, 0);
		submit_req_select(vusb->ep_default, NULL, FALSE, 0, 0, 0);
	}
	return status;
}
