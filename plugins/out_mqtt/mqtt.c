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

#include <fluent-bit/flb_output_plugin.h>
#include <fluent-bit/flb_time.h>
#include <fluent-bit/flb_pack.h>
#include <fluent-bit/flb_utils.h>
#include <fluent-bit/flb_log_event_decoder.h>

#include "mqtt.h"

int flb_out_mqtt_destroy(struct flb_out_mqtt *ctx)
{
    if (!ctx)
    {
        return -1;
    }
    if (ctx->json_date_key)
    {
        flb_sds_destroy(ctx->json_date_key);
    }
    if (ctx->date_key)
    {
        flb_sds_destroy(ctx->date_key);
    }
    if (ctx->client_id)
    {
        flb_sds_destroy(ctx->client_id);
    }
    if (ctx->mqtt_host)
    {
        flb_sds_destroy(ctx->mqtt_host);
    }
    if (ctx->topic)
    {
        flb_sds_destroy(ctx->topic);
    }
    if (ctx->sendbuf)
    {
        flb_free(ctx->sendbuf);
    }
    if (ctx->recvbuf)
    {
        flb_free(ctx->recvbuf);
    }
    if (ctx->network)
    {
        flb_free(ctx->network);
    }
    if (ctx->client)
    {
        flb_free(ctx->client);
    }

    flb_free(ctx);
    return 0;
}

static int cb_mqtt_init(struct flb_output_instance *ins,
                        struct flb_config *config,
                        void *data)
{
    int ret;
    const char *tmp;

    struct flb_out_mqtt *ctx = flb_calloc(1, sizeof(struct flb_out_mqtt));
    if (!ctx)
    {
        flb_errno();
        return -1;
    }
    ctx->ins = ins;

    ret = flb_output_config_map_set(ins, (void *)ctx);
    if (ret == -1)
    {
        flb_free(ctx);
        return -1;
    }

    ctx->out_format = FLB_PACK_JSON_FORMAT_JSON;

    /* Date key */
    ctx->date_key = ctx->json_date_key;
    tmp = flb_output_get_property("json_date_key", ins);
    if (tmp)
    {
        /* Just check if we have to disable it */
        if (flb_utils_bool(tmp) == FLB_FALSE)
        {
            ctx->date_key = NULL;
        }
    }

    /* Date format for JSON output */
    ctx->json_date_format = FLB_PACK_JSON_DATE_DOUBLE;
    tmp = flb_output_get_property("json_date_format", ins);
    if (tmp)
    {
        ret = flb_pack_to_json_date_type(tmp);
        if (ret == -1)
        {
            flb_plg_error(ctx->ins, "invalid json_date_format '%s'. "
                                    "Using 'double' type",
                          tmp);
        }
        else
        {
            ctx->json_date_format = ret;
        }
    }

    ctx->sendbuf = flb_calloc(16 * 1024, sizeof(uint8_t));
    ctx->recvbuf = flb_calloc(1024, sizeof(uint8_t));

    ctx->network = flb_calloc(1, sizeof(struct Network));
    ctx->client = flb_calloc(1, sizeof(struct MQTTClient));

    NetworkInit(ctx->network);
    int connect_rc = NetworkConnect(ctx->network, ctx->mqtt_host, ctx->mqtt_port);
    if (connect_rc != 0)
    {
        flb_plg_error(ins, "Failed to connect socket, return code %d, error=%s", connect_rc, strerror(errno));
        return NULL;
    }
    MQTTClientInit(ctx->client, ctx->network, 10000, ctx->sendbuf, 16 * 1024, ctx->recvbuf, 1024);

    // MQTTPacket_connectData data;
    MQTTPacket_connectData connect_config_data = MQTTPacket_connectData_initializer;
    connect_config_data.willFlag = 0;
    connect_config_data.MQTTVersion = 3;
    connect_config_data.clientID.cstring = ctx->client_id;
    connect_config_data.keepAliveInterval = 10;
    connect_config_data.cleansession = 1;

    flb_plg_info(ins, "Connecting to %s:%d\n", ctx->mqtt_host, ctx->mqtt_port);

    int rc = MQTTConnect(ctx->client, &connect_config_data);
    if (rc != SUCCESS)
    {
        flb_plg_error(ins, "Failed to connect MQTT, return code %d, error=%s", rc, strerror(errno));
        return NULL;
    }
    flb_plg_info(ins, "Connected %d\n", rc);

    /* Export context */
    flb_output_set_context(ins, ctx);

    return 0;
}

static void cb_mqtt_flush(struct flb_event_chunk *event_chunk,
                          struct flb_output_flush *out_flush,
                          struct flb_input_instance *i_ins,
                          void *out_context,
                          struct flb_config *config)
{
    int ret;
    struct flb_out_mqtt *ctx = out_context;
    flb_sds_t json;

    if (!MQTTIsConnected(ctx->client))
    {
        FLB_OUTPUT_RETURN(FLB_RETRY);
    }

    json = flb_pack_msgpack_to_json_format(event_chunk->data,
                                           event_chunk->size,
                                           ctx->out_format,
                                           ctx->json_date_format,
                                           ctx->date_key);
    MQTTMessage message;
    message.qos = 0;
    message.retained = 0;
    message.payload = json;
    message.payloadlen = flb_sds_len(json);
    ret = MQTTPublish(ctx->client, ctx->topic, &message);
    flb_sds_destroy(json);
    if (ret != SUCCESS)
    {
        flb_plg_warn(ctx->ins, "Publish failed, return code=%d, error=%s", ret, strerror(errno));
        FLB_OUTPUT_RETURN(FLB_RETRY);
    }

    FLB_OUTPUT_RETURN(FLB_OK);
}

static int cb_mqtt_exit(void *data, struct flb_config *config)
{
    struct flb_out_mqtt *ctx = data;

    flb_out_mqtt_destroy(ctx);
    return 0;
}

static struct flb_config_map config_map[] = {
    {FLB_CONFIG_MAP_STR, "json_date_format", NULL,
     0, FLB_FALSE, 0,
     FBL_PACK_JSON_DATE_FORMAT_DESCRIPTION},
    {FLB_CONFIG_MAP_STR, "json_date_key", "date",
     0, FLB_TRUE, offsetof(struct flb_out_mqtt, json_date_key),
     "Specifies the name of the date field in output."},
    {FLB_CONFIG_MAP_STR, "client_id", (char *)NULL,
     0, FLB_TRUE, offsetof(struct flb_out_mqtt, client_id),
     "mqtt clientId"},
    {FLB_CONFIG_MAP_STR, "mqtt_host", (char *)NULL,
     0, FLB_TRUE, offsetof(struct flb_out_mqtt, mqtt_host),
     "mqtt host"},
    {FLB_CONFIG_MAP_INT, "mqtt_port", 0,
     0, FLB_TRUE, offsetof(struct flb_out_mqtt, mqtt_port),
     "mqtt port"},
    {
        FLB_CONFIG_MAP_STR,
        "topic",
        (char *)NULL,
        0,
        FLB_TRUE,
        offsetof(struct flb_out_mqtt, topic),
    },
    {FLB_CONFIG_MAP_INT, "qos", 0,
     0, FLB_TRUE, offsetof(struct flb_out_mqtt, qos),
     "message qos"},
    /* EOF */
    {0}};

struct flb_output_plugin out_mqtt_plugin = {
    .name = "mqtt",
    .description = "MQTT output plugin",
    .cb_init = cb_mqtt_init,
    .cb_flush = cb_mqtt_flush,
    .cb_exit = cb_mqtt_exit,
    .config_map = config_map,
    .workers = 1,
    .flags = 0};