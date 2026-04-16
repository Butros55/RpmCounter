#include "ambient_light.h"

#include <math.h>

#include "ambient_light_algo.h"
#include "core/config.h"
#include "core/logging.h"
#include "core/utils.h"

#if defined(CONFIG_IDF_TARGET_ESP32S3)
#include <driver/i2c_master.h>

#include "hardware/display_s3.h"
#else
#include <Adafruit_VEML7700.h>
#include <Wire.h>
#endif

namespace
{
    constexpr uint32_t AMBIENT_POLL_INTERVAL_MS = 180;
    constexpr uint32_t AMBIENT_MIN_READ_GAP_MS = 100;
    constexpr uint32_t AMBIENT_RETRY_INTERVAL_MS = 3000;

#if defined(CONFIG_IDF_TARGET_ESP32S3)
    constexpr int S3_LCD_RESET_PIN = 21;
    constexpr int S3_BOARD_I2C_SDA_PIN = 47;
    constexpr int S3_BOARD_I2C_SCL_PIN = 48;
    constexpr uint8_t VEML7700_I2C_ADDR = 0x10;
    constexpr uint8_t VEML7700_REG_ALS_CONFIG = 0x00;
    constexpr uint8_t VEML7700_REG_POWER_SAVE = 0x03;
    constexpr uint8_t VEML7700_REG_ALS_DATA = 0x04;
    constexpr uint8_t VEML7700_REG_WHITE_DATA = 0x05;
    constexpr uint8_t VEML7700_GAIN_1 = 0x00;
    constexpr uint8_t VEML7700_GAIN_2 = 0x01;
    constexpr uint8_t VEML7700_GAIN_1_8 = 0x02;
    constexpr uint8_t VEML7700_GAIN_1_4 = 0x03;
    constexpr uint8_t VEML7700_IT_100MS = 0x00;
    constexpr uint8_t VEML7700_IT_200MS = 0x01;
    constexpr uint8_t VEML7700_IT_400MS = 0x02;
    constexpr uint8_t VEML7700_IT_800MS = 0x03;
    constexpr uint8_t VEML7700_IT_50MS = 0x08;
    constexpr uint8_t VEML7700_IT_25MS = 0x0C;
    constexpr float VEML7700_MAX_RES = 0.0036f;
    constexpr float VEML7700_GAIN_MAX = 2.0f;
    constexpr float VEML7700_IT_MAX = 800.0f;
    constexpr i2c_port_num_t AMBIENT_PRIVATE_I2C_PORT = I2C_NUM_1;

    i2c_master_bus_handle_t g_ownedBus = nullptr;
    i2c_master_dev_handle_t g_sensorDev = nullptr;
    bool g_usingSharedBus = false;
    uint8_t g_vemlGain = VEML7700_GAIN_1_8;
    uint8_t g_vemlIntegration = VEML7700_IT_100MS;
    unsigned long g_lastSensorIoMs = 0;
#else
    TwoWire g_ambientWire(1);
    Adafruit_VEML7700 g_sensor;
#endif

    AmbientLightDebugInfo g_debug;
    bool g_sensorReady = false;
    bool g_filterPrimed = false;
    uint8_t g_consecutiveReadErrors = 0;
    int g_runtimeSdaPin = -1;
    int g_runtimeSclPin = -1;
    unsigned long g_lastPollMs = 0;
    unsigned long g_lastInitAttemptMs = 0;

    AutoBrightnessCurveConfig curveConfig()
    {
        AutoBrightnessCurveConfig config;
        config.manualMax = clampInt(cfg.brightness, 0, 255);
        config.minBrightness = clampInt(cfg.autoBrightnessMin, 0, config.manualMax);
        config.strengthPct = clampInt(cfg.autoBrightnessStrengthPct, 25, 200);
        config.luxMin = clampInt(cfg.autoBrightnessLuxMin, 0, 120000);
        config.luxMax = clampInt(cfg.autoBrightnessLuxMax, config.luxMin + 1, 120000);
        return config;
    }

    bool pinsChanged()
    {
        return g_runtimeSdaPin != cfg.ambientLightSdaPin || g_runtimeSclPin != cfg.ambientLightSclPin;
    }

    void resetRuntimeState()
    {
        g_filterPrimed = false;
        g_consecutiveReadErrors = 0;
        g_debug.rawLux = 0.0f;
        g_debug.filteredLux = 0.0f;
        g_debug.rawAls = 0;
        g_debug.rawWhite = 0;
        g_debug.configReg = 0;
        g_debug.targetBrightness = clampInt(cfg.brightness, 0, 255);
        g_debug.desiredBrightness = clampInt(cfg.brightness, 0, 255);
    }

    bool validLux(float lux)
    {
        return !isnan(lux) && !isinf(lux) && lux >= 0.0f;
    }

    void updateDesiredBrightnessFromLux(bool smoothOutput)
    {
        const AutoBrightnessCurveConfig config = curveConfig();
        const int manualBrightness = config.manualMax;

        if (!cfg.autoBrightnessEnabled || !g_sensorReady || !g_filterPrimed)
        {
            g_debug.targetBrightness = manualBrightness;
            g_debug.desiredBrightness = manualBrightness;
            return;
        }

        const int target = ambientComputeTargetBrightness(g_debug.filteredLux, config);
        g_debug.targetBrightness = target;

        if (!smoothOutput)
        {
            g_debug.desiredBrightness = target;
            return;
        }

        const float responseAlpha = ambientComputeResponseAlpha(cfg.autoBrightnessResponsePct);
        const float brightnessAlpha = responseAlpha < 0.08f ? 0.08f : (responseAlpha * 1.2f);
        const float smoothed = ambientApplySmoothing(
            static_cast<float>(g_debug.desiredBrightness),
            static_cast<float>(target),
            brightnessAlpha > 0.45f ? 0.45f : brightnessAlpha);
        g_debug.desiredBrightness = clampInt(static_cast<int>(lroundf(smoothed)), 0, manualBrightness);
    }

    bool validateBoardPinSelection()
    {
#if defined(CONFIG_IDF_TARGET_ESP32S3)
        if (g_runtimeSdaPin == S3_LCD_RESET_PIN || g_runtimeSclPin == S3_LCD_RESET_PIN)
        {
            g_debug.lastError = F("Waveshare S3: GPIO 47/48 statt 21/22 nutzen");
            g_sensorReady = false;
            g_debug.sensorDetected = false;
            g_debug.busInitialized = false;
            g_debug.deviceResponding = false;
            g_debug.usingSharedBus = false;
            resetRuntimeState();
            LOG_WARN("AMBIENT", "PIN_CONFLICT",
                     String("configured sda=") + g_runtimeSdaPin + " scl=" + g_runtimeSclPin +
                         " but Waveshare S3 board SDA/SCL are GPIO " + S3_BOARD_I2C_SDA_PIN + "/" +
                         S3_BOARD_I2C_SCL_PIN + " and GPIO 21 is AMOLED reset");
            return false;
        }
#endif
        return true;
    }

#if defined(CONFIG_IDF_TARGET_ESP32S3)
    int integrationTimeMs(uint8_t integration)
    {
        switch (integration)
        {
        case VEML7700_IT_25MS:
            return 25;
        case VEML7700_IT_50MS:
            return 50;
        case VEML7700_IT_100MS:
            return 100;
        case VEML7700_IT_200MS:
            return 200;
        case VEML7700_IT_400MS:
            return 400;
        case VEML7700_IT_800MS:
            return 800;
        default:
            return 100;
        }
    }

    float gainValue(uint8_t gain)
    {
        switch (gain)
        {
        case VEML7700_GAIN_1_8:
            return 0.125f;
        case VEML7700_GAIN_1_4:
            return 0.25f;
        case VEML7700_GAIN_1:
            return 1.0f;
        case VEML7700_GAIN_2:
            return 2.0f;
        default:
            return 0.125f;
        }
    }

    float currentResolution()
    {
        return VEML7700_MAX_RES * (VEML7700_IT_MAX / static_cast<float>(integrationTimeMs(g_vemlIntegration))) *
               (VEML7700_GAIN_MAX / gainValue(g_vemlGain));
    }

    uint16_t buildAlsConfigRegister(bool enableSensor)
    {
        uint16_t configReg = 0;
        if (!enableSensor)
        {
            configReg |= 0x0001;
        }
        configReg |= static_cast<uint16_t>(g_vemlIntegration & 0x0F) << 6;
        configReg |= static_cast<uint16_t>(g_vemlGain & 0x03) << 11;
        return configReg;
    }

    void releaseSensorBus()
    {
        if (g_sensorDev)
        {
            if (g_usingSharedBus)
            {
                display_s3_remove_shared_i2c_device(g_sensorDev);
            }
            else
            {
                i2c_master_bus_rm_device(g_sensorDev);
            }
            g_sensorDev = nullptr;
        }

        if (!g_usingSharedBus && g_ownedBus)
        {
            i2c_del_master_bus(g_ownedBus);
            g_ownedBus = nullptr;
        }

        g_usingSharedBus = false;
    }

    bool writeRegister16(uint8_t reg, uint16_t value)
    {
        if (!g_sensorDev)
        {
            return false;
        }

        uint8_t payload[3] = {
            reg,
            static_cast<uint8_t>(value & 0xFF),
            static_cast<uint8_t>((value >> 8) & 0xFF)};
        const esp_err_t err = i2c_master_transmit(g_sensorDev, payload, sizeof(payload), -1);
        return err == ESP_OK;
    }

    bool readRegister16(uint8_t reg, uint16_t &value)
    {
        if (!g_sensorDev)
        {
            return false;
        }

        uint8_t raw[2] = {};
        const esp_err_t err = i2c_master_transmit_receive(g_sensorDev, &reg, 1, raw, sizeof(raw), -1);
        if (err != ESP_OK)
        {
            return false;
        }

        value = static_cast<uint16_t>(raw[0] | (static_cast<uint16_t>(raw[1]) << 8));
        return true;
    }

    bool refreshConfigRegister()
    {
        uint16_t configReg = 0;
        if (!readRegister16(VEML7700_REG_ALS_CONFIG, configReg))
        {
            return false;
        }
        g_debug.configReg = configReg;
        return true;
    }

    void markBusReady(bool usingShared)
    {
        g_debug.busInitialized = true;
        g_debug.usingSharedBus = usingShared;
    }

    bool attachSensorDevice()
    {
        if (display_s3_uses_shared_i2c_pins(g_runtimeSdaPin, g_runtimeSclPin))
        {
            if (!display_s3_add_shared_i2c_device(VEML7700_I2C_ADDR, 100000U, &g_sensorDev))
            {
                g_debug.lastError = F("shared-i2c-bus-failed");
                return false;
            }
            g_usingSharedBus = true;
            markBusReady(true);
            return true;
        }

        i2c_master_bus_config_t bus_cfg = {};
        bus_cfg.clk_source = I2C_CLK_SRC_DEFAULT;
        bus_cfg.i2c_port = AMBIENT_PRIVATE_I2C_PORT;
        bus_cfg.scl_io_num = static_cast<gpio_num_t>(g_runtimeSclPin);
        bus_cfg.sda_io_num = static_cast<gpio_num_t>(g_runtimeSdaPin);
        bus_cfg.glitch_ignore_cnt = 7;
        bus_cfg.trans_queue_depth = 0;
        bus_cfg.flags.enable_internal_pullup = true;

        if (i2c_new_master_bus(&bus_cfg, &g_ownedBus) != ESP_OK)
        {
            g_debug.lastError = F("private-i2c-bus-failed");
            return false;
        }

        i2c_device_config_t dev_cfg = {};
        dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
        dev_cfg.device_address = VEML7700_I2C_ADDR;
        dev_cfg.scl_speed_hz = 100000U;
        dev_cfg.scl_wait_us = 0;
        dev_cfg.flags.disable_ack_check = false;

        if (i2c_master_bus_add_device(g_ownedBus, &dev_cfg, &g_sensorDev) != ESP_OK)
        {
            g_debug.lastError = F("private-i2c-device-failed");
            return false;
        }

        markBusReady(false);
        return true;
    }

    void waitForMeasurement()
    {
        const unsigned long requiredMs = static_cast<unsigned long>(integrationTimeMs(g_vemlIntegration) * 2);
        const unsigned long elapsedMs = (g_lastSensorIoMs > 0) ? (millis() - g_lastSensorIoMs) : requiredMs;
        if (elapsedMs < requiredMs)
        {
            delay(requiredMs - elapsedMs);
        }
    }

    bool setGain(uint8_t gain)
    {
        g_vemlGain = gain;
        if (!writeRegister16(VEML7700_REG_ALS_CONFIG, buildAlsConfigRegister(true)))
        {
            return false;
        }
        g_lastSensorIoMs = millis();
        refreshConfigRegister();
        return true;
    }

    bool setIntegrationTime(uint8_t integration, bool waitOldCycle = true)
    {
        const int flushDelay = waitOldCycle ? integrationTimeMs(g_vemlIntegration) : 0;
        g_vemlIntegration = integration;
        if (!writeRegister16(VEML7700_REG_ALS_CONFIG, buildAlsConfigRegister(true)))
        {
            return false;
        }
        if (flushDelay > 0)
        {
            delay(flushDelay);
        }
        g_lastSensorIoMs = millis();
        refreshConfigRegister();
        return true;
    }

    bool readAlsRaw(bool waitForSample, uint16_t &value)
    {
        if (waitForSample)
        {
            waitForMeasurement();
        }
        const bool ok = readRegister16(VEML7700_REG_ALS_DATA, value);
        if (ok)
        {
            g_lastSensorIoMs = millis();
        }
        return ok;
    }

    bool readWhiteRaw(bool waitForSample, uint16_t &value)
    {
        if (waitForSample)
        {
            waitForMeasurement();
        }
        const bool ok = readRegister16(VEML7700_REG_WHITE_DATA, value);
        if (ok)
        {
            g_lastSensorIoMs = millis();
        }
        return ok;
    }

    float computeLuxFromAls(uint16_t rawAls, bool corrected)
    {
        float lux = currentResolution() * static_cast<float>(rawAls);
        if (corrected)
        {
            lux = (((6.0135e-13f * lux - 9.3924e-9f) * lux + 8.1488e-5f) * lux + 1.0023f) * lux;
        }
        return lux;
    }

    bool readAutoLux(float &luxOut, uint16_t &alsOut, uint16_t &whiteOut)
    {
        const uint8_t gains[] = {VEML7700_GAIN_1_8, VEML7700_GAIN_1_4, VEML7700_GAIN_1, VEML7700_GAIN_2};
        const uint8_t integrationTimes[] = {VEML7700_IT_25MS, VEML7700_IT_50MS, VEML7700_IT_100MS,
                                            VEML7700_IT_200MS, VEML7700_IT_400MS, VEML7700_IT_800MS};

        uint8_t gainIndex = 0;
        uint8_t integrationIndex = 2;
        bool useCorrection = false;

        if (!setGain(gains[gainIndex]) || !setIntegrationTime(integrationTimes[integrationIndex]))
        {
            return false;
        }

        if (!readAlsRaw(true, alsOut))
        {
            return false;
        }

        if (alsOut <= 100)
        {
            while (alsOut <= 100 && !((gainIndex == 3) && (integrationIndex == 5)))
            {
                if (gainIndex < 3)
                {
                    if (!setGain(gains[++gainIndex]))
                    {
                        return false;
                    }
                }
                else if (integrationIndex < 5)
                {
                    if (!setIntegrationTime(integrationTimes[++integrationIndex]))
                    {
                        return false;
                    }
                }

                if (!readAlsRaw(true, alsOut))
                {
                    return false;
                }
            }
        }
        else
        {
            useCorrection = true;
            while (alsOut > 10000 && integrationIndex > 0)
            {
                if (!setIntegrationTime(integrationTimes[--integrationIndex]))
                {
                    return false;
                }
                if (!readAlsRaw(true, alsOut))
                {
                    return false;
                }
            }
        }

        luxOut = computeLuxFromAls(alsOut, useCorrection);
        if (!readWhiteRaw(false, whiteOut))
        {
            whiteOut = 0;
        }
        refreshConfigRegister();
        return true;
    }

    bool configureVemlDefaults()
    {
        g_vemlGain = VEML7700_GAIN_1_8;
        g_vemlIntegration = VEML7700_IT_100MS;
        if (!writeRegister16(VEML7700_REG_ALS_CONFIG, buildAlsConfigRegister(false)))
        {
            return false;
        }
        delay(5);
        if (!writeRegister16(VEML7700_REG_POWER_SAVE, 0x0000))
        {
            return false;
        }
        if (!writeRegister16(VEML7700_REG_ALS_CONFIG, buildAlsConfigRegister(true)))
        {
            return false;
        }
        delay(5);
        g_lastSensorIoMs = millis();
        return refreshConfigRegister();
    }
#endif

    bool configureSensor()
    {
        g_debug.initAttempts++;
        g_debug.lastInitMs = millis();
        g_lastInitAttemptMs = g_debug.lastInitMs;
        g_debug.sdaPin = cfg.ambientLightSdaPin;
        g_debug.sclPin = cfg.ambientLightSclPin;
        g_runtimeSdaPin = cfg.ambientLightSdaPin;
        g_runtimeSclPin = cfg.ambientLightSclPin;
        g_debug.busInitialized = false;
        g_debug.deviceResponding = false;
        g_debug.usingSharedBus = false;
        g_debug.sensorDetected = false;
        g_sensorReady = false;
        resetRuntimeState();

        if (g_runtimeSdaPin < 0 || g_runtimeSdaPin > 48 || g_runtimeSclPin < 0 || g_runtimeSclPin > 48)
        {
            g_debug.lastError = F("invalid-pins");
            LOG_WARN("AMBIENT", "PIN_INVALID",
                     String("sda=") + g_runtimeSdaPin + " scl=" + g_runtimeSclPin);
            return false;
        }

        if (!validateBoardPinSelection())
        {
            return false;
        }

#if defined(CONFIG_IDF_TARGET_ESP32S3)
        releaseSensorBus();

        if (!attachSensorDevice())
        {
            LOG_WARN("AMBIENT", "BUS_INIT_FAIL",
                     String("sda=") + g_runtimeSdaPin + " scl=" + g_runtimeSclPin + " err=" + g_debug.lastError);
            releaseSensorBus();
            return false;
        }

        uint16_t configReg = 0;
        if (!readRegister16(VEML7700_REG_ALS_CONFIG, configReg))
        {
            g_debug.lastError = F("veml7700-not-found");
            LOG_WARN("AMBIENT", "SENSOR_MISSING",
                     String("sda=") + g_runtimeSdaPin + " scl=" + g_runtimeSclPin + " addr=0x10");
            releaseSensorBus();
            return false;
        }

        g_debug.deviceResponding = true;
        g_debug.configReg = configReg;
        if (!configureVemlDefaults())
        {
            g_debug.lastError = F("veml7700-config-failed");
            LOG_WARN("AMBIENT", "CONFIG_FAIL",
                     String("sda=") + g_runtimeSdaPin + " scl=" + g_runtimeSclPin + " addr=0x10");
            releaseSensorBus();
            return false;
        }
#else
        g_ambientWire.begin(static_cast<int>(g_runtimeSdaPin), static_cast<int>(g_runtimeSclPin), 100000U);
        g_debug.busInitialized = true;

        if (!g_sensor.begin(&g_ambientWire))
        {
            g_debug.lastError = F("veml7700-not-found");
            LOG_WARN("AMBIENT", "SENSOR_MISSING",
                     String("sda=") + g_runtimeSdaPin + " scl=" + g_runtimeSclPin);
            return false;
        }

        g_sensor.setGain(VEML7700_GAIN_1_8);
        g_sensor.setIntegrationTime(VEML7700_IT_100MS, false);
        g_sensor.enable(true);
        g_debug.deviceResponding = true;
#endif

        g_sensorReady = true;
        g_consecutiveReadErrors = 0;
        g_debug.sensorDetected = true;
        g_debug.initSuccessCount++;
        g_debug.lastError = "";
        updateDesiredBrightnessFromLux(false);
        LOG_INFO("AMBIENT", "READY",
                 String("veml7700 pins sda=") + g_runtimeSdaPin + " scl=" + g_runtimeSclPin);
        return true;
    }

    void handleReadFailure(const __FlashStringHelper *reason)
    {
        ++g_consecutiveReadErrors;
        g_debug.readErrorCount++;
        g_debug.lastError = reason;
        LOG_WARN("AMBIENT", "READ_FAIL",
                 String("reason=") + String(reason) + " count=" + g_consecutiveReadErrors);
        if (g_consecutiveReadErrors >= 3)
        {
            g_sensorReady = false;
            g_debug.sensorDetected = false;
            g_debug.deviceResponding = false;
            g_debug.lastError = F("sensor-read-lost");
            LOG_WARN("AMBIENT", "DISABLED", "sensor read failed repeatedly, falling back to manual brightness");
#if defined(CONFIG_IDF_TARGET_ESP32S3)
            releaseSensorBus();
#endif
        }
        updateDesiredBrightnessFromLux(false);
    }

    void sampleSensor(bool seedOnly)
    {
        if (!g_sensorReady)
        {
            updateDesiredBrightnessFromLux(false);
            return;
        }

        float lux = 0.0f;
        uint16_t rawAls = 0;
        uint16_t rawWhite = 0;

#if defined(CONFIG_IDF_TARGET_ESP32S3)
        if (!readAutoLux(lux, rawAls, rawWhite))
        {
            handleReadFailure(F("lux-read-failed"));
            return;
        }
#else
        lux = g_sensor.readLux(VEML_LUX_AUTO);
        rawAls = g_sensor.readALS(false);
        rawWhite = g_sensor.readWhite(false);
#endif

        if (!validLux(lux))
        {
            handleReadFailure(F("invalid-lux"));
            return;
        }

        const float clampedLux = lux > 120000.0f ? 120000.0f : lux;
        const float responseAlpha = ambientComputeResponseAlpha(cfg.autoBrightnessResponsePct);
        g_consecutiveReadErrors = 0;
        g_debug.readCount++;
        g_debug.rawLux = clampedLux;
        g_debug.rawAls = rawAls;
        g_debug.rawWhite = rawWhite;
        g_debug.lastReadMs = millis();
        g_debug.lastError = "";
        g_debug.deviceResponding = true;

        if (!g_filterPrimed || seedOnly)
        {
            g_debug.filteredLux = clampedLux;
            g_filterPrimed = true;
            updateDesiredBrightnessFromLux(false);
            return;
        }

        g_debug.filteredLux = ambientApplySmoothing(g_debug.filteredLux, clampedLux, responseAlpha);
        updateDesiredBrightnessFromLux(true);
    }
}

void initAmbientLight()
{
    g_debug = AmbientLightDebugInfo{};
    g_debug.autoEnabled = cfg.autoBrightnessEnabled;
    g_debug.sdaPin = cfg.ambientLightSdaPin;
    g_debug.sclPin = cfg.ambientLightSclPin;
    g_debug.targetBrightness = clampInt(cfg.brightness, 0, 255);
    g_debug.desiredBrightness = clampInt(cfg.brightness, 0, 255);
    g_debug.appliedBrightness = clampInt(cfg.brightness, 0, 255);
    g_lastPollMs = 0;
    g_lastInitAttemptMs = 0;
    configureSensor();
    sampleSensor(true);
}

void ambientLightOnConfigChanged()
{
    g_debug.autoEnabled = cfg.autoBrightnessEnabled;
    g_debug.sdaPin = cfg.ambientLightSdaPin;
    g_debug.sclPin = cfg.ambientLightSclPin;

    if (pinsChanged())
    {
        LOG_INFO("AMBIENT", "RECONFIGURE",
                 String("reinit pins sda=") + cfg.ambientLightSdaPin + " scl=" + cfg.ambientLightSclPin);
        configureSensor();
        sampleSensor(true);
        return;
    }

    if (!g_sensorReady)
    {
        configureSensor();
        sampleSensor(true);
        return;
    }

    updateDesiredBrightnessFromLux(false);
}

void ambientLightForceProbe()
{
    LOG_INFO("AMBIENT", "FORCE_PROBE",
             String("sda=") + cfg.ambientLightSdaPin + " scl=" + cfg.ambientLightSclPin);
    g_lastPollMs = 0;
    configureSensor();
    if (g_sensorReady)
    {
        sampleSensor(true);
    }
}

void ambientLightLoop()
{
    g_debug.autoEnabled = cfg.autoBrightnessEnabled;
    g_debug.sensorActive = cfg.autoBrightnessEnabled && g_sensorReady;

    const unsigned long now = millis();
    if (!g_sensorReady)
    {
        g_debug.desiredBrightness = clampInt(cfg.brightness, 0, 255);
        g_debug.targetBrightness = g_debug.desiredBrightness;

        if (g_lastInitAttemptMs == 0 || (now - g_lastInitAttemptMs) >= AMBIENT_RETRY_INTERVAL_MS)
        {
            configureSensor();
            if (g_sensorReady)
            {
                sampleSensor(true);
            }
        }
        return;
    }

    if (g_lastPollMs > 0 && (now - g_lastPollMs) < AMBIENT_POLL_INTERVAL_MS)
    {
        return;
    }

    if (g_debug.lastReadMs > 0 && (now - g_debug.lastReadMs) < AMBIENT_MIN_READ_GAP_MS)
    {
        return;
    }

    g_lastPollMs = now;
    sampleSensor(false);
}

uint8_t ambientLightGetLedBrightness()
{
    const int manualBrightness = clampInt(cfg.brightness, 0, 255);
    if (!cfg.autoBrightnessEnabled || !g_sensorReady || !g_filterPrimed)
    {
        g_debug.sensorActive = false;
        g_debug.targetBrightness = manualBrightness;
        g_debug.desiredBrightness = manualBrightness;
        return static_cast<uint8_t>(manualBrightness);
    }

    g_debug.sensorActive = true;
    g_debug.desiredBrightness = clampInt(g_debug.desiredBrightness, 0, manualBrightness);
    return static_cast<uint8_t>(g_debug.desiredBrightness);
}

void ambientLightNoteAppliedBrightness(uint8_t brightness)
{
    g_debug.appliedBrightness = clampInt(static_cast<int>(brightness), 0, 255);
    g_debug.lastApplyMs = millis();
}

AmbientLightDebugInfo ambientLightGetDebugInfo()
{
    AmbientLightDebugInfo info = g_debug;
    info.autoEnabled = cfg.autoBrightnessEnabled;
    info.sensorActive = cfg.autoBrightnessEnabled && g_sensorReady;
    info.sdaPin = cfg.ambientLightSdaPin;
    info.sclPin = cfg.ambientLightSclPin;
    return info;
}
