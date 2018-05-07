#ifndef _USBIP_WINDOWS_H
#define _USBIP_WINDOWS_H

#include <winsock2.h>
#include <windows.h>

int show_port_status(void);
int detach_port(char *port);
int init_socket();
int cleanup_socket();

HANDLE usbip_vbus_open(void);
int usbip_vbus_get_free_port(HANDLE fd);
int usbip_vbus_attach_device(HANDLE fd, int port, struct usbip_usb_device *udev);
int usbip_vbus_detach_device(HANDLE fd, int port);
void usbip_vbus_forward(SOCKET sockfd, HANDLE devfd);

#endif