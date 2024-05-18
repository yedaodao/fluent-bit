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

#ifndef FLB_OUT_MQTT_CONFIG_H
#define FLB_OUT_MQTT_CONFIG_H

#include <fluent-bit/flb_output_plugin.h>
#include <fluent-bit/flb_pack.h>

#include <mqtt.h>

struct flb_out_mqtt {
    flb_sds_t client_id;
    flb_sds_t addr;
    flb_sds_t port;
    flb_sds_t topic;
    int qos;
    /* Plugin instance */
    struct flb_output_instance *ins;

    struct mqtt_client *client;

    uint8_t *sendbuf;
    uint8_t *recvbuf;

    int sockfd;

    struct flb_record_accessor *body_ra;
};

struct flb_out_mqtt *flb_out_mqtt_create(struct flb_output_instance *ins,
                                           struct flb_config *config);
int flb_out_mqtt_destroy(struct flb_out_mqtt *ctx);

#endif
