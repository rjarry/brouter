// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2023 Robin Jarry

#include "control.h"
#include "dpdk.h"

#include <br_api.h>
#include <br_control.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/queue.h>

static LIST_HEAD(, br_api_handler) handlers;

void br_register_api_handler(struct br_api_handler *handler) {
	const struct br_api_handler *h;

	assert(handler != NULL);
	assert(handler->callback != NULL);
	assert(handler->name != NULL);
	LIST_FOREACH(h, &handlers, entries) {
		assert(h->request_type != handler->request_type);
	}

	LOG(DEBUG, "registered api handler type=0x%08x '%s'", handler->request_type, handler->name);
	LIST_INSERT_HEAD(&handlers, handler, entries);
}

const struct br_api_handler *br_lookup_api_handler(const struct br_api_request *req) {
	const struct br_api_handler *handler;

	LIST_FOREACH(handler, &handlers, entries) {
		if (handler->request_type == req->type)
			return handler;
	}

	return NULL;
}