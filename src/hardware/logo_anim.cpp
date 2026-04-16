#include "logo_anim.h"

#include <Arduino.h>

#include "core/config.h"
#include "core/state.h"
#include "led_bar.h"

namespace
{
    void waitForLedEffect(unsigned long timeoutMs)
    {
        const unsigned long startMs = millis();
        while (ledBarEffectActive() && (millis() - startMs) < timeoutMs)
        {
            delay(20);
        }
    }
}

void showMLogoPreview()
{
    ledBarRequestLogoPreview();
}

void showMLogoAnimation()
{
    if (g_animationActive)
        return;

    Serial.println("[MLOGO] Starte BMW M Boot-Animation");
    ledBarRequestLogoAnimation();
    waitForLedEffect(2400);

    Serial.println("[MLOGO] Animation fertig");
}

void showMLogoLeavingAnimation()
{
    if (g_animationActive)
        return;

    Serial.println("[MLOGO] Starte Leaving-Animation");
    ledBarRequestLeavingAnimation();
    waitForLedEffect(3200);

    Serial.println("[MLOGO] Leaving-Animation fertig");
}

void logoAnimLoop()
{
    unsigned long now = millis();

    if (g_ignitionOn && (now - g_lastObdMs > IGNITION_TIMEOUT_MS))
    {
        Serial.println("[MLOGO] Zündung aus (Timeout) erkannt");
        g_ignitionOn = false;
        g_engineRunning = false;

        if (cfg.logoOnIgnitionOff && !g_leavingPlayedThisCycle)
        {
            g_leavingPlayedThisCycle = true;
            showMLogoLeavingAnimation();
        }

        g_logoPlayedThisCycle = false;
        g_engineStartLogoShown = false;
        g_ignitionLogoShown = false;
    }
}
