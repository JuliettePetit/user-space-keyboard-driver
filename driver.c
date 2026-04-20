#include <dirent.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/limits.h>
#include <linux/usb/ch9.h>
#include <linux/usbdevice_fs.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define HID_CLASS 0x03
// #define USB_DT_INTERFACE 0x04
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
  exit (EXIT_SUCCESS);
}

bool
is_HID (char *dev_path)
{
  char path[PATH_MAX + 1] = { 0 };
  snprintf (path, PATH_MAX + 1, "%s", dev_path);
  fd = open (path, O_RDWR);
  if (fd < 0)
    return false;

  uint8_t config_buf[256];
  struct usbdevfs_ctrltransfer ctrl = { .bRequestType = USB_DIR_IN,
                                        .bRequest     = USB_REQ_GET_DESCRIPTOR,
                                        .wValue       = USB_DT_CONFIG << 8,
                                        .wIndex       = 0,
                                        .wLength      = sizeof (config_buf),
                                        .timeout      = 1000,
                                        .data         = config_buf };

  int len = ioctl (fd, USBDEVFS_CONTROL, &ctrl);
  if (len < 0)
    errx (EXIT_FAILURE, "cannot read configuration descriptor");

  // Walk the blob
  int offset = 0;
  while (offset < len)
    {
      uint8_t desc_len  = config_buf[offset];
      uint8_t desc_type = config_buf[offset + 1];

      if (desc_type == USB_DT_INTERFACE)
        {
          uint8_t iface_class = config_buf[offset + 5];
          if (iface_class == 0x03)
            {
              close (fd);
              return true;
            }
        }

      if (desc_len == 0)
        break; // avoid infinite loop on malformed descriptor
      offset += desc_len;
    }
  close (fd);
  return false;
}

char *
get_usb_device (uint16_t vid, uint16_t pid)
{
  char *device = calloc (
      PATH_MAX + 1,
      sizeof (char)); // size is unix path max length + the finishing null byte

  DIR *buses = opendir ("/dev/bus/usb/");

  if (!buses)
    err (0, "cannot open dir /dev/bus/usb/");

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
          warnx ("cannot open dir %s", bus_path);
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
              printf ("%d", desc.bDeviceClass);
              return device;
            }

          // if we didn't find the right keyboard driver, we want to return a
          // keyboard driver we found
          if (desc.bDeviceClass == HID_CLASS || is_HID (dev_path))
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

static const char *keycode_to_str(uint8_t kc)
{
    static const char *table[256] = {
        [0x04] = "a", [0x05] = "b", [0x06] = "c", [0x07] = "d",
        [0x08] = "e", [0x09] = "f", [0x0a] = "g", [0x0b] = "h",
        [0x0c] = "i", [0x0d] = "j", [0x0e] = "k", [0x0f] = "l",
        [0x10] = "m", [0x11] = "n", [0x12] = "o", [0x13] = "p",
        [0x14] = "q", [0x15] = "r", [0x16] = "s", [0x17] = "t",
        [0x18] = "u", [0x19] = "v", [0x1a] = "w", [0x1b] = "x",
        [0x1c] = "y", [0x1d] = "z",
        [0x1e] = "1", [0x1f] = "2", [0x20] = "3", [0x21] = "4",
        [0x22] = "5", [0x23] = "6", [0x24] = "7", [0x25] = "8",
        [0x26] = "9", [0x27] = "0",
        [0x28] = "ENTER",     [0x29] = "ESC",   [0x2a] = "BACKSPACE",
        [0x2b] = "TAB",       [0x2c] = " ",
        [0x2d] = "-",         [0x2e] = "=",
        [0x2f] = "[",         [0x30] = "]",     [0x31] = "\\",
        [0x33] = ";",         [0x34] = "'",     [0x35] = "`",
        [0x36] = ",",         [0x37] = ".",     [0x38] = "/",
        [0x39] = "CAPSLOCK",
        [0x4f] = "RIGHT",     [0x50] = "LEFT",
        [0x51] = "DOWN",      [0x52] = "UP",
    };

    if (table[kc])
        return table[kc];
    return NULL;
}


int
main ()
{
  signal (SIGINT, signal_handler);  // Ctrl+C
  signal (SIGTERM, signal_handler); // kill / system shutdown
  signal (SIGQUIT, signal_handler); // Ctrl+backslash
  signal (SIGHUP, signal_handler);  // terminal closed
  atexit (cleanup);

  char *keyboard_path = get_usb_device (0, 0);

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

  struct usbdevfs_ctrltransfer ctrl_proto = {
    .bRequestType = USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
    .bRequest = 0x0B, // SET_PROTOCOL
    .wValue = 0, // boot protocol
    .wIndex = 0,
    .wLength = 0,
    .timeout = 1000,
    .data = NULL
  };

  if (ioctl (fd, USBDEVFS_CONTROL, &ctrl_proto) < 0)
    warn ("cannot set boot protocol");

  uint8_t report[REPORT_SIZE];
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

      puts ("report: ");
      for (int i = 0; i < n; i++)
        printf ("%02x ", report[i]);

      if (report[2] != 0)
      {
          uint8_t modifiers = report[0];
          for (int i = 2; i < 8; i++)
          {
              if (report[i] == 0)
                  break;

              const char *str = keycode_to_str(report[i]);

              if (str)
                  printf("%s", str);
          }
          // exit if Ctrl C
          if(modifiers == 0x01 && report[2]== 0x06)
              return EXIT_SUCCESS;
      }
      puts("\n");

    }
  return EXIT_SUCCESS;
}
