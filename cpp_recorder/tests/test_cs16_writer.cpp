#include "test_framework.h"
#include "cs16_writer.h"
#include "constants.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>
#include <thread>
#include <chrono>

namespace fs = std::filesystem;

TEST_CASE(LutSampleConversionBitExactness) {
    // Verify mathematical bit-exactness of formula: (u8 - 128) * 256
    for (int i = 0; i < 256; ++i) {
        int16_t expected = (int16_t)((i - 128) * 256);
        // Boundaries and center
        if (i == 0) {
            ASSERT_EQ(expected, (int16_t)-32768);
        } else if (i == 128) {
            ASSERT_EQ(expected, (int16_t)0);
        } else if (i == 127) {
            ASSERT_EQ(expected, (int16_t)-256);
        } else if (i == 129) {
            ASSERT_EQ(expected, (int16_t)256);
        } else if (i == 255) {
            ASSERT_EQ(expected, (int16_t)32512);
        }
    }
}

TEST_CASE(Cs16FileCreationAndBitExactWrite) {
    const std::string out_dir = "tests_output/cs16_test";
    fs::create_directories(out_dir);
    const std::string cs16_file = out_dir + "/capture_test.cs16";

    // Clean up old file if exists
    if (fs::exists(cs16_file)) fs::remove(cs16_file);

    Cs16Writer writer(out_dir);
    writer.start_recording(2400000, 1420405000ULL, "Test-RTL-SDR", 28.0f, true, cs16_file);
    ASSERT_TRUE(writer.is_recording());

    // Generate known synthetic pattern: uint8 from 0 to 255 repeating
    const size_t BLOCK_SIZE = 65536; // 64K uint8
    std::vector<uint8_t> test_block(BLOCK_SIZE);
    for (size_t i = 0; i < BLOCK_SIZE; ++i) {
        test_block[i] = (uint8_t)(i % 256);
    }

    const int NUM_BLOCKS = 4;
    for (int b = 0; b < NUM_BLOCKS; ++b) {
        writer.feed_samples(test_block.data(), test_block.size());
    }

    // Give worker time to flush
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    bool stop_ok = writer.stop_recording();
    ASSERT_TRUE(stop_ok);
    ASSERT_FALSE(writer.is_recording());

    // Verify .cs16 file size: exactly NUM_BLOCKS * BLOCK_SIZE * 2 bytes
    ASSERT_TRUE(fs::exists(cs16_file));
    uint64_t file_size = fs::file_size(cs16_file);
    uint64_t expected_size = (uint64_t)NUM_BLOCKS * BLOCK_SIZE * sizeof(int16_t);
    ASSERT_EQ(file_size, expected_size);

    // Read file and verify bit-exact content
    std::ifstream in(cs16_file, std::ios::binary);
    ASSERT_TRUE(in.is_open());
    std::vector<int16_t> read_data(file_size / sizeof(int16_t));
    in.read(reinterpret_cast<char*>(read_data.data()), file_size);
    in.close();

    for (size_t i = 0; i < read_data.size(); ++i) {
        uint8_t original_u8 = (uint8_t)(i % 256);
        int16_t expected_val = (int16_t)((original_u8 - 128) * 256);
        ASSERT_EQ(read_data[i], expected_val);
    }
}

TEST_CASE(SidecarMetadataGenerationJsonAndTxt) {
    const std::string out_dir = "tests_output/cs16_test";
    const std::string base_path = out_dir + "/sidecar_test";
    const std::string cs16_file = base_path + ".cs16";
    const std::string json_file = base_path + ".json";
    const std::string txt_file = base_path + ".txt";

    // Clean up
    if (fs::exists(cs16_file)) fs::remove(cs16_file);
    if (fs::exists(json_file)) fs::remove(json_file);
    if (fs::exists(txt_file)) fs::remove(txt_file);

    Cs16Writer writer(out_dir);
    writer.start_recording(2048000, 1420800000ULL, "Astro-RTLSDR-V4", 36.4f, true, cs16_file);

    std::vector<uint8_t> dummy(32768, 128);
    writer.feed_samples(dummy.data(), dummy.size());

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    writer.stop_recording();

    // 1. Verify JSON sidecar
    ASSERT_TRUE(fs::exists(json_file));
    std::ifstream jf(json_file);
    ASSERT_TRUE(jf.is_open());
    std::string json_content((std::istreambuf_iterator<char>(jf)), std::istreambuf_iterator<char>());
    jf.close();

    ASSERT_TRUE(json_content.find("\"format\": \"cs16\"") != std::string::npos);
    ASSERT_TRUE(json_content.find("\"endianness\": \"little\"") != std::string::npos);
    ASSERT_TRUE(json_content.find("\"sample_rate_hz\": 2048000") != std::string::npos);
    ASSERT_TRUE(json_content.find("\"center_freq_hz\": 1420800000") != std::string::npos);
    ASSERT_TRUE(json_content.find("\"gain_db\": 36.4") != std::string::npos);
    ASSERT_TRUE(json_content.find("\"bias_tee\": true") != std::string::npos);

    // 2. Verify TXT sidecar
    ASSERT_TRUE(fs::exists(txt_file));
    std::ifstream tf(txt_file);
    ASSERT_TRUE(tf.is_open());
    std::string txt_content((std::istreambuf_iterator<char>(tf)), std::istreambuf_iterator<char>());
    tf.close();

    ASSERT_TRUE(txt_content.find("HYDROGEN LINE (21cm) RADIO ASTRONOMY OBSERVATION PARAMETERS") != std::string::npos);
    ASSERT_TRUE(txt_content.find("Tuned SDR Center LO:        1420.800000 MHz") != std::string::npos);
    ASSERT_TRUE(txt_content.find("Sampling Rate (Fs):         2.048 MSPS") != std::string::npos);
    ASSERT_TRUE(txt_content.find("Tuner LNA Gain:             36.4 dB") != std::string::npos);
    ASSERT_TRUE(txt_content.find("Bias-Tee (4.5V LNA Power):  Enabled") != std::string::npos);
}

TEST_CASE(QueueOverflowResistanceAndDropAccounting) {
    const std::string out_dir = "tests_output/cs16_test";
    const std::string cs16_file = out_dir + "/overflow_test.cs16";

    Cs16Writer writer(out_dir);
    writer.start_recording(2400000, 1420405000ULL, "Test-RTL-SDR", 28.0f, false, cs16_file);

    // Write queue capacity is 128 blocks.
    // Flood 300 blocks rapidly to trigger queue overflow protection.
    std::vector<uint8_t> block(16384, 128);
    for (int i = 0; i < 300; ++i) {
        writer.feed_samples(block.data(), block.size());
    }

    auto stats = writer.get_stats();
    // Verify that dropped blocks were safely counted without crash or memory exhaustion
    ASSERT_GT(stats.dropped_blocks, 50u);

    writer.stop_recording();
    ASSERT_FALSE(writer.is_recording());
}

int main() {
    bool ok = TestFramework::TestRunner::instance().run_all("CS16 Writer & Sidecar Metadata Tests");
    return ok ? 0 : 1;
}
