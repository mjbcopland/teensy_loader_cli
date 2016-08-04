/* Teensy Loader, Command Line Interface
 * Program and Reboot Teensy Board with HalfKay Bootloader
 * http://www.pjrc.com/teensy/loader_cli.html
 * Copyright 2008-2010, PJRC.COM, LLC
 *
 * You may redistribute this program and/or modify it under the terms
 * of the GNU General Public License as published by the Free Software
 * Foundation, version 3 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see http://www.gnu.org/licenses/
 */

/* Want to incorporate this code into a proprietary application??
 * Just email paul@pjrc.com to ask.  Usually it's not a problem,
 * but you do need to ask to use this code in any way other than
 * those permitted by the GNU General Public License, version 3  */

/* For non-root permissions on ubuntu or similar udev-based linux
 * http://www.pjrc.com/teensy/49-teensy.rules
 */


#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <getopt.h>

#if defined _WIN32 || defined _WIN64
#include <windows.h>
#else
#include <unistd.h>
#endif

void usage(const char *err) {
  if (err) fprintf(stderr, "%s\n\n", err);

  fprintf(stderr,
    "Usage: teensy_loader_cli [options] [--] filename\n"
    "Options:\n"
    "  --help                   Display this help message\n"
    "  --wait                   Wait for device to appear\n"
    "  --hard-reboot            Use hard reboot if device not online\n"
    "  --soft-reboot            Use soft reboot if device not online (Teensy 3.x only)\n"
    "  --no-reboot              No reboot after programming\n"
    "  --boot-only              Boot only, do not program\n"
    "  --verbose                Verbose output\n"
    "  --mcu=MCU                Specify MCU type\n"
    "  --list-mcus              List available MCU types\n"
    "\n"
    "For more information, please visit:\n"
    "http://www.pjrc.com/teensy/loader_cli.html\n");

  exit(err ? EXIT_FAILURE : EXIT_SUCCESS);
}

// USB Access Functions
int teensy_open(void);
int teensy_write(void *buf, int len, double timeout);
void teensy_close(void);
int hard_reboot(void);
int soft_reboot(void);

// Intel Hex File Functions
int read_intel_hex(const char *filename);
int ihex_bytes_within_range(int begin, int end);
void ihex_get_data(int addr, int len, unsigned char *bytes);
int memory_is_blank(int addr, int block_size);

// Misc stuff
int printf_verbose(const char *format, ...);
void delay(double seconds);
void die(const char *str, ...);
void parse_options(int argc, char **argv);
void boot(unsigned char *buf, int write_size);

// options (from user via command line args)
int wait_for_device_to_appear = 0;
int hard_reboot_device = 0;
int soft_reboot_device = 0;
int reboot_after_programming = 1;
int verbose = 0;
int boot_only = 0;
int code_size = 0, block_size = 0;
const char *filename=NULL;


/****************************************************************/
/*                                                              */
/*                       Main Program                           */
/*                                                              */
/****************************************************************/

int main(int argc, char **argv)
{
  unsigned char buf[2048];
  int num, addr, r, write_size=block_size+2;
  int first_block=1, waited=0;

  // parse command line arguments
  parse_options(argc, argv);
  if (!filename && !boot_only) {
    usage("Filename must be specified");
  }
  if (!code_size) {
    usage("MCU type must be specified");
  }
  printf_verbose("Teensy Loader, Command Line, Version 2.0\n");

  if (boot_only) {
    if (! teensy_open()) {
      die("Could not find HalfKay");
    }
    printf_verbose("Found HalfKay Bootloader\n");

    boot(buf, block_size+2);
    exit(0);
  }

  // read the intel hex file
  // this is done first so any error is reported before using USB
  num = read_intel_hex(filename);
  if (num < 0) die("error reading intel hex file \"%s\"", filename);
  printf_verbose("Read \"%s\": %d bytes, %.1f%% usage\n",
    filename, num, (double)num / (double)code_size * 100.0);

  // open the USB device
  while (1) {
    if (teensy_open()) break;
    if (hard_reboot_device) {
      if (!hard_reboot()) die("Unable to find rebootor\n");
      printf_verbose("Hard Reboot performed\n");
      hard_reboot_device = 0; // only hard reboot once
      wait_for_device_to_appear = 1;
    }
    if (soft_reboot_device) {
      if (soft_reboot()) {
        printf_verbose("Soft reboot performed\n");
      }
      soft_reboot_device = 0;
      wait_for_device_to_appear = 1;
    }
    if (!wait_for_device_to_appear) die("Unable to open device\n");
    if (!waited) {
      printf_verbose("Waiting for Teensy device...\n");
      printf_verbose(" (hint: press the reset button)\n");
      waited = 1;
    }
    delay(0.25);
  }
  printf_verbose("Found HalfKay Bootloader\n");

  // if we waited for the device, read the hex file again
  // perhaps it changed while we were waiting?
  if (waited) {
    num = read_intel_hex(filename);
    if (num < 0) die("error reading intel hex file \"%s\"", filename);
    printf_verbose("Read \"%s\": %d bytes, %.1f%% usage\n",
       filename, num, (double)num / (double)code_size * 100.0);
  }

  // program the data
  printf_verbose("Programming");
  fflush(stdout);
  for (addr = 0; addr < code_size; addr += block_size) {
    if (!first_block && !ihex_bytes_within_range(addr, addr + block_size - 1)) {
      // don't waste time on blocks that are unused,
      // but always do the first one to erase the chip
      continue;
    }
    if (!first_block && memory_is_blank(addr, block_size)) continue;
    printf_verbose(".");
    if (block_size <= 256 && code_size < 0x10000) {
      buf[0] = addr & 255;
      buf[1] = (addr >> 8) & 255;
      ihex_get_data(addr, block_size, buf + 2);
      write_size = block_size + 2;
    } else if (block_size == 256) {
      buf[0] = (addr >> 8) & 255;
      buf[1] = (addr >> 16) & 255;
      ihex_get_data(addr, block_size, buf + 2);
      write_size = block_size + 2;
    } else if (block_size == 512 || block_size == 1024) {
      buf[0] = addr & 255;
      buf[1] = (addr >> 8) & 255;
      buf[2] = (addr >> 16) & 255;
      memset(buf + 3, 0, 61);
      ihex_get_data(addr, block_size, buf + 64);
      write_size = block_size + 64;
    } else {
      die("Unknown code/block size\n");
    }
    r = teensy_write(buf, write_size, first_block ? 3.0 : 0.25);
    if (!r) die("error writing to Teensy\n");
    first_block = 0;
  }
  printf_verbose("\n");

  // reboot to the user's new code
  if (reboot_after_programming) {
    boot(buf, write_size);
  }
  teensy_close();
  return 0;
}




/****************************************************************/
/*                                                              */
/*             USB Access - libusb (Linux & FreeBSD)            */
/*                                                              */
/****************************************************************/

// http://libusb.sourceforge.net/doc/index.html
#include <usb.h>

usb_dev_handle * open_usb_device(int vid, int pid)
{
  struct usb_bus *bus;
  struct usb_device *dev;
  usb_dev_handle *h;
  char buf[128];
  int r;

  usb_init();
  usb_find_busses();
  usb_find_devices();
  //printf_verbose("\nSearching for USB device:\n");
  for (bus = usb_get_busses(); bus; bus = bus->next) {
    for (dev = bus->devices; dev; dev = dev->next) {
      //printf_verbose("bus \"%s\", device \"%s\" vid=%04X, pid=%04X\n",
      //  bus->dirname, dev->filename,
      //  dev->descriptor.idVendor,
      //  dev->descriptor.idProduct
      //);
      if (dev->descriptor.idVendor != vid) continue;
      if (dev->descriptor.idProduct != pid) continue;
      h = usb_open(dev);
      if (!h) {
        printf_verbose("Found device but unable to open\n");
        continue;
      }
      #ifdef LIBUSB_HAS_GET_DRIVER_NP
      r = usb_get_driver_np(h, 0, buf, sizeof(buf));
      if (r >= 0) {
        r = usb_detach_kernel_driver_np(h, 0);
        if (r < 0) {
          usb_close(h);
          printf_verbose("Device is in use by \"%s\" driver\n", buf);
          continue;
        }
      }
      #endif
      // Mac OS-X - removing this call to usb_claim_interface() might allow
      // this to work, even though it is a clear misuse of the libusb API.
      // normally Apple's IOKit should be used on Mac OS-X
      #if !defined(MACOSX)
      r = usb_claim_interface(h, 0);
      if (r < 0) {
        usb_close(h);
        printf_verbose("Unable to claim interface, check USB permissions\n");
        continue;
      }
      #endif

      return h;
    }
  }
  return NULL;
}

static usb_dev_handle *libusb_teensy_handle = NULL;

int teensy_open(void)
{
  teensy_close();
  libusb_teensy_handle = open_usb_device(0x16C0, 0x0478);
  if (libusb_teensy_handle) return 1;
  return 0;
}

int teensy_write(void *buf, int len, double timeout)
{
  int r;

  if (!libusb_teensy_handle) return 0;
  while (timeout > 0) {
    r = usb_control_msg(libusb_teensy_handle, 0x21, 9, 0x0200, 0,
      (char *)buf, len, (int)(timeout * 1000.0));
    if (r >= 0) return 1;
    fprintf(stderr, "teensy_write, r=%d\n", r);
    delay(0.01);
    timeout -= 0.01;  // TODO: subtract actual elapsed time
  }
  return 0;
}

void teensy_close(void)
{
  if (!libusb_teensy_handle) return;
  usb_release_interface(libusb_teensy_handle, 0);
  usb_close(libusb_teensy_handle);
  libusb_teensy_handle = NULL;
}

int hard_reboot(void)
{
  usb_dev_handle *rebootor;
  int r;

  rebootor = open_usb_device(0x16C0, 0x0477);
  if (!rebootor) return 0;
  r = usb_control_msg(rebootor, 0x21, 9, 0x0200, 0, "reboot", 6, 100);
  usb_release_interface(rebootor, 0);
  usb_close(rebootor);
  if (r < 0) return 0;
  return 1;
}

int soft_reboot(void)
{
  usb_dev_handle *serial_handle = NULL;

  serial_handle = open_usb_device(0x16C0, 0x0483);
  if (!serial_handle) {
    char *error = usb_strerror();
    printf("Error opening USB device: %s\n", error);
    return 0;
  }

  char reboot_command = 134;
  int response = usb_control_msg(serial_handle, 0x21, 0x20, 0, 0, &reboot_command, 1, 10000);

  usb_release_interface(serial_handle, 0);
  usb_close(serial_handle);

  if (response < 0) {
    char *error = usb_strerror();
    printf("Unable to soft reboot with USB error: %s\n", error);
    return 0;
  }

  return 1;
}




/****************************************************************/
/*                                                              */
/*                     Read Intel Hex File                      */
/*                                                              */
/****************************************************************/

// the maximum flash image size we can support
// chips with larger memory may be used, but only this
// much intel-hex data can be loaded into memory!
#define MAX_MEMORY_SIZE 0x40000

static unsigned char firmware_image[MAX_MEMORY_SIZE];
static unsigned char firmware_mask[MAX_MEMORY_SIZE];
static int end_record_seen=0;
static int byte_count;
static unsigned int extended_addr = 0;
static int parse_hex_line(char *line);

int read_intel_hex(const char *filename)
{
  FILE *fp;
  int i, lineno=0;
  char buf[1024];

  byte_count = 0;
  end_record_seen = 0;
  for (i=0; i<MAX_MEMORY_SIZE; i++) {
    firmware_image[i] = 0xFF;
    firmware_mask[i] = 0;
  }
  extended_addr = 0;

  fp = fopen(filename, "r");
  if (fp == NULL) {
    //printf("Unable to read file %s\n", filename);
    return -1;
  }
  while (!feof(fp)) {
    *buf = '\0';
    if (!fgets(buf, sizeof(buf), fp)) break;
    lineno++;
    if (*buf) {
      if (parse_hex_line(buf) == 0) {
        printf("Warning, HEX parse error line %d\n", lineno);
        return -2;
      }
    }
    if (end_record_seen) break;
    if (feof(stdin)) break;
  }
  fclose(fp);
  return byte_count;
}


/* from ihex.c, at http://www.pjrc.com/tech/8051/pm2_docs/intel-hex.html */

/* parses a line of intel hex code, stores the data in bytes[] */
/* and the beginning address in addr, and returns a 1 if the */
/* line was valid, or a 0 if an error occured.  The variable */
/* num gets the number of bytes that were stored into bytes[] */


int parse_hex_line(char *line)
{
  int addr, code, num;
  int sum, len, cksum, i;
  char *ptr;

  num = 0;
  if (line[0] != ':') return 0;
  if (strlen(line) < 11) return 0;
  ptr = line+1;
  if (!sscanf(ptr, "%02x", &len)) return 0;
  ptr += 2;
  if ((int)strlen(line) < (11 + (len * 2)) ) return 0;
  if (!sscanf(ptr, "%04x", &addr)) return 0;
  ptr += 4;
  /* printf("Line: length=%d Addr=%d\n", len, addr); */
  if (!sscanf(ptr, "%02x", &code)) return 0;
  if (addr + extended_addr + len >= MAX_MEMORY_SIZE) return 0;
  ptr += 2;
  sum = (len & 255) + ((addr >> 8) & 255) + (addr & 255) + (code & 255);
  if (code != 0) {
    if (code == 1) {
      end_record_seen = 1;
      return 1;
    }
    if (code == 2 && len == 2) {
      if (!sscanf(ptr, "%04x", &i)) return 1;
      ptr += 4;
      sum += ((i >> 8) & 255) + (i & 255);
      if (!sscanf(ptr, "%02x", &cksum)) return 1;
      if (((sum & 255) + (cksum & 255)) & 255) return 1;
      extended_addr = i << 4;
      //printf("ext addr = %05X\n", extended_addr);
    }
    if (code == 4 && len == 2) {
      if (!sscanf(ptr, "%04x", &i)) return 1;
      ptr += 4;
      sum += ((i >> 8) & 255) + (i & 255);
      if (!sscanf(ptr, "%02x", &cksum)) return 1;
      if (((sum & 255) + (cksum & 255)) & 255) return 1;
      extended_addr = i << 16;
      //printf("ext addr = %08X\n", extended_addr);
    }
    return 1;  // non-data line
  }
  byte_count += len;
  while (num != len) {
    if (sscanf(ptr, "%02x", &i) != 1) return 0;
    i &= 255;
    firmware_image[addr + extended_addr + num] = i;
    firmware_mask[addr + extended_addr + num] = 1;
    ptr += 2;
    sum += i;
    (num)++;
    if (num >= 256) return 0;
  }
  if (!sscanf(ptr, "%02x", &cksum)) return 0;
  if (((sum & 255) + (cksum & 255)) & 255) return 0; /* checksum error */
  return 1;
}

int ihex_bytes_within_range(int begin, int end)
{
  int i;

  if (begin < 0 || begin >= MAX_MEMORY_SIZE ||
    end < 0 || end >= MAX_MEMORY_SIZE) {
      return 0;
    }
    for (i=begin; i<=end; i++) {
      if (firmware_mask[i]) return 1;
    }
    return 0;
  }

  void ihex_get_data(int addr, int len, unsigned char *bytes)
  {
    int i;

    if (addr < 0 || len < 0 || addr + len >= MAX_MEMORY_SIZE) {
      for (i=0; i<len; i++) {
        bytes[i] = 255;
      }
      return;
    }
    for (i=0; i<len; i++) {
      if (firmware_mask[addr]) {
        bytes[i] = firmware_image[addr];
      } else {
        bytes[i] = 255;
      }
      addr++;
    }
  }

int memory_is_blank(int addr, int block_size)
{
  if (addr < 0 || addr > MAX_MEMORY_SIZE) return 1;

  while (block_size && addr < MAX_MEMORY_SIZE) {
    if (firmware_mask[addr] && firmware_image[addr] != 255) return 0;
    addr++;
    block_size--;
  }
  return 1;
}




/****************************************************************/
/*                                                              */
/*                       Misc Functions                         */
/*                                                              */
/****************************************************************/

int printf_verbose(const char *format, ...)
{
  va_list ap;
  int r;

  va_start(ap, format);
  if (verbose) {
    r = vprintf(format, ap);
    fflush(stdout);
    return r;
  }
  return 0;
}

void delay(double seconds)
{
  #ifdef WIN32
  Sleep(seconds * 1e3 * 2);
  #else
  usleep(seconds * 1e6);
  #endif
}

void die(const char *str, ...)
{
  va_list  ap;

  va_start(ap, str);
  vfprintf(stderr, str, ap);
  fprintf(stderr, "\n");
  exit(1);
}


static const struct {
  const char *name;
  int code_size;
  int block_size;
} MCUs[] = {
  { "at90usb162",    15872,   128 },
  { "atmega32u4",    32256,   128 },
  { "at90usb646",    64512,   256 },
  { "at90usb1286",  130048,   256 },
  { "mkl26z64",      63488,   512 },
  { "mk20dx128",    131072,  1024 },
  { "mk20dx256",    262144,  1024 },
  { "mk66fx1m0",   1048576,  1024 },
  { "mk64fx512",    524288,  1024 },
  { 0, 0, 0 }
};


void list_mcus()
{
  int i;
  printf("Supported MCUs are:\n");
  for(i=0; MCUs[i].name != NULL; i++)
    printf(" - %s\n", MCUs[i].name);
  exit(1);
}


void read_mcu(char *name)
{
  int i;

  if(name == NULL) {
    fprintf(stderr, "No MCU specified.\n");
    list_mcus();
  }

  for(i=0; MCUs[i].name != NULL; i++) {
    if(strcasecmp(name, MCUs[i].name) == 0) {
      code_size  = MCUs[i].code_size;
      block_size = MCUs[i].block_size;
      return;
    }
  }

  fprintf(stderr, "Unknown MCU type \"%s\"\n", name);
  list_mcus();
}

void parse_options(int argc, char **argv)
{
  int opt;
  int opt_idx = 1;

  static const char *opts = "hwrsnbvp:lm:";
  static const struct option long_opts[] = {
    { "help",         no_argument,        NULL, 'h' },
    { "wait",         no_argument,        NULL, 'w' },
    { "hard-reboot",  no_argument,        NULL, 'r' },
    { "soft-reboot",  no_argument,        NULL, 's' },
    { "no-reboot",    no_argument,        NULL, 'n' },
    { "boot-only",    no_argument,        NULL, 'b' },
    { "verbose",      no_argument,        NULL, 'v' },
    { "mcu",          required_argument,  NULL, 'p' },
    { "list-mcus",    no_argument,        NULL, 'l' },
    { 0, 0, 0, 0 }
  };

  while ((opt = getopt_long_only(argc, argv, opts, long_opts, &opt_idx)) != -1) {
    switch (opt) {
      case 'h':
        usage(NULL);
        break;

      case 'w':
        wait_for_device_to_appear = 1;
        break;

      case 'r':
        hard_reboot_device = 1;
        break;

      case 's':
        soft_reboot_device = 1;
        break;

      case 'n':
        reboot_after_programming = 0;
        break;

      case 'b':
        boot_only = 1;
        break;

      case 'v':
        verbose = 1;
        break;

      case 'l':
        list_mcus();
        break;

      case 'm':
        if (strncmp(optarg, "mcu=", 4) == 0) {
          optarg += 4;
        } else {
          fprintf(stderr, "%s: unknown option -- m%s\n", argv[0], optarg);
          break;
        }
        // fall through
      case 'p':
        read_mcu(optarg);
        break;

      default:
        break;
    }
  }

  if (optind < argc) filename = argv[optind];
}

void boot(unsigned char *buf, int write_size)
{
  printf_verbose("Booting\n");
  memset(buf, 0, write_size);
  buf[0] = 0xFF;
  buf[1] = 0xFF;
  buf[2] = 0xFF;
  if (!teensy_write(buf, write_size, 0.25)) die("error booting Teensy\n");
}
