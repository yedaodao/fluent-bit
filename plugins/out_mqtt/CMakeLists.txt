# Fluent Bit MQTT Output plugin
set(src
  mqtt_config.c
  mqtt.c
  posix_sockets.c)
FLB_PLUGIN(out_mqtt "${src}" "mqttc")

