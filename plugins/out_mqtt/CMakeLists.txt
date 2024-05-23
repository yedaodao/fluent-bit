# Fluent Bit MQTT Output plugin
set(src
  mqtt.c)
FLB_PLUGIN(out_mqtt "${src}" "paho-embed-mqtt3cc")
