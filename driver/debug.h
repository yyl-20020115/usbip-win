#pragma once

//
// Debugging Output Levels
//

#define BUS_DBG_ALWAYS                  0x00000000

#define BUS_DBG_STARTUP_SHUTDOWN_MASK   0x0000000F
#define BUS_DBG_SS_NOISE                0x00000001
#define BUS_DBG_SS_TRACE                0x00000002
#define BUS_DBG_SS_INFO                 0x00000004
#define BUS_DBG_SS_ERROR                0x00000008

#define BUS_DBG_PNP_MASK                0x000000F0
#define BUS_DBG_PNP_NOISE               0x00000010
#define BUS_DBG_PNP_TRACE               0x00000020
#define BUS_DBG_PNP_INFO                0x00000040
#define BUS_DBG_PNP_ERROR               0x00000080

#define BUS_DBG_IOCTL_MASK              0x00000F00
#define BUS_DBG_IOCTL_NOISE             0x00000100
#define BUS_DBG_IOCTL_TRACE             0x00000200
#define BUS_DBG_IOCTL_INFO              0x00000400
#define BUS_DBG_IOCTL_ERROR             0x00000800

#define BUS_DBG_POWER_MASK              0x0000F000
#define BUS_DBG_POWER_NOISE             0x00001000
#define BUS_DBG_POWER_TRACE             0x00002000
#define BUS_DBG_POWER_INFO              0x00004000
#define BUS_DBG_POWER_ERROR             0x00008000

#define BUS_DBG_WMI_MASK                0x000F0000
#define BUS_DBG_WMI_NOISE               0x00010000
#define BUS_DBG_WMI_TRACE               0x00020000
#define BUS_DBG_WMI_INFO                0x00040000
#define BUS_DBG_WMI_ERROR               0x00080000

#if DBG
#define BUS_DEFAULT_DEBUG_OUTPUT_LEVEL 0x000FFFFF

#define Bus_KdPrint(_d_,_l_, _x_) \
            if (!(_l_) || (_d_)->DebugLevel & (_l_)) { \
               DbgPrint ("USBIPEnum: "); \
               DbgPrint _x_; \
            }

#define Bus_KdPrint_Cont(_d_,_l_, _x_) \
            if (!(_l_) || (_d_)->DebugLevel & (_l_)) { \
               DbgPrint _x_; \
            }

#define Bus_KdPrint_Def(_l_, _x_) \
            if (!(_l_) || BusEnumDebugLevel & (_l_)) { \
               DbgPrint ("USBIPEnum: "); \
               DbgPrint _x_; \
            }

#define DbgRaiseIrql(_x_,_y_) KeRaiseIrql(_x_,_y_)
#define DbgLowerIrql(_x_) KeLowerIrql(_x_)
#else

#define BUS_DEFAULT_DEBUG_OUTPUT_LEVEL 0x0
#define Bus_KdPrint(_d_, _l_, _x_)
#define Bus_KdPrint_Cont(_d_, _l_, _x_)
#define Bus_KdPrint_Def(_l_, _x_)
#define DbgRaiseIrql(_x_,_y_)
#define DbgLowerIrql(_x_)

#endif
