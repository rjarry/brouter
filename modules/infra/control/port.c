// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2023 Robin Jarry

#include <br_api.h>
#include <br_control.h>
#include <br_infra_msg.h>

#include <rte_build_config.h>
#include <rte_common.h>
#include <rte_dev.h>
#include <rte_ethdev.h>

#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>
#include <sys/queue.h>

struct port_entry {
	uint16_t port_id;
	char name[64];
	TAILQ_ENTRY(port_entry) entries;
};
static TAILQ_HEAD(, port_entry) port_entries;

static int fill_port_info(struct port_entry *e, struct br_infra_port *port) {
	struct rte_eth_dev_info info;
	int ret;

	memset(port, 0, sizeof(*port));
	port->index = e->port_id;
	memccpy(port->name, e->name, 0, sizeof(port->name));

	if ((ret = rte_eth_dev_info_get(e->port_id, &info)) < 0)
		return ret;
	if ((ret = rte_eth_dev_get_mtu(e->port_id, &port->mtu)) < 0)
		return ret;
	if ((ret = rte_eth_macaddr_get(e->port_id, (void *)&port->mac)) < 0)
		return ret;

	memccpy(port->device, rte_dev_name(info.device), 0, sizeof(port->device));

	return 0;
}

#define return_resp(resp, code, len)                                                               \
	do {                                                                                       \
		resp->status = code;                                                               \
		resp->payload_len = len;                                                           \
		return;                                                                            \
	} while (0)

static void port_add(void *req_payload, struct br_api_response *resp) {
	struct br_infra_port_add_req *req = req_payload;
	struct br_infra_port_add_resp *payload;
	uint16_t port_id = RTE_MAX_ETHPORTS;
	struct rte_dev_iterator iterator;
	struct rte_eth_dev_info info;
	struct port_entry *entry;
	int ret;

	RTE_ETH_FOREACH_MATCHING_DEV(port_id, req->devargs, &iterator) {
		rte_eth_iterator_cleanup(&iterator);
		return_resp(resp, EEXIST, 0);
	}
	TAILQ_FOREACH(entry, &port_entries, entries) {
		if (strcmp(entry->name, req->name) != 0)
			continue;
		return_resp(resp, EEXIST, 0);
	}

	if ((ret = rte_dev_probe(req->devargs)) < 0)
		return_resp(resp, -ret, 0);

	RTE_ETH_FOREACH_MATCHING_DEV(port_id, req->devargs, &iterator) {
		rte_eth_iterator_cleanup(&iterator);
		break;
	}
	if (!rte_eth_dev_is_valid_port(port_id))
		return_resp(resp, ENOENT, 0);

	entry = calloc(1, sizeof(*entry));
	if (entry == NULL) {
		rte_eth_dev_info_get(port_id, &info);
		rte_eth_dev_close(port_id);
		rte_dev_remove(info.device);
		return_resp(resp, ENOMEM, 0);
	}

	entry->port_id = port_id;
	memccpy(entry->name, req->name, 0, sizeof(entry->name));

	TAILQ_INSERT_TAIL(&port_entries, entry, entries);

	payload = PAYLOAD(resp);
	if ((ret = fill_port_info(entry, &payload->port)) < 0)
		return_resp(resp, -ret, 0);

	return_resp(resp, 0, sizeof(*payload));
}

static struct port_entry *find_port(const char *name) {
	struct port_entry *entry;
	TAILQ_FOREACH(entry, &port_entries, entries) {
		if (strcmp(entry->name, name) == 0)
			return entry;
	}
	return NULL;
}

static void port_del(void *req_payload, struct br_api_response *resp) {
	struct br_infra_port_del_req *req = req_payload;
	struct rte_eth_dev_info info;
	struct port_entry *entry;
	int ret;

	TAILQ_FOREACH(entry, &port_entries, entries) {
		if (strcmp(entry->name, req->name) != 0)
			continue;
		break;
	}

	entry = find_port(req->name);
	if (entry == NULL)
		return_resp(resp, ENODEV, 0);

	if ((ret = rte_eth_dev_info_get(entry->port_id, &info)) < 0)
		return_resp(resp, -ret, 0);
	if ((ret = rte_eth_dev_close(entry->port_id)) < 0)
		return_resp(resp, -ret, 0);
	if ((ret = rte_dev_remove(info.device)) < 0)
		return_resp(resp, -ret, 0);

	TAILQ_REMOVE(&port_entries, entry, entries);
	free(entry);

	return_resp(resp, 0, 0);
}

static void port_get(void *req_payload, struct br_api_response *resp) {
	struct br_infra_port_get_req *req = req_payload;
	struct br_infra_port_get_resp *payload;
	struct port_entry *entry;
	int ret;

	entry = find_port(req->name);
	if (entry == NULL)
		return_resp(resp, ENODEV, 0);

	payload = PAYLOAD(resp);
	if ((ret = fill_port_info(entry, &payload->port)) < 0)
		return_resp(resp, -ret, 0);

	return_resp(resp, 0, sizeof(*payload));
}

static void port_list(void *req_payload, struct br_api_response *resp) {
	struct br_infra_port_list_resp *payload;
	struct port_entry *entry;
	int ret;

	(void)req_payload;

	payload = PAYLOAD(resp);
	payload->n_ports = 0;

	TAILQ_FOREACH(entry, &port_entries, entries) {
		struct br_infra_port *port = &payload->ports[payload->n_ports];
		if ((ret = fill_port_info(entry, port)) < 0)
			return_resp(resp, -ret, 0);
		payload->n_ports++;
	}

	return_resp(resp, 0, sizeof(*payload));
}

static struct br_api_handler port_add_handler = {
	.name = "port add",
	.request_type = BR_INFRA_PORT_ADD,
	.callback = port_add,
};
static struct br_api_handler port_del_handler = {
	.name = "port del",
	.request_type = BR_INFRA_PORT_DEL,
	.callback = port_del,
};
static struct br_api_handler port_get_handler = {
	.name = "port get",
	.request_type = BR_INFRA_PORT_GET,
	.callback = port_get,
};
static struct br_api_handler port_list_handler = {
	.name = "port list",
	.request_type = BR_INFRA_PORT_LIST,
	.callback = port_list,
};

RTE_INIT(control_infra_init) {
	TAILQ_INIT(&port_entries);
	br_register_api_handler(&port_add_handler);
	br_register_api_handler(&port_del_handler);
	br_register_api_handler(&port_get_handler);
	br_register_api_handler(&port_list_handler);
}