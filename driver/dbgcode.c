#include "driver.h"

#include <usbdi.h>

#ifdef DBG

#define K_V(a) {#a, a},

struct namecode {
	const char		*name;
	unsigned int	code;
};

struct namecode namecodes_ioctl[] = {
	K_V(IOCTL_INTERNAL_USB_CYCLE_PORT)
	K_V(IOCTL_INTERNAL_USB_ENABLE_PORT)
	K_V(IOCTL_INTERNAL_USB_GET_BUS_INFO)
	K_V(IOCTL_INTERNAL_USB_GET_BUSGUID_INFO)
	K_V(IOCTL_INTERNAL_USB_GET_CONTROLLER_NAME) 
	K_V(IOCTL_INTERNAL_USB_GET_DEVICE_HANDLE)
	K_V(IOCTL_INTERNAL_USB_GET_HUB_COUNT)
	K_V(IOCTL_INTERNAL_USB_GET_HUB_NAME)
	K_V(IOCTL_INTERNAL_USB_GET_PARENT_HUB_INFO)
	K_V(IOCTL_INTERNAL_USB_GET_PORT_STATUS)
	K_V(IOCTL_INTERNAL_USB_RESET_PORT)
	K_V(IOCTL_INTERNAL_USB_GET_ROOTHUB_PDO)
	K_V(IOCTL_INTERNAL_USB_SUBMIT_IDLE_NOTIFICATION)
	K_V(IOCTL_INTERNAL_USB_SUBMIT_URB)
	K_V(IOCTL_INTERNAL_USB_GET_TOPOLOGY_ADDRESS)
	K_V(IOCTL_USB_DIAG_IGNORE_HUBS_ON)
	K_V(IOCTL_USB_DIAG_IGNORE_HUBS_OFF)
	K_V(IOCTL_USB_DIAGNOSTIC_MODE_OFF)
	K_V(IOCTL_USB_DIAGNOSTIC_MODE_ON)
	K_V(IOCTL_USB_GET_DESCRIPTOR_FROM_NODE_CONNECTION)
	K_V(IOCTL_USB_GET_HUB_CAPABILITIES)
	K_V(IOCTL_USB_GET_ROOT_HUB_NAME)
	K_V(IOCTL_GET_HCD_DRIVERKEY_NAME)
	K_V(IOCTL_USB_GET_NODE_INFORMATION)
	K_V(IOCTL_USB_GET_NODE_CONNECTION_INFORMATION)
	K_V(IOCTL_USB_GET_NODE_CONNECTION_ATTRIBUTES)
	K_V(IOCTL_USB_GET_NODE_CONNECTION_NAME)
	K_V(IOCTL_USB_GET_NODE_CONNECTION_DRIVERKEY_NAME)
	K_V(IOCTL_USB_HCD_DISABLE_PORT)
	K_V(IOCTL_USB_HCD_ENABLE_PORT)
	K_V(IOCTL_USB_HCD_GET_STATS_1)
	K_V(IOCTL_USB_HCD_GET_STATS_2)
	{0,0}
};

struct namecode	namecodes_urb_func[] = {
	K_V(URB_FUNCTION_SELECT_CONFIGURATION)
	K_V(URB_FUNCTION_SELECT_INTERFACE)
	K_V(URB_FUNCTION_ABORT_PIPE)
	K_V(URB_FUNCTION_TAKE_FRAME_LENGTH_CONTROL)
	K_V(URB_FUNCTION_RELEASE_FRAME_LENGTH_CONTROL)
	K_V(URB_FUNCTION_GET_FRAME_LENGTH)
	K_V(URB_FUNCTION_SET_FRAME_LENGTH)
	K_V(URB_FUNCTION_GET_CURRENT_FRAME_NUMBER)
	K_V(URB_FUNCTION_CONTROL_TRANSFER)
	K_V(URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER)
	K_V(URB_FUNCTION_ISOCH_TRANSFER)
	K_V(URB_FUNCTION_RESET_PIPE)
	K_V(URB_FUNCTION_GET_DESCRIPTOR_FROM_DEVICE)
	K_V(URB_FUNCTION_GET_DESCRIPTOR_FROM_ENDPOINT)
	K_V(URB_FUNCTION_GET_DESCRIPTOR_FROM_INTERFACE)
	K_V(URB_FUNCTION_SET_DESCRIPTOR_TO_DEVICE)
	K_V(URB_FUNCTION_SET_DESCRIPTOR_TO_ENDPOINT)
	K_V(URB_FUNCTION_SET_DESCRIPTOR_TO_INTERFACE)
	K_V(URB_FUNCTION_SET_FEATURE_TO_DEVICE)
	K_V(URB_FUNCTION_SET_FEATURE_TO_INTERFACE)
	K_V(URB_FUNCTION_SET_FEATURE_TO_ENDPOINT)
	K_V(URB_FUNCTION_SET_FEATURE_TO_OTHER)
	K_V(URB_FUNCTION_CLEAR_FEATURE_TO_DEVICE)
	K_V(URB_FUNCTION_CLEAR_FEATURE_TO_INTERFACE)
	K_V(URB_FUNCTION_CLEAR_FEATURE_TO_ENDPOINT)
	K_V(URB_FUNCTION_CLEAR_FEATURE_TO_OTHER)
	K_V(URB_FUNCTION_GET_STATUS_FROM_DEVICE)
	K_V(URB_FUNCTION_GET_STATUS_FROM_INTERFACE)
	K_V(URB_FUNCTION_GET_STATUS_FROM_ENDPOINT)
	K_V(URB_FUNCTION_GET_STATUS_FROM_OTHER)
	K_V(URB_FUNCTION_RESERVED0)
	K_V(URB_FUNCTION_VENDOR_DEVICE)
	K_V(URB_FUNCTION_VENDOR_INTERFACE)
	K_V(URB_FUNCTION_VENDOR_ENDPOINT)
	K_V(URB_FUNCTION_VENDOR_OTHER)
	K_V(URB_FUNCTION_CLASS_DEVICE)
	K_V(URB_FUNCTION_CLASS_INTERFACE)
	K_V(URB_FUNCTION_CLASS_ENDPOINT)
	K_V(URB_FUNCTION_CLASS_OTHER)
	K_V(URB_FUNCTION_RESERVED)
	K_V(URB_FUNCTION_GET_CONFIGURATION)
	K_V(URB_FUNCTION_GET_INTERFACE)
	K_V(URB_FUNCTION_LAST)
	{0,0}
};

struct namecode	namecodes_pnp_minor[] = {
	K_V(IRP_MN_START_DEVICE)
	K_V(IRP_MN_QUERY_REMOVE_DEVICE)
	K_V(IRP_MN_REMOVE_DEVICE)
	K_V(IRP_MN_CANCEL_REMOVE_DEVICE)
	K_V(IRP_MN_STOP_DEVICE)
	K_V(IRP_MN_QUERY_STOP_DEVICE)
	K_V(IRP_MN_CANCEL_STOP_DEVICE)
	K_V(IRP_MN_QUERY_DEVICE_RELATIONS)
	K_V(IRP_MN_QUERY_INTERFACE)
	K_V(IRP_MN_QUERY_CAPABILITIES)
	K_V(IRP_MN_QUERY_RESOURCES)
	K_V(IRP_MN_QUERY_RESOURCE_REQUIREMENTS)
	K_V(IRP_MN_QUERY_DEVICE_TEXT)
	K_V(IRP_MN_FILTER_RESOURCE_REQUIREMENTS)
	K_V(IRP_MN_READ_CONFIG)
	K_V(IRP_MN_WRITE_CONFIG)
	K_V(IRP_MN_EJECT)
	K_V(IRP_MN_SET_LOCK)
	K_V(IRP_MN_QUERY_ID)
	K_V(IRP_MN_QUERY_PNP_DEVICE_STATE)
	K_V(IRP_MN_QUERY_BUS_INFORMATION)
	K_V(IRP_MN_DEVICE_USAGE_NOTIFICATION)
	K_V(IRP_MN_SURPRISE_REMOVAL)
	K_V(IRP_MN_QUERY_LEGACY_BUS_INFORMATION)
	{0,0}
};

struct namecode	namecodes_bus_query_id[] = {
	K_V(BusQueryDeviceID)
	K_V(BusQueryHardwareIDs)
	K_V(BusQueryCompatibleIDs)
	K_V(BusQueryInstanceID)
	K_V(BusQueryDeviceSerialNumber)
	K_V(BusQueryContainerID)
	{0,0}
};

struct namecode	namecodes_dev_relation[] = {
	K_V(BusRelations)
	K_V(PowerRelations)
	K_V(EjectionRelations)
	K_V(RemovalRelations)
	K_V(TargetDeviceRelation)
	{0,0}
};

struct namecode	namecodes_wmi_minor[] = {
	K_V(IRP_MN_CHANGE_SINGLE_INSTANCE)
	K_V(IRP_MN_CHANGE_SINGLE_ITEM)
	K_V(IRP_MN_DISABLE_COLLECTION)
	K_V(IRP_MN_DISABLE_EVENTS)
	K_V(IRP_MN_ENABLE_COLLECTION)
	K_V(IRP_MN_ENABLE_EVENTS)
	K_V(IRP_MN_EXECUTE_METHOD)
	K_V(IRP_MN_QUERY_ALL_DATA)
	K_V(IRP_MN_QUERY_SINGLE_INSTANCE)
	K_V(IRP_MN_REGINFO)
	{0,0}
};

struct namecode	namecodes_power_minor[] = {
	K_V(IRP_MN_SET_POWER)
	K_V(IRP_MN_QUERY_POWER)
	K_V(IRP_MN_POWER_SEQUENCE)
	K_V(IRP_MN_WAIT_WAKE)
	{0,0}
};

struct namecode	namecodes_system_power[] = {
	K_V(PowerSystemUnspecified)
	K_V(PowerSystemWorking)
	K_V(PowerSystemSleeping2)
	K_V(PowerSystemSleeping3)
	K_V(PowerSystemHibernate)
	K_V(PowerSystemShutdown)
	K_V(PowerSystemMaximum)
	{0,0}
};

struct namecode	namecodes_device_power[] = {
	K_V(PowerDeviceUnspecified)
	K_V(PowerDeviceD0)
	K_V(PowerDeviceD1)
	K_V(PowerDeviceD2)
	K_V(PowerDeviceD3)
	K_V(PowerDeviceMaximum)
	K_V(IRP_MN_EXECUTE_METHOD)
	K_V(IRP_MN_QUERY_ALL_DATA)
	K_V(IRP_MN_QUERY_SINGLE_INSTANCE)
	K_V(IRP_MN_REGINFO)
	{0,0}
};

static const char *
dbg_namecode(struct namecode *namecodes, const char *codetype, unsigned int code)
{
	static char	buf[128];
	int i;

	for (i = 0; namecodes[i].name; i++) {
		if (code == namecodes[i].code)
			return namecodes[i].name;
	}
	RtlStringCchPrintfA(buf, 128, "Unknown %s code: %x", codetype, code);
	return buf;
}

const char *
dbg_ioctl_code(unsigned int ioctl_code)
{
	return dbg_namecode(namecodes_ioctl, "ioctl", ioctl_code);
}

const char *
dbg_urbfunc(unsigned int urbfunc)
{
	return dbg_namecode(namecodes_urb_func, "urb function", urbfunc);
}

const char *
dbg_pnp_minor(UCHAR minor)
{
	return dbg_namecode(namecodes_pnp_minor, "pnp minor function", minor);
}

const char *
dbg_bus_query_id_type(BUS_QUERY_ID_TYPE type)
{
	return dbg_namecode(namecodes_bus_query_id, "bus query id", type);
}

const char *
dbg_dev_relation(DEVICE_RELATION_TYPE type)
{
	return dbg_namecode(namecodes_dev_relation, "device relation", type);
}

const char *
dbg_wmi_minor(UCHAR minor)
{
	return dbg_namecode(namecodes_wmi_minor, "wmi minor function", minor);
}

const char *
dbg_power_minor(UCHAR minor)
{
	return dbg_namecode(namecodes_power_minor, "power minor function", minor);
}

const char *
dbg_system_power(SYSTEM_POWER_STATE state)
{
	return dbg_namecode(namecodes_system_power, "system power", (int)state);
}

const char *
dbg_device_power(DEVICE_POWER_STATE state)
{
	return dbg_namecode(namecodes_device_power, "device power", (int)state);
}

#endif
