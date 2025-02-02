#pragma once

#include <esp_core_dump.h>
#include <mbedtls/base64.h>

#include <ArduinoJson.h>

#include <kernel/NvsStore.hpp>
#include <kernel/Strings.hpp>

namespace farmhub::kernel {

class CrashManager {
public:
    static void handleCrashReport(JsonObject& json) {
        NvsStore nvs("crash-report");
        reportPreviousCrashIfAny(json, nvs);
        nvs.set("version", farmhubVersion);
    }

private:
    static void reportPreviousCrashIfAny(JsonObject& json, NvsStore& nvs) {
        if (!hasCoreDump()) {
            return;
        }

        std::string crashedFirmwareVersion;
        if (!nvs.get<std::string>("version", crashedFirmwareVersion)) {
            crashedFirmwareVersion = "unknown";
        }

        esp_core_dump_summary_t summary {};
        esp_err_t err = esp_core_dump_get_summary(&summary);
        if (err != ESP_OK) {
            LOGE("Failed to get core dump summary: %s", esp_err_to_name(err));
        } else {
            auto crashJson = json["crash"].to<JsonObject>();
            crashJson["firmware-version"] = crashedFirmwareVersion;
            reportPreviousCrash(crashJson, summary);
        }

        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_core_dump_image_erase());
    }

    static bool hasCoreDump() {
        esp_err_t err = esp_core_dump_image_check();
        switch (err) {
            case ESP_OK:
                return true;
            case ESP_ERR_NOT_FOUND:
                LOGV("No core dump found");
                return false;
            case ESP_ERR_INVALID_SIZE:
                LOGD("Invalid core dump size");
                // Typically, this happens when no core dump has been saved yet
                return false;
            default:
                LOGE("Failed to check for core dump: %s", esp_err_to_name(err));
                return false;
        }
    }

    static void reportPreviousCrash(JsonObject& json, const esp_core_dump_summary_t& summary) {
        auto excCause =
#if __XTENSA__
            summary.ex_info.exc_cause;
#else
            summary.ex_info.mcause;
#endif

        LOGW("Core dump found: task: %s, cause: %ld",
            summary.exc_task, excCause);

        json["dump-version"] = summary.core_dump_version;
        json["elf-sha256"] = std::string((const char*) summary.app_elf_sha256);
        json["task"] = std::string(summary.exc_task);
        json["cause"] = excCause;

        static constexpr size_t PANIC_REASON_SIZE = 256;
        char panicReason[PANIC_REASON_SIZE];
        if (esp_core_dump_get_panic_reason(panicReason, PANIC_REASON_SIZE) == ESP_OK) {
            LOGW("Panic reason: %s", panicReason);
            json["panicReason"] = std::string(panicReason);
        }

#ifdef __XTENSA__
        auto backtraceJson = json["backtrace"].to<JsonObject>();
        if (summary.exc_bt_info.corrupted) {
            LOGE("Backtrace corrupted, depth %lu", summary.exc_bt_info.depth);
            backtraceJson["corrupted"] = true;
        } else {
            auto framesJson = backtraceJson["frames"].to<JsonArray>();
            for (int i = 0; i < summary.exc_bt_info.depth; i++) {
                auto& frame = summary.exc_bt_info.bt[i];
                framesJson.add("0x" + toHexString(frame));
            }
        }
#else
        size_t encodedLen = (summary.exc_bt_info.dump_size + 2) / 3 * 4 + 1;
        unsigned char encoded[encodedLen];

        size_t writtenLen = 0;
        int ret = mbedtls_base64_encode(encoded, sizeof(encoded), &writtenLen, summary.exc_bt_info.stackdump, summary.exc_bt_info.dump_size);

        if (ret == 0) {
            encoded[writtenLen] = '\0';    // Null-terminate the output string
            json["stackdump"] = encoded;
        } else {
            LOGE("Failed to encode stackdump: %d", ret);
        }
#endif
    }
};

}    // namespace farmhub::kernel
