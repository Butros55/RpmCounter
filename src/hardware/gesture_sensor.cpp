#include "gesture_sensor.h"

#include "core/config.h"
#include "core/logging.h"
#include "hardware/led_bar.h"

#if defined(CONFIG_IDF_TARGET_ESP32S3)
#include <driver/i2c_master.h>

#include "hardware/display_s3.h"
#endif

namespace
{
    constexpr uint32_t GESTURE_POLL_INTERVAL_MS = 35;
    constexpr uint32_t GESTURE_INT_FAST_POLL_MS = 4;
    constexpr uint32_t GESTURE_RETRY_INTERVAL_MS = 3000;
    constexpr uint32_t GESTURE_COOLDOWN_MS = 650;
    constexpr uint8_t GESTURE_MAX_READ_ERRORS = 4;
    constexpr uint8_t APDS9960_I2C_ADDR = 0x39;
    constexpr uint8_t APDS9960_REG_ENABLE = 0x80;
    constexpr uint8_t APDS9960_REG_ATIME = 0x81;
    constexpr uint8_t APDS9960_REG_WTIME = 0x83;
    constexpr uint8_t APDS9960_REG_PILT = 0x89;
    constexpr uint8_t APDS9960_REG_PIHT = 0x8B;
    constexpr uint8_t APDS9960_REG_PERS = 0x8C;
    constexpr uint8_t APDS9960_REG_PPULSE = 0x8E;
    constexpr uint8_t APDS9960_REG_CONTROL = 0x8F;
    constexpr uint8_t APDS9960_REG_CONFIG2 = 0x90;
    constexpr uint8_t APDS9960_REG_ID = 0x92;
    constexpr uint8_t APDS9960_REG_STATUS = 0x93;
    constexpr uint8_t APDS9960_REG_GPENTH = 0xA0;
    constexpr uint8_t APDS9960_REG_GEXTH = 0xA1;
    constexpr uint8_t APDS9960_REG_GCONF1 = 0xA2;
    constexpr uint8_t APDS9960_REG_GCONF2 = 0xA3;
    constexpr uint8_t APDS9960_REG_GPULSE = 0xA6;
    constexpr uint8_t APDS9960_REG_GCONF3 = 0xAA;
    constexpr uint8_t APDS9960_REG_GCONF4 = 0xAB;
    constexpr uint8_t APDS9960_REG_GFLVL = 0xAE;
    constexpr uint8_t APDS9960_REG_GSTATUS = 0xAF;
    constexpr uint8_t APDS9960_REG_GFIFO_U = 0xFC;
    constexpr uint8_t APDS9960_ENABLE_PON = 0x01;
    constexpr uint8_t APDS9960_ENABLE_PEN = 0x04;
    constexpr uint8_t APDS9960_ENABLE_GEN = 0x40;
    constexpr uint8_t APDS9960_GSTATUS_GVALID = 0x01;
    constexpr int APDS_GESTURE_DATASETS_MAX = 32;
    constexpr int APDS_SAMPLE_TOTAL_MIN = 40;
    constexpr int APDS_GESTURE_SENSITIVITY = 18;
    constexpr int APDS_GESTURE_DOMINANCE = 8;

#if defined(CONFIG_IDF_TARGET_ESP32S3)
    constexpr int S3_BOARD_I2C_SDA_PIN = 47;
    constexpr int S3_BOARD_I2C_SCL_PIN = 48;
    constexpr int S3_GESTURE_INT_PIN = 15;

    i2c_master_dev_handle_t g_sensorDev = nullptr;
    bool g_usingSharedBus = false;
    volatile bool g_intPending = false;
    volatile uint32_t g_intTriggerCount = 0;
    bool g_intConfigured = false;
    unsigned long g_lastIntHandledMs = 0;
#endif

    enum class GesturePollState : uint8_t
    {
        NoData = 0,
        GestureReady,
        Failure
    };

    GestureSensorDebugInfo g_debug;
    bool g_sensorReady = false;
    uint8_t g_consecutiveReadErrors = 0;
    unsigned long g_lastInitAttemptMs = 0;
    unsigned long g_lastPollMs = 0;
    unsigned long g_lastAcceptedGestureMs = 0;

    void syncDebugPins()
    {
#if defined(CONFIG_IDF_TARGET_ESP32S3)
        g_debug.sdaPin = S3_BOARD_I2C_SDA_PIN;
        g_debug.sclPin = S3_BOARD_I2C_SCL_PIN;
        g_debug.intPin = S3_GESTURE_INT_PIN;
#else
        g_debug.sdaPin = -1;
        g_debug.sclPin = -1;
        g_debug.intPin = -1;
#endif
    }

#if defined(CONFIG_IDF_TARGET_ESP32S3)
    void IRAM_ATTR gestureIntIsr()
    {
        g_intPending = true;
        g_intTriggerCount = g_intTriggerCount + 1;
    }

    void refreshInterruptDebug()
    {
        g_debug.intConfigured = g_intConfigured;
        g_debug.intEnabled = g_intConfigured && g_sensorReady;
        g_debug.intPending = g_intPending;
        g_debug.intTriggerCount = g_intTriggerCount;
        g_debug.intLineLow = g_intConfigured ? (digitalRead(S3_GESTURE_INT_PIN) == LOW) : false;
    }

    void clearInterruptPending(unsigned long nowMs)
    {
        g_intPending = false;
        g_lastIntHandledMs = nowMs;
        g_debug.lastIntMs = nowMs;
    }

    void configureGestureInterruptPin()
    {
        pinMode(S3_GESTURE_INT_PIN, INPUT_PULLUP);
        detachInterrupt(digitalPinToInterrupt(S3_GESTURE_INT_PIN));
        attachInterrupt(digitalPinToInterrupt(S3_GESTURE_INT_PIN), gestureIntIsr, FALLING);
        g_intConfigured = true;
        refreshInterruptDebug();
    }

    void releaseGestureInterruptPin()
    {
        detachInterrupt(digitalPinToInterrupt(S3_GESTURE_INT_PIN));
        g_intConfigured = false;
        g_intPending = false;
        refreshInterruptDebug();
    }
#else
    void refreshInterruptDebug()
    {
    }

    void clearInterruptPending(unsigned long)
    {
    }
#endif

    void markUnavailable(const String &reason,
                         bool busInitialized = false,
                         bool deviceResponding = false,
                         bool usingSharedBus = false)
    {
        g_sensorReady = false;
        g_debug.sensorDetected = false;
        g_debug.sensorActive = false;
        g_debug.busInitialized = busInitialized;
        g_debug.deviceResponding = deviceResponding;
        g_debug.usingSharedBus = usingSharedBus;
        if (!deviceResponding)
        {
            g_debug.deviceId = 0;
        }
        g_debug.lastError = reason;
        refreshInterruptDebug();
    }

#if defined(CONFIG_IDF_TARGET_ESP32S3)
    void releaseGestureDevice()
    {
        if (g_sensorDev)
        {
            if (g_usingSharedBus)
            {
                display_s3_remove_shared_i2c_device(g_sensorDev);
            }
            g_sensorDev = nullptr;
        }
        g_usingSharedBus = false;
    }

    bool writeRegister8(uint8_t reg, uint8_t value)
    {
        if (!g_sensorDev)
        {
            return false;
        }

        uint8_t payload[2] = {reg, value};
        return i2c_master_transmit(g_sensorDev, payload, sizeof(payload), -1) == ESP_OK;
    }

    bool readRegister8(uint8_t reg, uint8_t &value)
    {
        if (!g_sensorDev)
        {
            return false;
        }

        return i2c_master_transmit_receive(g_sensorDev, &reg, 1, &value, 1, -1) == ESP_OK;
    }

    bool readRegisterBlock(uint8_t reg, uint8_t *buffer, size_t length)
    {
        if (!g_sensorDev || !buffer || length == 0)
        {
            return false;
        }

        return i2c_master_transmit_receive(g_sensorDev, &reg, 1, buffer, length, -1) == ESP_OK;
    }

    bool sendCommand(uint8_t reg)
    {
        if (!g_sensorDev)
        {
            return false;
        }

        return i2c_master_transmit(g_sensorDev, &reg, 1, -1) == ESP_OK;
    }

    bool knownDeviceId(uint8_t id)
    {
        return id == 0xAB || id == 0xA8 || id == 0xA9 || id == 0x9C;
    }

    bool attachGestureDevice()
    {
        if (!display_s3_add_shared_i2c_device(APDS9960_I2C_ADDR, 100000U, &g_sensorDev))
        {
            g_debug.lastError = F("shared-i2c-device-failed");
            return false;
        }

        g_usingSharedBus = true;
        g_debug.busInitialized = true;
        g_debug.usingSharedBus = true;
        return true;
    }

    bool probeGestureAddress()
    {
        const bool ok = display_s3_probe_shared_i2c_address(APDS9960_I2C_ADDR, 50);
        g_debug.ackResponding = ok;
        return ok;
    }

    bool configureApds9960()
    {
        if (!writeRegister8(APDS9960_REG_ENABLE, 0x00))
        {
            g_debug.lastError = F("apds-disable-failed");
            return false;
        }
        delay(5);

        // Conservative gesture profile based on common APDS-9960 defaults.
        if (!writeRegister8(APDS9960_REG_ATIME, 219) || !writeRegister8(APDS9960_REG_WTIME, 246) ||
            !writeRegister8(APDS9960_REG_PPULSE, 0x87) || !writeRegister8(APDS9960_REG_PILT, 0x00) ||
            !writeRegister8(APDS9960_REG_PIHT, 50) || !writeRegister8(APDS9960_REG_PERS, 0x11) ||
            !writeRegister8(APDS9960_REG_CONTROL, 0x05) || !writeRegister8(APDS9960_REG_CONFIG2, 0x01) ||
            !writeRegister8(APDS9960_REG_GPENTH, 40) || !writeRegister8(APDS9960_REG_GEXTH, 30) ||
            !writeRegister8(APDS9960_REG_GCONF1, 0x40) || !writeRegister8(APDS9960_REG_GCONF2, 0x41) ||
            !writeRegister8(APDS9960_REG_GPULSE, 0xC9) || !writeRegister8(APDS9960_REG_GCONF3, 0x00) ||
            !writeRegister8(APDS9960_REG_GCONF4, 0x03))
        {
            g_debug.lastError = F("apds-config-failed");
            return false;
        }

        (void)sendCommand(0xE7); // clear any stale interrupt state
        if (!writeRegister8(APDS9960_REG_ENABLE, APDS9960_ENABLE_PON | APDS9960_ENABLE_PEN | APDS9960_ENABLE_GEN))
        {
            g_debug.lastError = F("apds-enable-failed");
            return false;
        }

        g_debug.configApplied = true;
        delay(10);
        return true;
    }
#endif

    bool decodeGestureDirection(const uint8_t *fifoData, size_t datasetCount, GestureDirection &direction)
    {
        if (!fifoData || datasetCount < 2)
        {
            direction = GestureDirection::None;
            return false;
        }

        int firstU = -1;
        int firstD = -1;
        int firstL = -1;
        int firstR = -1;
        int lastU = -1;
        int lastD = -1;
        int lastL = -1;
        int lastR = -1;

        for (size_t index = 0; index < datasetCount; ++index)
        {
            const int u = fifoData[index * 4 + 0];
            const int d = fifoData[index * 4 + 1];
            const int l = fifoData[index * 4 + 2];
            const int r = fifoData[index * 4 + 3];
            if ((u + d + l + r) < APDS_SAMPLE_TOTAL_MIN)
            {
                continue;
            }

            if (firstL < 0)
            {
                firstU = u;
                firstD = d;
                firstL = l;
                firstR = r;
            }

            lastU = u;
            lastD = d;
            lastL = l;
            lastR = r;
        }

        if (firstL < 0 || lastL < 0)
        {
            direction = GestureDirection::None;
            return false;
        }

        const int lrFirst = ((firstL - firstR) * 100) / max(1, firstL + firstR);
        const int lrLast = ((lastL - lastR) * 100) / max(1, lastL + lastR);
        const int udFirst = ((firstU - firstD) * 100) / max(1, firstU + firstD);
        const int udLast = ((lastU - lastD) * 100) / max(1, lastU + lastD);
        const int horizontalDelta = lrLast - lrFirst;
        const int verticalDelta = udLast - udFirst;

        direction = gestureSensorClassifyDeltas(horizontalDelta, verticalDelta, APDS_GESTURE_SENSITIVITY,
                                                APDS_GESTURE_DOMINANCE);
        return direction != GestureDirection::None;
    }

    GesturePollState pollGestureFifo(GestureDirection &direction)
    {
        direction = GestureDirection::None;

#if !defined(CONFIG_IDF_TARGET_ESP32S3)
        return GesturePollState::NoData;
#else
        uint8_t status = 0;
        if (!readRegister8(APDS9960_REG_GSTATUS, status))
        {
            return GesturePollState::Failure;
        }

        g_debug.ackResponding = true;
        g_debug.deviceResponding = true;
        g_debug.lastStatusReg = status;

        if ((status & APDS9960_GSTATUS_GVALID) == 0)
        {
            g_debug.lastError = "";
            g_debug.lastReadMs = millis();
            g_debug.lastFifoLevel = 0;
            return GesturePollState::NoData;
        }

        uint8_t fifoLevel = 0;
        if (!readRegister8(APDS9960_REG_GFLVL, fifoLevel))
        {
            return GesturePollState::Failure;
        }

        g_debug.lastFifoLevel = fifoLevel;

        if (fifoLevel == 0)
        {
            g_debug.lastError = "";
            g_debug.lastReadMs = millis();
            return GesturePollState::NoData;
        }

        const size_t datasetCount = static_cast<size_t>(min<int>(fifoLevel, APDS_GESTURE_DATASETS_MAX));
        uint8_t fifoData[APDS_GESTURE_DATASETS_MAX * 4] = {};
        if (!readRegisterBlock(APDS9960_REG_GFIFO_U, fifoData, datasetCount * 4))
        {
            return GesturePollState::Failure;
        }

        g_debug.deviceResponding = true;
        g_debug.ackResponding = true;
        g_debug.lastReadMs = millis();
        g_debug.lastError = "";

        if (!decodeGestureDirection(fifoData, datasetCount, direction))
        {
            return GesturePollState::NoData;
        }

        return GesturePollState::GestureReady;
#endif
    }

    void handleReadFailure(const __FlashStringHelper *reason)
    {
        ++g_consecutiveReadErrors;
        ++g_debug.readErrorCount;
        g_debug.lastError = reason;
        LOG_WARN("GESTURE", "READ_FAIL",
                 String("reason=") + String(reason) + " count=" + g_consecutiveReadErrors);

        if (g_consecutiveReadErrors < GESTURE_MAX_READ_ERRORS)
        {
            return;
        }

        LOG_WARN("GESTURE", "DISABLED", "too many read failures, scheduling reprobe");
#if defined(CONFIG_IDF_TARGET_ESP32S3)
        releaseGestureDevice();
#endif
        g_debug.ackResponding = false;
        g_debug.idReadOk = false;
        g_debug.configApplied = false;
        markUnavailable(F("gesture-read-lost"), true, false, true);
    }

    bool configureSensor()
    {
        g_debug.enabled = cfg.gestureControlEnabled;
        syncDebugPins();
        g_debug.lastInitMs = millis();
        g_debug.lastProbeMs = g_debug.lastInitMs;
        ++g_debug.initAttempts;
        ++g_debug.probeCount;
        g_lastInitAttemptMs = g_debug.lastInitMs;
        g_consecutiveReadErrors = 0;

        if (!cfg.gestureControlEnabled)
        {
#if defined(CONFIG_IDF_TARGET_ESP32S3)
            releaseGestureDevice();
#endif
            markUnavailable(F("disabled"));
            return false;
        }

#if !defined(CONFIG_IDF_TARGET_ESP32S3)
        markUnavailable(F("gesture-s3-only"));
        return false;
#else
        releaseGestureDevice();
        g_debug.busInitialized = false;
        g_debug.deviceResponding = false;
        g_debug.ackResponding = false;
        g_debug.idReadOk = false;
        g_debug.configApplied = false;
        g_debug.sensorDetected = false;
        g_debug.sensorActive = false;
        g_debug.usingSharedBus = false;
        g_debug.deviceId = 0;
        g_debug.lastStatusReg = 0;
        g_debug.lastFifoLevel = 0;

        if (!attachGestureDevice())
        {
            LOG_WARN("GESTURE", "BUS_INIT_FAIL", String("addr=0x39 err=") + g_debug.lastError);
            releaseGestureDevice();
            markUnavailable(g_debug.lastError);
            return false;
        }

        const bool ackResponding = probeGestureAddress();
        uint8_t deviceId = 0;
        if (!readRegister8(APDS9960_REG_ID, deviceId))
        {
            releaseGestureDevice();
            markUnavailable(ackResponding ? F("apds-id-read-failed") : F("apds-no-ack-0x39"), true, false, true);
            LOG_WARN("GESTURE", ackResponding ? "ID_READ_FAIL" : "SENSOR_MISSING",
                     ackResponding ? "ack at 0x39 but ID register read failed" : "no ack at 0x39");
            return false;
        }

        g_debug.deviceResponding = true;
        g_debug.ackResponding = true;
        g_debug.idReadOk = true;
        g_debug.deviceId = deviceId;
        g_debug.lastReadMs = millis();
        if (!knownDeviceId(deviceId))
        {
            if (deviceId == 0x00 || deviceId == 0xFF)
            {
                releaseGestureDevice();
                markUnavailable(String(F("apds-id-0x")) + String(deviceId, HEX), true, true, true);
                LOG_WARN("GESTURE", "BAD_ID", String("id=0x") + String(deviceId, HEX));
                return false;
            }

            LOG_WARN("GESTURE", "UNEXPECTED_ID", String("continuing with id=0x") + String(deviceId, HEX));
        }

        if (!configureApds9960())
        {
            const String error = g_debug.lastError;
            releaseGestureDevice();
            markUnavailable(error, true, true, true);
            LOG_WARN("GESTURE", "CONFIG_FAIL", String("id=0x") + String(deviceId, HEX) + " err=" + error);
            return false;
        }

        g_sensorReady = true;
        g_debug.sensorDetected = true;
        g_debug.sensorActive = true;
        g_debug.lastError = "";
        ++g_debug.initSuccessCount;
        refreshInterruptDebug();
        LOG_INFO("GESTURE", "READY",
                 String("apds9960 id=0x") + String(deviceId, HEX) + " shared-bus SDA=" + g_debug.sdaPin +
                     " SCL=" + g_debug.sclPin);
        return true;
#endif
    }

    void applyGestureDirection(GestureDirection direction, unsigned long nowMs)
    {
        if (direction == GestureDirection::None)
        {
            return;
        }

        if (!gestureSensorCooldownReady(nowMs, g_lastAcceptedGestureMs, GESTURE_COOLDOWN_MS))
        {
            LOG_DEBUG("GESTURE", "COOLDOWN",
                      String("dir=") + gestureSensorDirectionName(direction) + " age=" + (nowMs - g_lastAcceptedGestureMs));
            return;
        }

        const int previousMode = gestureSensorClampMode(cfg.mode);
        const int nextMode = gestureSensorModeAfterDirection(previousMode, direction);
        if (nextMode == previousMode)
        {
            return;
        }

        cfg.mode = nextMode;
        saveConfig();
        ledBarInvalidateFrameCache();

        g_lastAcceptedGestureMs = nowMs;
        g_debug.lastGesture = direction;
        g_debug.lastGestureMs = nowMs;
        ++g_debug.gestureCount;
        ++g_debug.modeSwitchCount;
        g_debug.lastError = "";

        LOG_INFO("GESTURE", "MODE_SWITCH",
                 String("dir=") + gestureSensorDirectionName(direction) + " " + gestureSensorModeName(previousMode) +
                     " -> " + gestureSensorModeName(nextMode));
    }
}

const char *gestureSensorDirectionName(GestureDirection direction)
{
    switch (direction)
    {
    case GestureDirection::Left:
        return "left";
    case GestureDirection::Right:
        return "right";
    case GestureDirection::None:
    default:
        return "none";
    }
}

const char *gestureSensorModeName(int mode)
{
    switch (gestureSensorClampMode(mode))
    {
    case 0:
        return "Casual";
    case 1:
        return "F1";
    case 2:
        return "Aggressiv";
    case 3:
        return "GT3";
    default:
        return "Casual";
    }
}

void initGestureSensor()
{
    g_debug = GestureSensorDebugInfo{};
    g_debug.enabled = cfg.gestureControlEnabled;
    syncDebugPins();
    g_lastInitAttemptMs = 0;
    g_lastPollMs = 0;
    g_lastAcceptedGestureMs = 0;
    g_consecutiveReadErrors = 0;
#if defined(CONFIG_IDF_TARGET_ESP32S3)
    g_intPending = false;
    g_intTriggerCount = 0;
    g_intConfigured = false;
    g_lastIntHandledMs = 0;
    configureGestureInterruptPin();
#endif
    refreshInterruptDebug();

    if (!cfg.gestureControlEnabled)
    {
        g_debug.lastError = F("disabled");
        return;
    }

    configureSensor();
}

void gestureSensorOnConfigChanged()
{
    g_debug.enabled = cfg.gestureControlEnabled;
    if (!cfg.gestureControlEnabled)
    {
#if defined(CONFIG_IDF_TARGET_ESP32S3)
        releaseGestureDevice();
        releaseGestureInterruptPin();
#endif
        markUnavailable(F("disabled"));
        return;
    }

#if defined(CONFIG_IDF_TARGET_ESP32S3)
    if (!g_intConfigured)
    {
        configureGestureInterruptPin();
    }
#endif
    if (!g_sensorReady)
    {
        configureSensor();
    }
}

void gestureSensorForceProbe()
{
    LOG_INFO("GESTURE", "FORCE_PROBE", "manual reprobe requested");
    g_lastPollMs = 0;
    g_consecutiveReadErrors = 0;
    configureSensor();
}

void gestureSensorLoop()
{
    g_debug.enabled = cfg.gestureControlEnabled;
    g_debug.sensorActive = cfg.gestureControlEnabled && g_sensorReady;
    refreshInterruptDebug();

    const unsigned long now = millis();
    if (!cfg.gestureControlEnabled)
    {
        return;
    }

    if (!g_sensorReady)
    {
        if (g_lastInitAttemptMs == 0 || (now - g_lastInitAttemptMs) >= GESTURE_RETRY_INTERVAL_MS)
        {
            configureSensor();
        }
        return;
    }

    const bool interruptHint = g_intPending;
    const uint32_t minPollGap = interruptHint ? GESTURE_INT_FAST_POLL_MS : GESTURE_POLL_INTERVAL_MS;
    if (g_lastPollMs > 0 && (now - g_lastPollMs) < minPollGap)
    {
        return;
    }

    g_lastPollMs = now;
    ++g_debug.pollCount;

    GestureDirection direction = GestureDirection::None;
    switch (pollGestureFifo(direction))
    {
    case GesturePollState::Failure:
        clearInterruptPending(now);
        handleReadFailure(F("gesture-poll-failed"));
        return;
    case GesturePollState::GestureReady:
        g_consecutiveReadErrors = 0;
        clearInterruptPending(now);
        applyGestureDirection(direction, now);
        return;
    case GesturePollState::NoData:
    default:
        g_consecutiveReadErrors = 0;
        if (interruptHint)
        {
            clearInterruptPending(now);
        }
        return;
    }
}

GestureSensorDebugInfo gestureSensorGetDebugInfo()
{
    GestureSensorDebugInfo info = g_debug;
    info.enabled = cfg.gestureControlEnabled;
    info.sensorActive = cfg.gestureControlEnabled && g_sensorReady;
    syncDebugPins();
    info.sdaPin = g_debug.sdaPin;
    info.sclPin = g_debug.sclPin;
    return info;
}
