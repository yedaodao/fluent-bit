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

#include "MQTTClient.h"

struct flb_out_mqtt {
    int out_format;
    flb_sds_t format;
    
    flb_sds_t client_id;
    flb_sds_t mqtt_host;
    int mqtt_port;
    flb_sds_t topic;
    int qos;

    flb_sds_t timestamp_key;
    
    /* Plugin instance */
    struct flb_output_instance *ins;

    /* Socket buffer */
    uint8_t *sendbuf;
    uint8_t *recvbuf;

    /* MQTT client */
    struct Network *network;
    struct MQTTClient *client;
};

#endif