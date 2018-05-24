#pragma

#include <winsock2.h>
#include <windows.h>

#include "getopt.h"

int init_socket(void);
int cleanup_socket(void);
int show_port_status(void);

HANDLE usbip_vhci_driver_open(void);
void usbip_vhci_driver_close(HANDLE hdev);
int usbip_vhci_get_free_port(HANDLE hdev);
int usbip_vhci_attach_device(HANDLE hdev, int port, struct usbip_usb_device *udev);
int usbip_vhci_detach_device(HANDLE hdev, int port);
void usbip_vhci_forward(SOCKET sockfd, HANDLE hdev);