#!/bin/sh
# Activates the ESP-IDF environment for the given platform and runs idf.py
# against that platform's isolated build directory (build-carrot/build-spinach),
# forwarding all remaining arguments.
#
# Must be sourced, not executed — same as tools/activate_idf.sh (which this
# wraps), and for the same reason: IDF's own activation script only exports
# its environment variables when it detects it's being sourced directly by
# an interactive shell; nested inside an executed script it refuses to run.
# Source this script from the project root, same as tools/activate_idf.sh:
#
#   . tools/build.sh [carrot|spinach] [idf.py args...]
#
# The platform defaults to carrot when omitted — pass it explicitly to build
# spinach instead:
#
# Examples:
#   . tools/build.sh build
#   . tools/build.sh spinach -DUD_NETWORK_CONFIG=config/network-config.prod.json build app-flash monitor

case "$1" in
    carrot|spinach)
        _ud_build_platform="$1"
        shift
        ;;
    *)
        _ud_build_platform="carrot"
        ;;
esac

. tools/activate_idf.sh "$_ud_build_platform"
idf.py -B "build-${_ud_build_platform}" "$@"
_ud_build_status=$?
unset _ud_build_platform

return "$_ud_build_status" 2>/dev/null || exit "$_ud_build_status"
