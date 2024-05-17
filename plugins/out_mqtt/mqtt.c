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

#include "mqtt_config.h"

static int cb_mqtt_init(struct flb_output_instance *ins,
                        struct flb_config *config,
                        void *data)
{
    struct flb_out_mqtt *ctx;
    ctx = flb_out_mqtt_create(ins, config);
    /* Set global context */
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
    struct flb_log_event_decoder log_decoder;
    struct flb_log_event log_event;

    // TODO METRIC TRACE

    ret = flb_log_event_decoder_init(&log_decoder,
                                     (char *)event_chunk->data,
                                     event_chunk->size);
    if (ret != FLB_EVENT_DECODER_SUCCESS)
    {
        flb_plg_error(ctx->ins,
                      "Log event decoder initialization error : %d", ret);

        FLB_OUTPUT_RETURN(FLB_RETRY);
    }

    /* Iterate the original buffer and perform adjustments */
    while ((ret = flb_log_event_decoder_next(
                    &log_decoder,
                    &log_event)) == FLB_EVENT_DECODER_SUCCESS) {
        ret = produce_message(&log_event,
                              ctx, config);

        if (ret != FLB_OK) {
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

    flb_out_mqtt_destroy(ctx);
    return 0;
}

static struct flb_config_map config_map[] = {
    {FLB_CONFIG_MAP_STR, "addr", (char *)NULL,
     0, FLB_TRUE, offsetof(struct flb_out_mqtt, addr),
     "mqtt addr"},
    {FLB_CONFIG_MAP_STR, "port", (char *)NULL,
     0, FLB_TRUE, offsetof(struct flb_out_mqtt, port),
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

int produce_message(struct flb_log_event *log_event,
                    struct flb_out_mqtt *ctx, struct flb_config *config)
{
    msgpack_sbuffer mp_sbuf;
    msgpack_sbuffer_init(&mp_sbuf);

    flb_sds_t json_msg = flb_msgpack_raw_to_json_sds(mp_sbuf.data, mp_sbuf.size);
    enum MQTTErrors err = mqtt_publish(ctx->client, ctx->topic, json_msg, flb_sds_len(json_msg), ctx->qos);
    msgpack_sbuffer_destroy(&mp_sbuf);
    return 0;
}

struct flb_output_plugin out_mqtt_plugin = {
    .name = "mqtt",
    .description = "MQTT output plugin",
    .cb_init = cb_mqtt_init,
    .cb_flush = cb_mqtt_flush,
    .cb_exit = cb_mqtt_exit,
    .config_map = config_map,
    .flags = 0};