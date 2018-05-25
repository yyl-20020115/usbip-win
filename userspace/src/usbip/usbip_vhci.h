#pragma

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>

HANDLE usbip_vhci_driver_open(void);
void usbip_vhci_driver_close(HANDLE hdev);
int usbip_vhci_get_free_port(HANDLE hdev);
int usbip_vhci_attach_device(HANDLE hdev, int port, struct usbip_usb_device *udev);
int usbip_vhci_detach_device(HANDLE hdev, int port);
void usbip_vhci_forward(SOCKET sockfd, HANDLE hdev);