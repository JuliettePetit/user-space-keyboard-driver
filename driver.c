#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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

char *
get_usb_device (uint16_t vid, uint16_t pid)
{
  char *device = calloc (
      PATH_MAX + 1,
      sizeof (char)); // size is unix path max length + the finishing null byte
  strncat (device, "/dev/bus/usb", 12);

  DIR *buses = opendir (device);

  if (!buses)
    {
      perror ("cannot open dir /dev/bus/usb");
      return NULL;
    }

  // scan /dev/bus/usb/BUS/
  struct dirent *bus_entry;
  while ((bus_entry = readdir (buses)))
    {
      if (bus_entry->d_name[0] == '.')
        continue;

      strncat (device, bus_entry->d_name, PATH_MAX);

      DIR *devs = opendir (device);
      if (!devs)
        {
          perror ("cannot open dir /dev/bus/usb/bus");
          return NULL;
        }

      // scan /dev/bus/usb/BUS/DEV
      struct dirent *dev_entry;
      while ((dev_entry = readdir (devs)))
        {
          // Open and read device descriptor
          char dev_path[PATH_MAX + 1] = { 0 };
          strncat (dev_path, device, PATH_MAX);

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
              if (device[17] != '\0')
                device[17] = '\0'; // TODO
              strncat (device, dev_entry->d_name, PATH_MAX);
              return device;
            }

          // if we didn't find the right keyboard driver, we want to return a
          // keyboard driver we found
          if (desc.bDeviceClass == 0x03)
            strncat (device, dev_entry->d_name, PATH_MAX);
        }
    }
  printf ("keyboard found : %s", device);
  return device;
}

int
main ()
{
  int fd = open (get_usb_device (0, 0), O_RDONLY);
  if (fd < 0)
    return EXIT_FAILURE;

  return EXIT_SUCCESS;
}
