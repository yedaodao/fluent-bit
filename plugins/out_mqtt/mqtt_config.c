/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

/*  Fluent Bit
 *  ==========
 *  Copyright (C) 2015-2024 The Fluent Bit Authors
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

#include <fluent-bit/flb_info.h>
#include <fluent-bit/flb_output.h>
#include <fluent-bit/flb_mem.h>
#include <fluent-bit/flb_kv.h>
#include <fluent-bit/flb_utils.h>

#include "mqtt_config.h"
#include "posix_sockets.h"

void publish_callback(void **unused, struct mqtt_response_publish *published)
{
}

struct flb_out_mqtt *flb_out_mqtt_create(struct flb_output_instance *ins,
                                         struct flb_config *config)
{
    int ret;
    struct flb_out_mqtt *ctx;

    /* Configuration context */
    ctx = flb_calloc(1, sizeof(struct flb_out_mqtt));
    if (!ctx)
    {
        flb_errno();
        return NULL;
    }
    ctx->ins = ins;

    ret = flb_output_config_map_set(ins, (void *)ctx);
    if (ret == -1)
    {
        flb_plg_error(ins, "unable to load configuration.");
        flb_free(ctx);

        return NULL;
    }

    ctx->sockfd = open_nb_socket(ctx->addr, ctx->port);
    if (ctx->sockfd == -1)
    {
        flb_plg_error(ins, "could not open socket");
        return NULL;
    }

    ctx->sendbuf = flb_calloc(16 * 1024, sizeof(uint8_t));
    ctx->recvbuf = flb_calloc(1024, sizeof(uint8_t));

    ctx->client = flb_calloc(1, sizeof(struct mqtt_client));
    if (!ctx->client)
    {
        flb_errno();
        return NULL;
    }
    mqtt_init(ctx->client, ctx->sockfd, ctx->sendbuf, 16 * 1024, ctx->recvbuf, 1024, publish_callback);

    return ctx;
}

int flb_out_mqtt_destroy(struct flb_out_mqtt *ctx)
{
    // TODO MQTT CLIENT
    if (!ctx)
    {
        return -1;
    }
    if (ctx->client)
    {
        flb_free(ctx->client);
    }
    if (ctx->addr)
    {
        flb_free(ctx->addr);
    }
    if (ctx->port)
    {
        flb_free(ctx->port);
    }
    if (ctx->topic)
    {
        flb_free(ctx->topic);
    }
    if (ctx->sendbuf)
    {
        flb_free(ctx->sendbuf);
    }
    if (ctx->recvbuf)
    {
        flb_free(ctx->recvbuf);
    }
    if (ctx->sockfd)
    {
        close(ctx->sockfd);
    }
    if (ctx->client)
    {
        flb_free(ctx->client);
    }
    
    flb_free(ctx);
    return 0;
}