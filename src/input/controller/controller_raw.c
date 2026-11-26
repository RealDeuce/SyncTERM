/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_controller_config.h"
#if ST_CONTROLLER_HAVE_LINUX_HIDRAW
 #define _POSIX_C_SOURCE 200809L
#endif
#include <sys/types.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#if ST_CONTROLLER_HAVE_LINUX_HIDRAW
 #include <linux/hidraw.h>
 #include <linux/input.h>
#else
 #include <dev/usb/usb_ioctl.h>
#endif

#include "controller_internal.h"
#include "st_time.h"

struct node {
	struct node           *next;
	char                   path[64];
	int                    fd;
	bool                   seen;
	struct pad_collections collections;
	uint8_t               *report;
};

bool
pad_native_init(struct st_controllers *hub)
{
	(void)hub;
	return true;
}

static void
close_node(struct node *node)
{
	pad_detach_collections(&node->collections);
	close(node->fd);
	free(node->report);
	free(node);
}

static bool
attach_node(struct st_controllers *hub, struct node *node)
{
#if ST_CONTROLLER_HAVE_LINUX_HIDRAW
	struct hidraw_devinfo           info = {0};
	struct hidraw_report_descriptor desc = {0};
	int                             size = 0;
	if (ioctl(node->fd, HIDIOCGRAWINFO, &info) < 0 || info.bustype != BUS_USB
	    || ioctl(node->fd, HIDIOCGRDESCSIZE, &size) < 0 || size <= 0 || (size_t)size > sizeof desc.value)
		return false;
	desc.size = (uint32_t)size;
	if (ioctl(node->fd, HIDIOCGRDESC, &desc) < 0 || desc.size > sizeof desc.value)
		return false;
	char name[128] = {0};
	(void)ioctl(node->fd, HIDIOCGRAWNAME(sizeof name - 1), name);
	return pad_attach_collections(hub, &node->collections, desc.value, desc.size, (uint16_t)info.vendor,
	    (uint16_t)info.product, name);
#else
	struct usb_device_info    info = {0};
	uint8_t                   data[4096];
	struct usb_gen_descriptor desc = {.ugd_data = data, .ugd_maxlen = sizeof data};
	if (ioctl(node->fd, USB_GET_DEVICEINFO, &info) < 0 || (info.udi_bustypeNo && info.udi_bustypeNo != 3)
	    || ioctl(node->fd, USB_GET_REPORT_DESC, &desc) < 0 || !desc.ugd_actlen
	    || desc.ugd_actlen > sizeof data)
		return false;
	info.udi_product[sizeof info.udi_product - 1] = 0;
	return pad_attach_collections(hub, &node->collections, data, desc.ugd_actlen, info.udi_vendorNo,
	    info.udi_productNo, info.udi_product);
#endif
}

static void
scan(struct st_controllers *hub)
{
	DIR *dir = opendir("/dev");
	if (!dir)
		return;
	struct node *nodes = hub->interfaces;
	for (struct node *n = nodes; n; n = n->next)
		n->seen = false;
	struct dirent *entry;
	while ((entry = readdir(dir))) {
		const char *name   = entry->d_name;
		size_t      prefix = !strncmp(name, "uhid", 4) ? 4 : !strncmp(name, "hidraw", 6) ? 6 : 0;
		if (!prefix || !name[prefix] || name[prefix + strspn(name + prefix, "0123456789")])
			continue;
		char path[64];
		int  len = snprintf(path, sizeof path, "/dev/%s", name);
		if (len < 0 || (size_t)len >= sizeof path)
			continue;
		struct node *n = nodes;
		while (n && strcmp(path, n->path))
			n = n->next;
		if (n) {
			n->seen = true;
			continue;
		}
		int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
		if (fd < 0)
			continue;
		n = calloc(1, sizeof(*n));
		if (!n) {
			close(fd);
			continue;
		}
		n->fd = fd;
		strcpy(n->path, path);
		if (!attach_node(hub, n)) {
			close_node(n);
			continue;
		}
		n->report = malloc(n->collections.report_size);
		if (!n->report) {
			close_node(n);
			continue;
		}
		n->seen = true;
		n->next = nodes;
		nodes   = n;
	}
	closedir(dir);
	hub->interfaces = nodes;
}

void
pad_native_poll(struct st_controllers *hub, st_controller_emit emit, void *ctx)
{
	uint64_t now = 0;
	(void)st_time_monotonic_ms(&now);
	if (!hub->next_scan || now >= hub->next_scan) {
		scan(hub);
		hub->next_scan = now + 1000;
	}
	struct node *nodes = hub->interfaces;
	for (struct node **link = &nodes; *link;) {
		struct node *n = *link;
		for (int i = 0; n->seen && i < 64; i++) {
			/* The uhid-compatible API can concatenate reports in a large read. */
			ssize_t size = read(n->fd, n->report, n->collections.report_size);
			if (size <= 0) {
				if (!size || (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR))
					n->seen = false;
				break;
			}
			pad_input_collections(&n->collections, n->report, (size_t)size, emit, ctx);
		}
		if (n->seen)
			link = &n->next;
		else {
			*link = n->next;
			close_node(n);
		}
	}
	hub->interfaces = nodes;
}

void
pad_native_close(struct st_controllers *hub)
{
	while (hub->interfaces) {
		struct node *n  = hub->interfaces;
		hub->interfaces = n->next;
		close_node(n);
	}
}
