#!/bin/sh

envsubst < /mosquitto/config/mosquitto.conf.template > /mosquitto/config/mosquitto.conf
exec mosquitto -c /mosquitto/config/mosquitto.conf
