# Fluent Bit MQTT Output plugin
set(src
  mqtt_config.c
  mqtt.c)
FLB_PLUGIN(out_mqtt "${src}" "mqttc")

