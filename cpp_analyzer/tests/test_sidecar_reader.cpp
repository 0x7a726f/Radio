#include "test_framework.h"
#include "sidecar_reader.h"
#include <fstream>
#include <filesystem>

using namespace astro;

void test_json_sidecar_parsing() {
    TEST_CASE("JSON Sidecar Parsing (.json)");

    std::string cs16_path = "test_obs.cs16";
    std::string json_path = "test_obs.json";

    {
        std::ofstream out(json_path);
        out << "{\n"
            << "  \"center_freq_hz\": 1420405751.768,\n"
            << "  \"sample_rate_hz\": 2400000.0,\n"
            << "  \"gain_db\": 32.5,\n"
            << "  \"bias_tee\": true,\n"
            << "  \"lo_offset_khz\": 125.0,\n"
            << "  \"device_name\": \"RTL-SDR v4\",\n"
            << "  \"timestamp\": \"2026-11-12T01:00:00Z\"\n"
            << "}\n";
    }

    ObservationMetadata meta = SidecarReader::parse(cs16_path);

    ASSERT_TRUE(meta.found_sidecar);
    ASSERT_NEAR(meta.center_freq_hz, 1420405751.768, 1e-3);
    ASSERT_NEAR(meta.sample_rate_hz, 2400000.0, 1e-3);
    ASSERT_TRUE(meta.has_gain);
    ASSERT_NEAR(meta.gain_db, 32.5, 1e-3);
    ASSERT_TRUE(meta.bias_tee);
    ASSERT_NEAR(meta.lo_offset_khz, 125.0, 1e-3);
    ASSERT_EQ(meta.device_name, "RTL-SDR v4");

    AstroConfig cfg;
    bool applied = SidecarReader::apply_to_config(meta, cfg);
    ASSERT_TRUE(applied);
    ASSERT_NEAR(cfg.center_freq_hz, 1420405751.768, 1e-3);
    ASSERT_NEAR(cfg.sample_rate_hz, 2400000.0, 1e-3);

    std::filesystem::remove(json_path);
    TEST_PASS();
}

void test_txt_sidecar_parsing() {
    TEST_CASE("TXT Sidecar Parameter Report Parsing (.txt)");

    std::string cs16_path = "test_report.cs16";
    std::string txt_path = "test_report.txt";

    {
        std::ofstream out(txt_path);
        out << "=======================================================\n"
            << "  TVRadio SDR Observation Report\n"
            << "=======================================================\n"
            << "Tuned SDR Center LO: 1,420.500000 MHz\n"
            << "Sampling Rate (Fs):  2.400000 MSPS\n"
            << "Hardware RF Gain:    28.0 dB\n"
            << "Bias-Tee Power:      ON (5V)\n"
            << "LO Offset Shift:     94.25 kHz\n"
            << "Frontend Device:     Airspy R2\n";
    }

    ObservationMetadata meta = SidecarReader::parse(cs16_path);

    ASSERT_TRUE(meta.found_sidecar);
    ASSERT_NEAR(meta.center_freq_hz, 1420.5e6, 1e-1);
    ASSERT_NEAR(meta.sample_rate_hz, 2.4e6, 1e-1);
    ASSERT_TRUE(meta.has_gain);
    ASSERT_NEAR(meta.gain_db, 28.0, 1e-1);
    ASSERT_TRUE(meta.bias_tee);
    ASSERT_NEAR(meta.lo_offset_khz, 94.25, 1e-1);

    std::filesystem::remove(txt_path);
    TEST_PASS();
}

void test_filename_fallback_parsing() {
    TEST_CASE("Filename Fallback Regex Parsing (e.g. 2400000SPS_1420410000Hz)");

    std::string cs16_path = "2026-11-11_17-47-26_2400000SPS_1420410000Hz.cs16";

    ObservationMetadata meta = SidecarReader::parse(cs16_path);

    ASSERT_TRUE(meta.found_sidecar);
    ASSERT_NEAR(meta.center_freq_hz, 1420410000.0, 1e-1);
    ASSERT_NEAR(meta.sample_rate_hz, 2400000.0, 1e-1);

    TEST_PASS();
}

int main() {
    std::cout << "=======================================================\n";
    std::cout << " RUNNING SIDECAR READER TEST SUITE\n";
    std::cout << "=======================================================\n";

    test_json_sidecar_parsing();
    test_txt_sidecar_parsing();
    test_filename_fallback_parsing();

    test::report_summary("Sidecar Reader");
    return test::exit_code();
}
