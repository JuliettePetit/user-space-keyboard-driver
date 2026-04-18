#include <dirent.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/limits.h>
#include <linux/usb/ch9.h>
#include <linux/usbdevice_fs.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define HID_CLASS          0x03
#define HID_GET_DESCRIPTOR 0x06
#define HID_DT_REPORT      0x22
#define KEYBOARD_ENDPOINT  0x81
#define REPORT_SIZE        8

int fd = -1;

typedef struct
{
  uint8_t bLength;
  uint8_t bDescriptorType;
  uint16_t bcdUSB;
  uint8_t bDeviceClass;
  uint8_t bDeviceSubClass;
  uint8_t bDeviceProtocol;
  uint8_t bMaxPacketSize0;
  uint16_t idVendor;
  uint16_t idProduct;
  uint16_t bcdDevice;
  uint8_t iManufacturer;
  uint8_t iProduct;
  uint8_t iSerialNumber;
  uint8_t bNumConfigurations;
} __attribute__ ((packed)) usb_device_descriptor_t;

void
cleanup ()
{
  if (fd < 0)
    return;
  int ifno = 0;
  ioctl (fd, USBDEVFS_RELEASEINTERFACE, &ifno);

  struct usbdevfs_ioctl reattach
      = { .ifno = 0, .ioctl_code = USBDEVFS_CONNECT, .data = NULL };
  ioctl (fd, USBDEVFS_IOCTL, &reattach);
  close (fd);
}

void
signal_handler (int sig)
{
  (void)sig;
  // cleanup(fd);
  exit (EXIT_SUCCESS);
}

char *
get_usb_device (uint16_t vid, uint16_t pid)
{
  char *device = calloc (
      PATH_MAX + 1,
      sizeof (char)); // size is unix path max length + the finishing null byte

  DIR *buses = opendir ("/dev/bus/usb/");

  if (!buses)
    err (0, "cannot open dir %s", device);

  // scan /dev/bus/usb/BUS/
  struct dirent *bus_entry;
  while ((bus_entry = readdir (buses)))
    {
      if (bus_entry->d_name[0] == '.')
        continue;

      char bus_path[PATH_MAX + 1];
      snprintf (bus_path, sizeof (bus_path), "/dev/bus/usb/%s",
                bus_entry->d_name);

      DIR *devs = opendir (bus_path);
      if (!devs)
        {
          warnx ("cannot open dir %s", device);
          continue;
        }

      // scan /dev/bus/usb/BUS/DEV
      struct dirent *dev_entry;
      while ((dev_entry = readdir (devs)))
        {
          // Open and read device descriptor
          char dev_path[PATH_MAX + 1];
          int written = snprintf (dev_path, sizeof (dev_path), "%s/%s",
                                  bus_path, dev_entry->d_name);
          if (written < 0 || written >= (int)sizeof (dev_path))
            {
              warnx ("path too long");
              continue;
            }

          int fd = open (dev_path, O_RDONLY);
          if (fd < 0)
            continue; // skips if no permission

          usb_device_descriptor_t desc;
          int n = read (fd, &desc, sizeof (desc));
          close (fd);

          if (n < (int)sizeof (desc))
            continue; // skips if incomplete read

          if (desc.idProduct == pid && desc.idVendor == vid)
            {
              memset (device, 0, PATH_MAX + 1);
              snprintf (device, PATH_MAX + 1, "%s", dev_path);
              closedir (devs);
              closedir (buses);
              return device;
            }

          // if we didn't find the right keyboard driver, we want to return a
          // keyboard driver we found
          if (desc.bDeviceClass == HID_CLASS && device[0] == '\0')
            {
              memset (device, 0, PATH_MAX + 1);
              snprintf (device, PATH_MAX + 1, "%s", dev_path);
            }
        }
      closedir (devs);
    }
  closedir (buses);
  return device;
}

int
main ()
{
  signal (SIGINT, signal_handler);  // Ctrl+C
  signal (SIGTERM, signal_handler); // kill / system shutdown
  signal (SIGQUIT, signal_handler); // Ctrl+backslash
  signal (SIGHUP, signal_handler);  // terminal closed
  atexit (cleanup);
  // char *keyboard_path = get_usb_device (0x413c, 0x2113);
  // char *keyboard_path = get_usb_device (0, 0);
  char *keyboard_path = get_usb_device (0x1a2c, 0x9b09);
  if (keyboard_path[0] == '\0')
    errx (EXIT_FAILURE, "no keyboard found");

  fd = open (keyboard_path, O_RDWR);
  if (fd < 0)
    errx (EXIT_FAILURE, "cannot open %s", keyboard_path);

  printf ("keyboard found : %s\n", keyboard_path);

  /* detach HID driver from kernel */
  struct usbdevfs_ioctl detach
      = { .ifno = 0, .ioctl_code = USBDEVFS_DISCONNECT, .data = NULL };
  if (ioctl (fd, USBDEVFS_IOCTL, &detach) < 0)
    err (EXIT_FAILURE, "cannot detach kernel driver");

  /* claim interface */
  int ifno = 0;
  if (ioctl (fd, USBDEVFS_CLAIMINTERFACE, &ifno) < 0)
    err (EXIT_FAILURE, "cannot claim interface");

  /* read device descriptor */
  struct usb_device_descriptor desc;
  struct usbdevfs_ctrltransfer ctrl = { .bRequestType = USB_DIR_IN,
                                        .bRequest     = USB_REQ_GET_DESCRIPTOR,
                                        .wValue       = USB_DT_DEVICE << 8,
                                        .wIndex       = 0,
                                        .wLength      = sizeof (desc),
                                        .timeout      = 1000,
                                        .data         = &desc };

  if (ioctl (fd, USBDEVFS_CONTROL, &ctrl) < 0)
    err (EXIT_FAILURE, "cannot read specified device");

  /* reads HID descriptor */
  uint8_t report_desc[256];
  struct usbdevfs_ctrltransfer ctrl2
      = { .bRequestType = USB_DIR_IN | USB_TYPE_STANDARD | USB_RECIP_INTERFACE,
          .bRequest     = USB_REQ_GET_DESCRIPTOR,
          .wValue       = HID_DT_REPORT << 8,
          .wIndex       = 0, // interface 0
          .wLength      = sizeof (report_desc),
          .timeout      = 1000,
          .data         = report_desc };

  int len = ioctl (fd, USBDEVFS_CONTROL, &ctrl2);
  if (len < 0)
    err (EXIT_FAILURE, "cannot read HID report descriptor");

  uint8_t report[REPORT_SIZE];

  struct usbdevfs_ctrltransfer ctrl_proto = {
      .bRequestType = USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
      .bRequest     = 0x0B,  // SET_PROTOCOL
      .wValue       = 0,     // boot protocol
      .wIndex       = 0,
      .wLength      = 0,
      .timeout      = 1000,
      .data         = NULL
  };
  if (ioctl(fd, USBDEVFS_CONTROL, &ctrl_proto) < 0)
      warn("cannot set boot protocol");

  struct usbdevfs_ctrltransfer ctrl_idle = {
      .bRequestType = USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
      .bRequest     = 0x0A,  // SET_IDLE
      .wValue       = 0,     // send report on every change
      .wIndex       = 0,
      .wLength      = 0,
      .timeout      = 1000,
      .data         = NULL
  };
  if (ioctl(fd, USBDEVFS_CONTROL, &ctrl_idle) < 0)
      warn("cannot set idle");

  while (1)
    {
      struct usbdevfs_bulktransfer bulk
          = { .ep      = KEYBOARD_ENDPOINT,
              .len     = sizeof (report),
              .timeout = 100, // 100ms, check signals regularly
              .data    = report };

      int n = ioctl (fd, USBDEVFS_BULK, &bulk);
      if (n < 0)
        {
          if (errno == ETIMEDOUT)
            continue; // no data, loop again
          if (errno == EINTR)
            break; // signal received, exit loop
          err (EXIT_FAILURE, "interrupt transfer failed");
        }

      printf ("report: ");
      for (int i = 0; i < n; i++)
        printf ("%02x ", report[i]);
      printf ("\n");
    }

  // cleanup(fd);
  return EXIT_SUCCESS;
}
