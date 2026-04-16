#include "ambient_light.h"

#include <Adafruit_VEML7700.h>
#include <Wire.h>
#include <math.h>

#include "ambient_light_algo.h"
#include "core/config.h"
#include "core/logging.h"
#include "core/utils.h"

namespace
{
    constexpr uint32_t AMBIENT_POLL_INTERVAL_MS = 180;
    constexpr uint32_t AMBIENT_MIN_READ_GAP_MS = 100;

    TwoWire g_ambientWire(1);
    Adafruit_VEML7700 g_sensor;
    AmbientLightDebugInfo g_debug;
    bool g_sensorReady = false;
    bool g_filterPrimed = false;
    uint8_t g_consecutiveReadErrors = 0;
    int g_runtimeSdaPin = -1;
    int g_runtimeSclPin = -1;
    unsigned long g_lastPollMs = 0;

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

    void resetRuntimeState(bool keepDetection)
    {
        g_filterPrimed = false;
        g_consecutiveReadErrors = 0;
        g_debug.rawLux = 0.0f;
        g_debug.filteredLux = 0.0f;
        g_debug.targetBrightness = clampInt(cfg.brightness, 0, 255);
        g_debug.desiredBrightness = clampInt(cfg.brightness, 0, 255);
        if (!keepDetection)
        {
            g_debug.sensorDetected = false;
            g_debug.busInitialized = false;
            g_sensorReady = false;
        }
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

    bool configureSensor()
    {
        g_debug.initAttempts++;
        g_debug.sdaPin = cfg.ambientLightSdaPin;
        g_debug.sclPin = cfg.ambientLightSclPin;
        g_runtimeSdaPin = cfg.ambientLightSdaPin;
        g_runtimeSclPin = cfg.ambientLightSclPin;

        if (g_runtimeSdaPin < 0 || g_runtimeSdaPin > 48 || g_runtimeSclPin < 0 || g_runtimeSclPin > 48)
        {
            g_debug.lastError = F("invalid-pins");
            g_sensorReady = false;
            g_debug.sensorDetected = false;
            g_debug.busInitialized = false;
            LOG_WARN("AMBIENT", "PIN_INVALID",
                     String("sda=") + g_runtimeSdaPin + " scl=" + g_runtimeSclPin);
            return false;
        }

        g_ambientWire.begin(static_cast<int>(g_runtimeSdaPin), static_cast<int>(g_runtimeSclPin), 100000U);
        g_debug.busInitialized = true;

        if (!g_sensor.begin(&g_ambientWire))
        {
            g_debug.lastError = F("veml7700-not-found");
            g_sensorReady = false;
            g_debug.sensorDetected = false;
            LOG_WARN("AMBIENT", "SENSOR_MISSING",
                     String("sda=") + g_runtimeSdaPin + " scl=" + g_runtimeSclPin);
            resetRuntimeState(false);
            return false;
        }

        g_sensor.setGain(VEML7700_GAIN_1_8);
        g_sensor.setIntegrationTime(VEML7700_IT_100MS, false);
        g_sensor.enable(true);

        g_sensorReady = true;
        g_consecutiveReadErrors = 0;
        g_debug.sensorDetected = true;
        g_debug.initSuccessCount++;
        g_debug.lastError = "";
        resetRuntimeState(true);
        LOG_INFO("AMBIENT", "READY",
                 String("veml7700 pins sda=") + g_runtimeSdaPin + " scl=" + g_runtimeSclPin);
        return true;
    }

    void sampleSensor(bool seedOnly)
    {
        if (!g_sensorReady)
        {
            updateDesiredBrightnessFromLux(false);
            return;
        }

        const float lux = g_sensor.readLux(VEML_LUX_AUTO);
        if (!validLux(lux))
        {
            ++g_consecutiveReadErrors;
            g_debug.readErrorCount++;
            g_debug.lastError = F("lux-read-failed");
            LOG_WARN("AMBIENT", "READ_FAIL",
                     String("invalid lux sample count=") + g_consecutiveReadErrors);
            if (g_consecutiveReadErrors >= 3)
            {
                g_sensorReady = false;
                g_debug.sensorDetected = false;
                g_debug.lastError = F("sensor-read-lost");
                LOG_WARN("AMBIENT", "DISABLED", "sensor read failed repeatedly, falling back to manual brightness");
            }
            updateDesiredBrightnessFromLux(false);
            return;
        }

        const float clampedLux = lux > 120000.0f ? 120000.0f : lux;
        const float responseAlpha = ambientComputeResponseAlpha(cfg.autoBrightnessResponsePct);
        g_consecutiveReadErrors = 0;
        g_debug.readCount++;
        g_debug.rawLux = clampedLux;
        g_debug.lastReadMs = millis();
        g_debug.lastError = "";

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

    updateDesiredBrightnessFromLux(false);
}

void ambientLightLoop()
{
    g_debug.autoEnabled = cfg.autoBrightnessEnabled;
    g_debug.sensorActive = cfg.autoBrightnessEnabled && g_sensorReady;

    if (!g_sensorReady)
    {
        g_debug.desiredBrightness = clampInt(cfg.brightness, 0, 255);
        g_debug.targetBrightness = g_debug.desiredBrightness;
        return;
    }

    const unsigned long now = millis();
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
