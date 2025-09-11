#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <functional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <utils/scheduling/MoistureKalmanFilter.hpp>

#include <FakeLog.hpp>

using Catch::Approx;

namespace farmhub::utils::scheduling {

extern const uint8_t _binary_idle_moisture_data_csv_start[] asm("_binary_idle_moisture_data_csv_start");
extern const uint8_t _binary_idle_moisture_data_csv_end[] asm("_binary_idle_moisture_data_csv_end");

struct DataPoint {
    std::string time;
    double volume;
    double moisture;
    double temperature;
};

inline double toDouble(const std::string& s) {
    if (s.empty()) {
        return 0.0;
    }
    return std::stod(s);
}

// Callback style parser: calls `onRow(dp)` for each parsed line
void parseCsv(const uint8_t* start, const uint8_t* end, std::function<void(const DataPoint&)> onRow, size_t chunkSize = 4 * 1024) {
    std::string carry;    // keep leftover from previous chunk (partial line)

    size_t totalSize = end - start;
    for (size_t offset = 0; offset < totalSize; offset += chunkSize) {
        size_t len = std::min(chunkSize, totalSize - offset);
        std::string_view chunk(reinterpret_cast<const char*>(start + offset), len);

        std::string buf = std::move(carry);
        buf.append(chunk);

        std::istringstream iss(buf);
        std::string line;

        carry.clear();

        while (std::getline(iss, line)) {
            if (!iss.eof() || line.back() == '\n' || line.back() == '\r') {
                // complete line
                if (line.rfind("time,", 0) == 0)
                    continue;    // skip header
                if (line.empty())
                    continue;

                std::stringstream ss(line);
                std::string field;
                DataPoint dp;

                if (!std::getline(ss, dp.time, ','))
                    continue;

                if (std::getline(ss, field, ',')) {
                    if (!field.empty() && field.front() == '"')
                        field = field.substr(1, field.size() - 2);
                    dp.volume = toDouble(field);
                }
                if (std::getline(ss, field, ','))
                    dp.moisture = toDouble(field);
                if (std::getline(ss, field, ','))
                    dp.temperature = toDouble(field);

                onRow(dp);
            } else {
                // incomplete line → save for next chunk
                carry = line;
            }
        }
    }

    // process leftover (if any)
    if (!carry.empty()) {
        std::stringstream ss(carry);
        std::string field;
        DataPoint dp;

        if (std::getline(ss, dp.time, ',') && std::getline(ss, field, ',')) {
            if (!field.empty() && field.front() == '"')
                field = field.substr(1, field.size() - 2);
            dp.volume = toDouble(field);
            if (std::getline(ss, field, ','))
                dp.moisture = toDouble(field);
            if (std::getline(ss, field, ','))
                dp.temperature = toDouble(field);

            onRow(dp);
        }
    }
}

TEST_CASE("Kalman processes input correctly", "[kalman][convergence]") {
    MoistureKalmanFilter filter(
        /*initMoistReal*/ 50.0,
        /*initBeta*/ 0.00,
        /*tempRef*/ 20.0);

    // Noise settings
    const double qMoistIdle = 1e-6;
    const double qBeta = 1e-6;
    const double R = 1.0;

    // Run for a while to converge

    // printf("time,moisture,temperature,real_moisture,beta\n");
    parseCsv(_binary_idle_moisture_data_csv_start,
        _binary_idle_moisture_data_csv_end,
        [&](const DataPoint& dp) {
            filter.update(dp.moisture, dp.temperature, qMoistIdle, qBeta, R);
            // printf("%s,%.2f,%.2f,%.2f,%.2f\n",
            //     dp.time.c_str(), dp.moisture, dp.temperature, filter.getMoistReal(), filter.getBeta());
        });

    // Expect beta close to truth and moistReal close to real level
    REQUIRE(filter.getBeta() == Approx(-2.8).margin(0.05));
    REQUIRE(filter.getMoistReal() == Approx(80.0).margin(0.50));
}

}    // namespace farmhub::utils::scheduling
