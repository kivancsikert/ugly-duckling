# syntax=docker/dockerfile:1
FROM eclipse-mosquitto:2.0-openssl

# Install envsubst (part of GNU gettext)
RUN apk add --no-cache gettext

COPY mosquitto.conf.template /mosquitto/config/mosquitto.conf.template

COPY entrypoint.sh /entrypoint.sh
RUN chmod +x /entrypoint.sh

ENTRYPOINT ["/entrypoint.sh"]
