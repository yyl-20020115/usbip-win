#pragma once

#ifdef DBG

#include <ntddk.h>
#include "usbreq.h"

#define DBG_GENERAL	0x00000001
#define DBG_READ	0x00000010
#define DBG_WRITE	0x00000100
#define DBG_PNP		0x00001000
#define DBG_IOCTL	0x00010000
#define DBG_POWER	0x00100000
#define DBG_WMI		0x01000000
#define DBG_URB		0x10000000

#define DBGE(part, fmt, ...)	DbgPrintEx(DPFLTR_SYSTEM_ID, DPFLTR_MASK | (part), "usbip_vhci:(EE) " fmt, ## __VA_ARGS__)
#define DBGW(part, fmt, ...)	DbgPrintEx(DPFLTR_SYSTEM_ID, DPFLTR_MASK | ((part) << 1), "usbip_vhci:(WW) " fmt, ## __VA_ARGS__)
#define DBGI(part, fmt, ...)	DbgPrintEx(DPFLTR_SYSTEM_ID, DPFLTR_MASK | ((part) << 2), "usbip_vhci: " fmt, ## __VA_ARGS__)

extern const char *dbg_urb_req(struct urb_req *urb_r);

extern const char *dbg_ntstatus(NTSTATUS status);
extern const char *dbg_ioctl_code(unsigned int ioctl_code);
extern const char *dbg_urbfunc(unsigned int urbfunc);
extern const char *dbg_pnp_minor(UCHAR minor);
extern const char *dbg_bus_query_id_type(BUS_QUERY_ID_TYPE type);
extern const char *dbg_dev_relation(DEVICE_RELATION_TYPE type);
extern const char *dbg_wmi_minor(UCHAR minor);
extern const char *dbg_power_minor(UCHAR minor);
extern const char *dbg_system_power(SYSTEM_POWER_STATE state);
extern const char *dbg_device_power(DEVICE_POWER_STATE state);

#else

#define DBGE(part, fmt, ...)
#define DBGW(part, fmt, ...)
#define DBGI(part, fmt, ...)

#endif	
