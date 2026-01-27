#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <malloc.h>
#include <stdlib.h>
#include <unistd.h>
#include <poll.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/eventfd.h>


#include <linux/usb/functionfs.h>

#define htole32(x) (x)
#define htole16(x) (x)

static const struct {
	struct usb_functionfs_descs_head_v2 header;
	__le32 fs_count;
	__le32 hs_count;
	struct {
		struct usb_interface_descriptor intf;
		struct usb_endpoint_descriptor_no_audio bulk_in;
		//struct usb_endpoint_descriptor_no_audio iso_in;
		//struct usb_endpoint_descriptor_no_audio bulk_out;
	} __attribute__ ((__packed__)) fs_descs, hs_descs;
} __attribute__ ((__packed__)) descriptors = {
	.header = {
		.magic = htole32(FUNCTIONFS_DESCRIPTORS_MAGIC_V2),
		.flags = htole32(FUNCTIONFS_HAS_FS_DESC |
				     FUNCTIONFS_HAS_HS_DESC),
		.length = htole32(sizeof(descriptors)),
	},
	.fs_count = htole32(2),
	.fs_descs = {
		.intf = {
			.bLength = sizeof(descriptors.fs_descs.intf),
			.bDescriptorType = USB_DT_INTERFACE,
			.bNumEndpoints = 2,
			.bInterfaceClass = USB_CLASS_VENDOR_SPEC,
			.iInterface = 1,
		},
		.bulk_in = {
			.bLength = sizeof(descriptors.fs_descs.bulk_in),
			.bDescriptorType = USB_DT_ENDPOINT,
			.bEndpointAddress = 1 | USB_DIR_IN,
			.bmAttributes = USB_ENDPOINT_XFER_BULK,
		},
		/*.iso_in = {
			.bLength = sizeof(descriptors.fs_descs.iso_in),
			.bDescriptorType = USB_DT_ENDPOINT,
			.bEndpointAddress = 2 | USB_DIR_IN,
			.bmAttributes = USB_ENDPOINT_XFER_ISOC | USB_ENDPOINT_SYNC_ASYNC,
		},*/
		/*.bulk_out = {
			.bLength = sizeof(descriptors.fs_descs.bulk_out),
			.bDescriptorType = USB_DT_ENDPOINT,
			.bEndpointAddress = 2 | USB_DIR_OUT,
			.bmAttributes = USB_ENDPOINT_XFER_BULK,
		},*/
	},
	.hs_count = htole32(2),
	.hs_descs = {
		.intf = {
			.bLength = sizeof(descriptors.hs_descs.intf),
			.bDescriptorType = USB_DT_INTERFACE,
			.bNumEndpoints = 2,
			.bInterfaceClass = USB_CLASS_VENDOR_SPEC,
			.iInterface = 1,
		},
		.bulk_in = {
			.bLength = sizeof(descriptors.hs_descs.bulk_in),
			.bDescriptorType = USB_DT_ENDPOINT,
			.bEndpointAddress = 1 | USB_DIR_IN,
			.bmAttributes = USB_ENDPOINT_XFER_BULK,
			.wMaxPacketSize = htole16(512)
		},
		/*.iso_in = {
			.bLength = sizeof(descriptors.hs_descs.iso_in),
			.bDescriptorType = USB_DT_ENDPOINT,
			.bEndpointAddress = 2 | USB_DIR_IN,
			.bmAttributes = USB_ENDPOINT_XFER_ISOC | USB_ENDPOINT_SYNC_ASYNC,
			.wMaxPacketSize = htole16(1023),
			.bInterval=2
		},*/
		/*.bulk_out = {
			.bLength = sizeof(descriptors.hs_descs.bulk_out),
			.bDescriptorType = USB_DT_ENDPOINT,
			.bEndpointAddress = 2 | USB_DIR_OUT,
			.bmAttributes = USB_ENDPOINT_XFER_BULK,
			.wMaxPacketSize = htole16(512),
		},*/
	},
};

#define STR_INTERFACE "Testing things"

static const struct {
	struct usb_functionfs_strings_head header;
	struct {
		__le16 code;
		const char str1[sizeof(STR_INTERFACE)];
	} __attribute__ ((__packed__)) lang0;
} __attribute__ ((__packed__)) strings = {
	.header = {
		.magic = htole32(FUNCTIONFS_STRINGS_MAGIC),
		.length = htole32(sizeof(strings)),
		.str_count = htole32(1),
		.lang_count = htole32(1),
	},
	.lang0 = {
		htole16(0x0409), /* en-us */
		STR_INTERFACE,
	},
};

int main(int argc, char const *argv[]){
	int ep0=open("/dev/ffs-usb0/ep0", O_RDWR);
	if(ep0<0){
		perror("open");
		return 1;
	}
	if(write(ep0, &descriptors, sizeof(descriptors))<0){
		perror("write descriptors");
		return 1;
	}
	if(write(ep0, &strings, sizeof(strings))<0){
		perror("write strings");
		return 1;
	}
	//close(ep0);
	printf("Done!");
	while(1){
		sleep(100);
	}
	return 0;
}