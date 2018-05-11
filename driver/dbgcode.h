#pragma once

extern const char *dbg_ioctl_code(unsigned int ioctl_code);
extern const char *dbg_urbfunc(unsigned int urbfunc);
extern const char *dbg_pnp_minor(UCHAR minor);
extern const char *dbg_bus_query_id_type(BUS_QUERY_ID_TYPE type);
extern const char *dbg_dev_relation(DEVICE_RELATION_TYPE type);
extern const char *dbg_wmi_minor(UCHAR minor);
extern const char *dbg_power_minor(UCHAR minor);
extern const char *dbg_system_power(SYSTEM_POWER_STATE state);
extern const char *dbg_device_power(DEVICE_POWER_STATE state);
