/* To compile
 * gcc -g -Wall -I../src -I../lib/ -I../include -DSTORAGEDIR=\"/var/lib/bluetooth\" -o sixpair sixpair.c ../src/storage.c ../common/libhelper.a -I../common `pkg-config --libs --cflags glib-2.0 libusb-1.0` -lbluetooth
 */

#include <unistd.h>
#include <stdio.h>
#include <inttypes.h>

#include <sdp.h>
#include <bluetooth/bluetooth.h>
#include <bluetooth/hidp.h>
#include <glib.h>
#include <libusb.h>

#include "storage.h"

/* Vendor and product ID for the Sixaxis PS3 controller */
#define VENDOR 0x054c
#define PRODUCT 0x0268

gboolean option_get_master = TRUE;
char *option_master= NULL;
gboolean option_store_info = TRUE;
const char *option_device = NULL;
gboolean option_quiet = FALSE;

const GOptionEntry options[] = {
	{ "get-master", '\0', 0, G_OPTION_ARG_NONE, &option_get_master, "Get currently set master address", NULL },
	{ "set-master", '\0', 0, G_OPTION_ARG_STRING, &option_master, "Set master address (\"auto\" for automatic)", NULL },
	{ "store-info", '\0', 0, G_OPTION_ARG_NONE, &option_store_info, "Store the HID info into the input database", NULL },
	{ "device", '\0', 0, G_OPTION_ARG_STRING, &option_device, "Only handle one device (default, all supported", NULL },
	{ "quiet", 'q', 0, G_OPTION_ARG_NONE, &option_quiet, "Quieten the output", NULL },
	{ NULL }
};

static gboolean
show_master (libusb_device_handle *devh, int itfnum)
{
	unsigned char msg[8];
	int res;

	res = libusb_control_transfer (devh,
				       LIBUSB_ENDPOINT_IN | LIBUSB_REQUEST_TYPE_CLASS | LIBUSB_RECIPIENT_INTERFACE,
				       0x01, 0x03f5, itfnum,
				       (void*) msg, sizeof(msg),
				       5000);

	if (res < 0) {
		g_warning ("Getting the master Bluetooth address failed");
		return FALSE;
	}
	g_print ("Current Bluetooth master: %02x:%02x:%02x:%02x:%02x:%02x\n",
		 msg[2], msg[3], msg[4], msg[5], msg[6], msg[7]);

	return TRUE;
}

static char *
get_bdaddr (libusb_device_handle *devh, int itfnum)
{
	unsigned char msg[17];
	char *address;
	int res;

	res = libusb_control_transfer (devh,
				       LIBUSB_ENDPOINT_IN | LIBUSB_REQUEST_TYPE_CLASS | LIBUSB_RECIPIENT_INTERFACE,
				       0x01, 0x03f2, itfnum,
				       (void*) msg, sizeof(msg),
				       5000);

	if (res < 0) {
		g_warning ("Getting the device Bluetooth address failed");
		return NULL;
	}

	address = g_strdup_printf ("%02x:%02x:%02x:%02x:%02x:%02x",
				   msg[4], msg[5], msg[6], msg[7], msg[8], msg[9]);

	if (option_quiet == FALSE) {
		g_print ("Device Bluetooth address: %s\n", address);
	}

	return address;
}

static gboolean
set_master_bdaddr (libusb_device_handle *devh, int itfnum, char *host)
{
	unsigned char msg[8];
	int mac[6];
	int res;

	if (sscanf(host, "%x:%x:%x:%x:%x:%x",
		   &mac[0],&mac[1],&mac[2],&mac[3],&mac[4],&mac[5]) != 6) {
		return FALSE;
	}

	msg[0] = 0x01;
	msg[1] = 0x00;
	msg[2] = mac[0];
	msg[3] = mac[1];
	msg[4] = mac[2];
	msg[5] = mac[3];
	msg[6] = mac[4];
	msg[7] = mac[5];

	res = libusb_control_transfer (devh,
				       LIBUSB_ENDPOINT_IN | LIBUSB_REQUEST_TYPE_CLASS | LIBUSB_RECIPIENT_INTERFACE,
				       0x09, 0x03f5, itfnum,
				       (void*) msg, sizeof(msg),
				       5000);

	if (res < 0) {
		g_warning ("Setting the master Bluetooth address failed");
		return FALSE;
	}

	return TRUE;
}

static char *
get_host_bdaddr (void)
{
	FILE *f;
	int mac[6];

	//FIXME use dbus to get the default adapter

	f = popen("hcitool dev", "r");

	if (f == NULL) {
		//FIXME
		return NULL;
	}
	if (fscanf(f, "%*s\n%*s %x:%x:%x:%x:%x:%x",
		   &mac[0],&mac[1],&mac[2],&mac[3],&mac[4],&mac[5]) != 6) {
		//FIXME
		return NULL;
	}

	return g_strdup_printf ("%x:%x:%x:%x:%x:%x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

static int
get_record_info (const struct libusb_interface_descriptor *alt, unsigned int *_len, unsigned int *_country, uint16_t *_version)
{
#if 0
	unsigned char *buf;
	unsigned int size, len, country;
	uint16_t version;
	int l;

	len = 0;
	country = 0;
	version = 0;

	if (!alt->extralen)
		return 0;

	size = alt->extralen;
	buf = alt->extra;
	while (size >= 2 * sizeof(u_int8_t)) {
		if (buf[0] < 2 || buf[1] != USB_DT_HID)
			continue;

		//FIXME that should be "21"
		//g_message ("country: %u", buf[4]);
		//country = buf[4];
		//country = 0x21;
		country = 0;
		version = (buf[3] << 8) + buf[2];

		for (l = 0; l < buf[5]; l++) {
			/* we are just interested in report descriptors*/
			if (buf[6+3*l] != USB_DT_REPORT)
				continue;
			len = buf[7+3*l] | (buf[8+3*l] << 8);
		}
		size -= buf[0];
		buf += buf[0];
	}

	if (len == 0)
		return -1;
	*_len = len;
	*_country = country;
	*_version = version;
#endif
	return 0;
}

static void
fill_req_from_usb (libusb_device *dev, struct hidp_connadd_req *req, void *data, unsigned int len, unsigned int country, uint16_t version)
{
#if 0
	req->vendor = dev->descriptor.idVendor;
	req->product = dev->descriptor.idProduct;
	req->version = version;
	/* req->subclass already set */
	req->country = country;
	/* Default value */
	req->parser = 0x0100;
	/* What are we expecting here? No idea, but we don't seem to need it */
	req->flags = 0;

	req->rd_size = len;
	req->rd_data = data;
#endif
}

static void
store_info (const char *host, const char *device, struct hidp_connadd_req *req)
{
	bdaddr_t dest, src;

	if (str2ba (host, &src) < 0) {
		//FIXME
		return;
	}
	if (str2ba (device, &dest) < 0) {
		//FIXME
		return;
	}

#if 0
	if (store_device_info (&src, &dest, req) < 0)
#endif
		g_message ("store_device_info failed");
}

static int
handle_device (libusb_device *dev, struct libusb_config_descriptor *cfg, int itfnum, const struct libusb_interface_descriptor *alt)
{
	libusb_device_handle *devh;
	int res, retval;

	retval = -1;

	if (libusb_open (dev, &devh) < 0) {
		g_warning ("Can't open device");
		goto bail;
	}
	libusb_detach_kernel_driver (devh, itfnum);

	res = libusb_claim_interface (devh, itfnum);
	if (res < 0) {
		g_warning ("Can't claim interface %d", itfnum);
		goto bail;
	}

	if (option_get_master != FALSE) {
		if (show_master (devh, itfnum) == FALSE)
			goto bail;
		retval = 0;
	}

	if (option_master != NULL) {
		if (strcmp (option_master, "auto") == 0) {
			g_free (option_master);
			option_master = get_host_bdaddr ();
			if (option_master == NULL) {
				g_warning ("Can't get bdaddr from default device");
				retval = -1;
				goto bail;
			}
		}
	} else {
		option_master = get_host_bdaddr ();
		if (option_master == NULL) {
			g_warning ("Can't get bdaddr from default device");
			retval = -1;
			goto bail;
		}
	}

	if (option_store_info != FALSE) {
		unsigned char data[8192];
		struct hidp_connadd_req req;
		unsigned int len, country;
		uint16_t version;
		char *device;

		device = get_bdaddr (devh, itfnum);
		if (device == NULL) {
			retval = -1;
			goto bail;
		}

		if (get_record_info (alt, &len, &country, &version) < 0) {
			g_warning ("Can't get record info");
			retval = -1;
			goto bail;
		}

		if (libusb_control_transfer(devh,
						 LIBUSB_ENDPOINT_IN | LIBUSB_REQUEST_TYPE_CLASS | LIBUSB_RECIPIENT_INTERFACE,
						 LIBUSB_REQUEST_GET_DESCRIPTOR,
						 (LIBUSB_DT_REPORT << 8),
						 itfnum, (void *) &data, len, 5000) < 0) {
			g_warning ("Can't get report descriptor (length: %d, interface: %d)", len, itfnum);
			retval = -1;
			goto bail;
		}

		req.subclass = alt->bInterfaceSubClass;
		fill_req_from_usb (dev, &req, data, len, country, version);

		store_info (option_master, device, &req);

		if (set_master_bdaddr (devh, itfnum, option_master) == FALSE) {
			retval = -1;
			goto bail;
		}

		//FIXME finally, set device as trusted
	}

bail:
	libusb_release_interface (devh, itfnum);
	libusb_attach_kernel_driver(devh, itfnum);
	if (devh != NULL)
		libusb_close (devh);

	return retval;
}

int main (int argc, char **argv)
{
	GOptionContext *context;
	GError *error = NULL;
	libusb_device **list;
	ssize_t num_devices, i;

	context = g_option_context_new ("- Manage Sixaxis PS3 controllers");
	g_option_context_add_main_entries (context, options, NULL);
	if (g_option_context_parse (context, &argc, &argv, &error) == FALSE) {
		g_warning ("Couldn't parse command-line options: %s", error->message);
		return 1;
	}

	/* Check that the passed bdaddr is correct */
	if (option_master != NULL && strcmp (option_master, "auto") != 0) {
		//FIXME check bdaddr
	}

	libusb_init (NULL);

	/* Find device(s) */
	num_devices = libusb_get_device_list (NULL, &list);
	if (num_devices < 0) {
		g_warning ("libusb_get_device_list failed");
		return 1;
	}

	for (i = 0; i < num_devices; i++) {
		struct libusb_config_descriptor *cfg;
		libusb_device *dev = list[i];
		struct libusb_device_descriptor desc;
		guint8 j;

		if (libusb_get_device_descriptor (dev, &desc) < 0) {
			g_warning ("libusb_get_device_descriptor failed");
			continue;
		}

		/* Here we check for the supported devices */
		if (desc.idVendor != VENDOR || desc.idProduct != PRODUCT)
			continue;

		/* Look for the interface number that interests us */
		for (j = 0; j < desc.bNumConfigurations; j++) {
			struct libusb_config_descriptor *config;
			guint8 k;

			libusb_get_config_descriptor (dev, j, &config);

			for (k = 0; k < config->bNumInterfaces; k++) {
				const struct libusb_interface *itf = &config->interface[k];
				int l;

				for (l = 0; l < itf->num_altsetting ; l++) {
					struct libusb_interface_descriptor alt;

					alt = itf->altsetting[l];
					if (alt.bInterfaceClass == 3) {
						handle_device (dev, cfg, l, &alt);
					}
				}
			}
		}
	}

	return 0;
}

