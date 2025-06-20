#pragma once

#include <esp_core_dump.h>
#include <mbedtls/base64.h>

#include <ArduinoJson.h>

#include <NvsStore.hpp>
#include <Strings.hpp>

namespace farmhub::kernel {

class CrashManager {
public:
    static void handleCrashReport(JsonObject& json) {
        NvsStore nvs("crash-report");
        switch (getCoreDumpStatus()) {
            case CoreDumpStatus::NoDump: {
                break;
            }
            case CoreDumpStatus::DumpFound: {
                std::string crashedFirmwareVersion;
                if (!nvs.get<std::string>("version", crashedFirmwareVersion)) {
                    crashedFirmwareVersion = "unknown";
                }
                reportPreviousCrash(json, crashedFirmwareVersion);
                ESP_ERROR_CHECK_WITHOUT_ABORT(esp_core_dump_image_erase());
                break;
            }
            default: {
                ESP_ERROR_CHECK_WITHOUT_ABORT(esp_core_dump_image_erase());
                break;
            }
        }
        nvs.set("version", farmhubVersion);
    }

private:
    static void reportPreviousCrash(JsonObject& json, const std::string& crashedFirmwareVersion) {
        esp_core_dump_summary_t summary {};
        esp_err_t err = esp_core_dump_get_summary(&summary);
        if (err != ESP_OK) {
            LOGE("Failed to get core dump summary: %s", esp_err_to_name(err));
        } else {
            auto crashJson = json["crash"].to<JsonObject>();
            crashJson["firmware-version"] = crashedFirmwareVersion;
            reportPreviousCrash(crashJson, summary);
        }
    }

    enum class CoreDumpStatus : uint8_t {
        NoDump,
        DumpFound,
        DumpInvalid
    };

    static CoreDumpStatus getCoreDumpStatus() {
        esp_err_t err = esp_core_dump_image_check();
        switch (err) {
            case ESP_OK:
                LOGV("Found core dump");
                return CoreDumpStatus::DumpFound;
            case ESP_ERR_NOT_FOUND:
                LOGV("No core dump found");
                return CoreDumpStatus::NoDump;
            case ESP_ERR_INVALID_SIZE:
                LOGD("Invalid core dump size, likely no core dump saved");
                return CoreDumpStatus::NoDump;
            case ESP_ERR_INVALID_CRC:
                LOGE("Invalid core dump CRC, likely corrupted");
                return CoreDumpStatus::DumpInvalid;
            default:
                LOGE("Failed to check for core dump: %s", esp_err_to_name(err));
                return CoreDumpStatus::DumpInvalid;
        }
    }

    static void reportPreviousCrash(JsonObject& json, const esp_core_dump_summary_t& summary) {
        auto excCause =
#if __XTENSA__
            summary.ex_info.exc_cause;
#else
            summary.ex_info.mcause;
#endif

        LOGI("Core dump found: task: %s, cause: %" PRIu32,
            summary.exc_task, excCause);

        json["dump-version"] = summary.core_dump_version;
        json["elf-sha256"] = std::string(reinterpret_cast<const char*>(summary.app_elf_sha256));
        json["task"] = std::string(summary.exc_task);
        json["cause"] = excCause;
        json["cause-description"] = resolveCauseDescription(excCause);
        json["tcb"] = "0x" + toHexString(summary.exc_tcb);
        json["pc"] = "0x" + toHexString(summary.exc_pc);
#if __XTENSA__
        // TODO Add more fields for Xtensa
#else
        // TODO Add more fields for RISC-V
#endif

        static constexpr size_t PANIC_REASON_SIZE = 256;
        char panicReason[PANIC_REASON_SIZE];
        esp_err_t panicReasonGetErr = esp_core_dump_get_panic_reason(panicReason, PANIC_REASON_SIZE);
        switch (panicReasonGetErr) {
            case ESP_OK:
                LOGD("Panic reason: %s", panicReason);
                json["panic-reason"] = std::string(panicReason);
                break;
            case ESP_ERR_NOT_FOUND:
                LOGD("No panic reason found");
                break;
            default:
                LOGI("Failed to get panic reason: %s", esp_err_to_name(panicReasonGetErr));
                json["panic-reason-err"] = esp_err_to_name(panicReasonGetErr);
        }

#ifdef __XTENSA__
        auto backtraceJson = json["backtrace"].to<JsonObject>();

        if (summary.exc_bt_info.corrupted) {
            LOGD("Backtrace corrupted, depth %" PRIu32, summary.exc_bt_info.depth);
            backtraceJson["corrupted"] = true;
        }

        auto framesJson = backtraceJson["frames"].to<JsonArray>();
        for (int i = 0; i < summary.exc_bt_info.depth; i++) {
            const auto& frame = summary.exc_bt_info.bt[i];
            framesJson.add("0x" + toHexString(frame));
        }
#else
        size_t encodedLen = (summary.exc_bt_info.dump_size + 2) / 3 * 4 + 1;
        std::string encoded(encodedLen, '\0');

        size_t writtenLen = 0;
        int ret = mbedtls_base64_encode(
            reinterpret_cast<unsigned char*>(encoded.data()),
            encoded.size(),
            &writtenLen,
            summary.exc_bt_info.stackdump,
            summary.exc_bt_info.dump_size);

        if (ret == 0) {
            // Resize to actual length
            encoded.resize(writtenLen);
            json["stackdump"] = encoded;
        } else {
            LOGE("Failed to encode stackdump: %d", ret);
        }
#endif
    }

    static const char* resolveCauseDescription(uint32_t cause) {
#if __XTENSA__
        constexpr std::array<const char*, 40> regularCauses = {
            "IllegalInstruction",
            "Syscall",
            "InstructionFetchError",
            "LoadStoreError",
            "Level1Interrupt",
            "Alloca",
            "IntegerDivideByZero",
            "PCValue",
            "Privileged",
            "LoadStoreAlignment",
            "res",
            "res",
            "InstrPDAddrError",
            "LoadStorePIFDataError",
            "InstrPIFAddrError",
            "LoadStorePIFAddrError",
            "InstTLBMiss",
            "InstTLBMultiHit",
            "InstFetchPrivilege",
            "res",
            "InstrFetchProhibited",
            "res",
            "res",
            "res",
            "LoadStoreTLBMiss",
            "LoadStoreTLBMultihit",
            "LoadStorePrivilege",
            "res",
            "LoadProhibited",
            "StoreProhibited",
            "res",
            "res",
            "Cp0Dis",
            "Cp1Dis",
            "Cp2Dis",
            "Cp3Dis",
            "Cp4Dis",
            "Cp5Dis",
            "Cp6Dis",
            "Cp7Dis",
        };

        constexpr std::array<const char*, 8> pseudoCauses = {
            "Unknown reason",
            "Unhandled debug exception",
            "Double exception",
            "Unhandled kernel exception",
            "Coprocessor exception",
            "Interrupt wdt timeout on CPU0",
            "Interrupt wdt timeout on CPU1",
            "Cache disabled but cached memory region accessed",
        };

#elif __riscv
        constexpr std::array<const char*, 16> regularCauses = {
            "Instruction address misaligned",
            "Instruction access fault",
            "Illegal instruction",
            "Breakpoint",
            "Load address misaligned",
            "Load access fault",
            "Store address misaligned",
            "Store access fault",
            "Environment call from U-mode",
            "Environment call from S-mode",
            NULL,
            "Environment call from M-mode",
            "Instruction page fault",
            "Load page fault",
            NULL,
            "Store page fault",
        };

        constexpr std::array<const char*, 0> pseudoCauses = {
            // TODO Handle RISC-V pseudo causes
        };
#endif
        // Lookup logic and strings copied from frame_to_panic_info()

        // These come from core.h / panic_arch_fill_info()
        if (cause < regularCauses.size() && regularCauses[cause] != nullptr) {
            return regularCauses[cause];
        }

        if (cause >= 64) {
            // These come from panic_reason.h / panic_soc_fill_info()
            auto pseudoCauseIndex = cause - 64;
            if (pseudoCauseIndex < pseudoCauses.size() && pseudoCauses[pseudoCauseIndex] != nullptr) {
                return pseudoCauses[pseudoCauseIndex];
            }
        }
        return "Unknown reason";
    }
};

}    // namespace farmhub::kernel
