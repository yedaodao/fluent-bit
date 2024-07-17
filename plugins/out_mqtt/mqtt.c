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
#include <fluent-bit/flb_random.h>

#include "mqtt.h"

static void init_random_client_id(struct flb_out_mqtt *ctx, int len)
{
    ctx->client_id = flb_sds_create_size(len);
    int random_ret = flb_random_bytes(ctx->client_id, len);
    if (random_ret != 0)
    {
        flb_plg_error(ctx->ins, "Failed to generate random client id");
        return -1;
    }

    int i=0;
    int num;
    char charset[] = "0123456789"
                     "abcdefghijklmnopqrstuvwxyz"
                     "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    int charset_len = strlen(charset);

    while (i < len)
    {
        num = (int)ctx->client_id[i];
        num = num % charset_len;
        ctx->client_id[i] = charset[num];
        i++;
    }
    ctx->client_id[len] = '\0';
}

static int get_mqtt_connect(MQTTClient *client, struct flb_out_mqtt *ctx)
{
    if (client->isconnected)
    {
        return 0;
    }
    if (ctx->client == NULL || ctx->network == NULL)
    {
        return -1;
    }
    MQTTDisconnect(ctx->client);
    NetworkDisconnect(ctx->network);

    int connect_rc = NetworkConnect(ctx->network, ctx->mqtt_host, ctx->mqtt_port);
    if (connect_rc != 0)
    {
        flb_plg_error(ctx->ins, "Failed to connect socket, return code %d, error=%s", connect_rc, strerror(errno));
        return -1;
    }
    MQTTClientInit(ctx->client, ctx->network, 10000, ctx->sendbuf, 16 * 1024, ctx->recvbuf, 1024);

    // MQTTPacket_connectData data;
    MQTTPacket_connectData connect_config_data = MQTTPacket_connectData_initializer;
    connect_config_data.willFlag = 0;
    connect_config_data.MQTTVersion = 3;
    connect_config_data.keepAliveInterval = 300;
    connect_config_data.cleansession = 1;
    if (ctx->client_id == NULL)
    {
        init_random_client_id(ctx, 12);
    }
    connect_config_data.clientID.cstring = ctx->client_id;

    flb_plg_info(ctx->ins, "Connecting to %s:%d\n", ctx->mqtt_host, ctx->mqtt_port);

    int rc = MQTTConnect(ctx->client, &connect_config_data);
    if (rc != SUCCESS)
    {
        flb_plg_error(ctx->ins, "Failed to connect MQTT, return code %d, error=%s", rc, strerror(errno));
        return -1;
    }
    flb_plg_info(ctx->ins, "Connected %d\n", rc);

    return 0;
}

static int produce_message_2_mqtt(struct flb_time *tm, msgpack_object *map,
                                  struct flb_out_mqtt *ctx, struct flb_config *config)
{

    int ret;
    int map_size;
    msgpack_sbuffer mp_sbuf;
    msgpack_packer mp_pck;
    char *out_buf;
    size_t out_size;
    flb_sds_t tmp_str;
    msgpack_object key;
    msgpack_object val;

    if (flb_log_check(FLB_LOG_DEBUG))
        msgpack_object_print(stderr, *map);

    msgpack_sbuffer_init(&mp_sbuf);
    msgpack_packer_init(&mp_pck, &mp_sbuf, msgpack_sbuffer_write);

    // make room for timestamp
    map_size = map->via.map.size + 1;
    msgpack_pack_map(&mp_pck, map_size);
    /* Pack timestamp */
    msgpack_pack_str(&mp_pck, flb_sds_len(ctx->timestamp_key));
    msgpack_pack_str_body(&mp_pck, ctx->timestamp_key, flb_sds_len(ctx->timestamp_key));
    msgpack_pack_double(&mp_pck, flb_time_to_double(tm));

    /* Pack the rest of the map */
    for (int i = 0; i < map->via.map.size; i++)
    {
        key = map->via.map.ptr[i].key;
        val = map->via.map.ptr[i].val;

        msgpack_pack_object(&mp_pck, key);
        msgpack_pack_object(&mp_pck, val);
    }

    if (ctx->out_format == FLB_PACK_JSON_FORMAT_JSON)
    {
        tmp_str = flb_msgpack_raw_to_json_sds(mp_sbuf.data, mp_sbuf.size);
        if (!tmp_str)
        {
            flb_plg_error(ctx->ins, "error encoding to JSON error=%s", strerror(errno));
            msgpack_sbuffer_destroy(&mp_sbuf);
            return -1;
        }
        out_buf = tmp_str;
        out_size = flb_sds_len(out_buf);
    }
    else
    {
        out_buf = mp_sbuf.data;
        out_size = mp_sbuf.size;
    }

    MQTTMessage message;
    message.qos = 0;
    message.retained = 0;
    message.payload = out_buf;
    message.payloadlen = out_size;
    ret = MQTTPublish(ctx->client, ctx->topic, &message);

    if (ctx->out_format == FLB_PACK_JSON_FORMAT_JSON)
    {
        flb_sds_destroy(tmp_str);
    }
    msgpack_sbuffer_destroy(&mp_sbuf);

    if (ret != 0)
    {
        flb_plg_warn(ctx->ins, "Publish failed, return code=%d, error=%s", ret, strerror(errno));
        return -1;
    }
    return 1;
}

static int flb_out_mqtt_destroy(struct flb_out_mqtt *ctx)
{
    if (!ctx)
    {
        return -1;
    }
    if (ctx->client_id)
    {
        flb_sds_destroy(ctx->client_id);
    }
    if (ctx->timestamp_key)
    {
        flb_sds_destroy(ctx->timestamp_key);
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
        return FLB_ERROR;
    }
    ctx->ins = ins;

    ret = flb_output_config_map_set(ins, (void *)ctx);
    if (ret == -1)
    {
        flb_free(ctx);
        return FLB_ERROR;
    }

    ctx->out_format = FLB_PACK_JSON_FORMAT_NONE;
    tmp = flb_output_get_property("format", ins);
    if (tmp)
    {
        if (strcasecmp(tmp, "json") == 0)
        {
            ctx->out_format = FLB_PACK_JSON_FORMAT_JSON;
        }
        else if (strcasecmp(tmp, "msgpack") == 0)
        {
            ctx->out_format = FLB_PACK_JSON_FORMAT_NONE;
        }
        else
        {
            flb_plg_error(ctx->ins, "unrecognized 'format' option. "
                                    "Using 'json'");
            return FLB_ERROR;
        }
    }

    ctx->timestamp_key = flb_sds_create("@timestamp");

    ctx->sendbuf = flb_calloc(16 * 1024, sizeof(uint8_t));
    ctx->recvbuf = flb_calloc(1024, sizeof(uint8_t));

    ctx->network = flb_calloc(1, sizeof(struct Network));
    ctx->client = flb_calloc(1, sizeof(struct MQTTClient));
    NetworkInit(ctx->network);

    ret = get_mqtt_connect(ctx->client, ctx);
    if (ret != 0)
    {
        flb_plg_error(ctx->ins, "Failed to get MQTT connnection, return code %d, error=%s", ret, strerror(errno));
        return FLB_ERROR;
    }

    /* Export context */
    flb_output_set_context(ins, ctx);

    return FLB_OK;
}

static void cb_mqtt_flush(struct flb_event_chunk *event_chunk,
                          struct flb_output_flush *out_flush,
                          struct flb_input_instance *i_ins,
                          void *out_context,
                          struct flb_config *config)
{
    int ret;
    struct flb_out_mqtt *ctx = out_context;
    struct flb_log_event_decoder log_decoder;
    struct flb_log_event log_event;

    if (!MQTTIsConnected(ctx->client))
    {
        flb_plg_warn(ctx->ins, "MQTT client is not connected, reconnecting");
        ret = get_mqtt_connect(ctx->client, ctx);
        if (ret != 0)
        {
            flb_plg_error(ctx->ins, "Failed to get MQTT connnection, return code %d, error=%s", ret, strerror(errno));
        }

        FLB_OUTPUT_RETURN(FLB_RETRY);
    }

    ret = flb_log_event_decoder_init(&log_decoder,
                                     (char *)event_chunk->data,
                                     event_chunk->size);
    if (ret != FLB_EVENT_DECODER_SUCCESS)
    {
        flb_plg_error(ctx->ins,
                      "Log event decoder initialization error : %d", ret);

        FLB_OUTPUT_RETURN(FLB_RETRY);
    }

    while ((ret = flb_log_event_decoder_next(
                &log_decoder,
                &log_event)) == FLB_EVENT_DECODER_SUCCESS)
    {
        ret = produce_message_2_mqtt(&log_event.timestamp, log_event.body, ctx, config);
        if (ret != FLB_OK)
        {
            flb_log_event_decoder_destroy(&log_decoder);

            FLB_OUTPUT_RETURN(ret);
        }
    }

    flb_log_event_decoder_destroy(&log_decoder);

    FLB_OUTPUT_RETURN(FLB_OK);
}

static int cb_mqtt_exit(void *data, struct flb_config *config)
{
    struct flb_out_mqtt *ctx = data;
    MQTTDisconnect(ctx->client);
    NetworkDisconnect(ctx->network);

    flb_out_mqtt_destroy(ctx);
    return 0;
}

static struct flb_config_map config_map[] = {
    {FLB_CONFIG_MAP_STR, "format", "json",
     0, FLB_TRUE, offsetof(struct flb_out_mqtt, format),
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
    {FLB_CONFIG_MAP_STR, "topic", (char *)NULL,
     0, FLB_TRUE, offsetof(struct flb_out_mqtt, topic),
     "mqtt topic"},
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