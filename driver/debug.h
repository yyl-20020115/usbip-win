#pragma once

#ifdef DBG

#define DBG_GENERAL	0x00000001
#define DBG_READ	0x00000010
#define DBG_WRITE	0x00000100
#define DBG_PNP		0x00001000
#define DBG_IOCTL	0x00010000
#define DBG_POWER	0x00100000
#define DBG_WMI		0x01000000
#define DBG_URB		0x10000000

#define DBGE(part, fmt, ...)	DbgPrintEx(DPFLTR_SYSTEM_ID, DPFLTR_MASK | (part), "USBIPEnum:(EE) " fmt, ## __VA_ARGS__)
#define DBGW(part, fmt, ...)	DbgPrintEx(DPFLTR_SYSTEM_ID, DPFLTR_MASK | ((part) << 1), "USBIPEnum:(WW) " fmt, ## __VA_ARGS__)
#define DBGI(part, fmt, ...)	DbgPrintEx(DPFLTR_SYSTEM_ID, DPFLTR_MASK | ((part) << 2), "USBIPEnum: " fmt, ## __VA_ARGS__)

#define DbgRaiseIrql(_x_,_y_) KeRaiseIrql(_x_,_y_)
#define DbgLowerIrql(_x_) KeLowerIrql(_x_)

#else

#define DBGE(part, fmt, ...)
#define DBGW(part, fmt, ...)
#define DBGI(part, fmt, ...)

#define DbgRaiseIrql(_x_,_y_)
#define DbgLowerIrql(_x_)

#endif
