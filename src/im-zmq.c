/*
 * zmq.c
 *
 * ZMQ data interface
 *
 * Copyright © 2017-2021 Deutsches Elektronen-Synchrotron DESY,
 *                       a research centre of the Helmholtz Association.
 *
 * Authors:
 *   2018-2021 Thomas White <taw@physics.org>
 *   2014      Valerio Mariani
 *   2017      Stijn de Graaf
 *
 * This file is part of CrystFEL.
 *
 * CrystFEL is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * CrystFEL is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with CrystFEL.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <assert.h>
#include <unistd.h>
#include <zmq.h>

#include <image.h>
#include <utils.h>

#include "im-zmq.h"

#include "datatemplate_priv.h"


struct im_zmq
{
	void *ctx;
	void *socket;
	zmq_msg_t msg;
	const char *request_str;
};


struct im_zmq *im_zmq_connect(const char *zmq_address,
                              char **subscriptions,
                              int n_subscriptions,
                              const char *zmq_request)
{
	struct im_zmq *z;
	int i;

	z = malloc(sizeof(struct im_zmq));
	if ( z == NULL ) return NULL;

	z->ctx = zmq_ctx_new();
	if ( z->ctx == NULL ) {
		free(z);
		return NULL;
	}

	if ( zmq_request == NULL ) {
		STATUS("Connecting ZMQ subscriber to '%s'\n", zmq_address);
		z->socket = zmq_socket(z->ctx, ZMQ_SUB);
	} else {
		STATUS("Connecting ZMQ requester to '%s'\n", zmq_address);
		z->socket = zmq_socket(z->ctx, ZMQ_REQ);
	}
	if ( z->socket == NULL ) {
		free(z);
		return NULL;
	}

	if ( zmq_connect(z->socket, zmq_address) == -1 ) {
		ERROR("ZMQ connection failed: %s\n", zmq_strerror(errno));
		free(z);
		return NULL;
	}

	if ( zmq_request == NULL ) {

		/* SUB mode */
		if ( n_subscriptions == 0 ) {
			ERROR("WARNING: No ZeroMQ subscriptions.  You should "
			      "probably try again with --zmq-subscribe.\n");
		}
		for ( i=0; i<n_subscriptions; i++ ) {
			STATUS("Subscribing to '%s'\n", subscriptions[i]);
			if ( zmq_setsockopt(z->socket, ZMQ_SUBSCRIBE,
			                    subscriptions[i],
			                    strlen(subscriptions[i])) )
			{
				ERROR("ZMQ subscription failed: %s\n",
				      zmq_strerror(errno));
				return NULL;
			}
		}

		z->request_str = NULL;

	} else {

		/* REQ mode */
		z->request_str = zmq_request;

	}

	return z;
}


void *im_zmq_fetch(struct im_zmq *z, size_t *pdata_size)
{
	int msg_size;
	void *data_copy;

	if ( z->request_str != NULL ) {

		/* Send the request */
		if ( zmq_send(z->socket, z->request_str,
		              strlen(z->request_str), 0) == -1 )
		{
			ERROR("ZMQ message send failed: %s\n",
			      zmq_strerror(errno));
			return NULL;
		}
	}

	/* Receive message */
	zmq_msg_init(&z->msg);
	msg_size = zmq_msg_recv(&z->msg, z->socket, 0);
	if ( msg_size == -1 ) {
		ERROR("ZMQ recieve failed: %s\n", zmq_strerror(errno));
		zmq_msg_close(&z->msg);
		return NULL;
	}

	data_copy = malloc(msg_size);
	if ( data_copy == NULL ) return NULL;
	memcpy(data_copy, zmq_msg_data(&z->msg), msg_size);

	zmq_msg_close(&z->msg);
	*pdata_size = msg_size;
	return data_copy;
}


void im_zmq_shutdown(struct im_zmq *z)
{
	if ( z == NULL ) return;
	zmq_close(z->socket);
	zmq_ctx_destroy(z->ctx);
}
