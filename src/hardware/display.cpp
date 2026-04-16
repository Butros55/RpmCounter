#include "display.h"

#if !defined(CONFIG_IDF_TARGET_ESP32S3)

#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <math.h>

#include "core/state.h"

constexpr int TFT_MOSI = 23;
constexpr int TFT_SCLK = 18;
constexpr int TFT_CS = 15;
constexpr int TFT_DC = 4;
constexpr int TFT_RST = 16;

// TFT-Objekt MUSS vor dem Namespace definiert werden!
Adafruit_ST7789 tft(TFT_CS, TFT_DC, TFT_RST);

namespace
{

    constexpr int DIGIT_WIDTH = 150;
    constexpr int DIGIT_HEIGHT = 200;
    constexpr int SEG_THICKNESS = 26;

    bool g_displayInitialized = false;
    int g_displayGear = -99;
    bool g_displayBlink = false;

    struct SegmentPattern
    {
        bool segments[7];
    };

    constexpr SegmentPattern DIGIT_PATTERNS[10] = {
        {{true, true, true, true, true, true, false}},     // 0
        {{false, true, true, false, false, false, false}}, // 1
        {{true, true, false, true, true, false, true}},    // 2
        {{true, true, true, true, false, false, true}},    // 3
        {{false, true, true, false, false, true, true}},   // 4
        {{true, false, true, true, false, true, true}},    // 5
        {{true, false, true, true, true, true, true}},     // 6
        {{true, true, true, false, false, false, false}},  // 7
        {{true, true, true, true, true, true, true}},      // 8
        {{true, true, true, true, false, true, true}}      // 9
    };

    void drawSegment(bool horizontal, int x, int y, int length, int thickness, uint16_t color)
    {
        int radius = thickness / 2;
        if (horizontal)
        {
            tft.fillRoundRect(x, y, length, thickness, radius, color);
        }
        else
        {
            tft.fillRoundRect(x, y, thickness, length, radius, color);
        }
    }

    void drawDigitPattern(const SegmentPattern &pattern, uint16_t color)
    {
        int originX = (tft.width() - DIGIT_WIDTH) / 2;
        int originY = (tft.height() - DIGIT_HEIGHT) / 2;

        int horizontalLength = DIGIT_WIDTH - SEG_THICKNESS * 2;
        int verticalLength = (DIGIT_HEIGHT - SEG_THICKNESS * 3) / 2;

        if (pattern.segments[0])
            drawSegment(true, originX + SEG_THICKNESS, originY, horizontalLength, SEG_THICKNESS, color);
        if (pattern.segments[1])
            drawSegment(false, originX + DIGIT_WIDTH - SEG_THICKNESS, originY + SEG_THICKNESS, verticalLength, SEG_THICKNESS, color);
        if (pattern.segments[2])
            drawSegment(false, originX + DIGIT_WIDTH - SEG_THICKNESS, originY + SEG_THICKNESS * 2 + verticalLength, verticalLength, SEG_THICKNESS, color);
        if (pattern.segments[3])
            drawSegment(true, originX + SEG_THICKNESS, originY + DIGIT_HEIGHT - SEG_THICKNESS, horizontalLength, SEG_THICKNESS, color);
        if (pattern.segments[4])
            drawSegment(false, originX, originY + SEG_THICKNESS * 2 + verticalLength, verticalLength, SEG_THICKNESS, color);
        if (pattern.segments[5])
            drawSegment(false, originX, originY + SEG_THICKNESS, verticalLength, SEG_THICKNESS, color);
        if (pattern.segments[6])
            drawSegment(true, originX + SEG_THICKNESS, originY + (DIGIT_HEIGHT / 2) - (SEG_THICKNESS / 2), horizontalLength, SEG_THICKNESS, color);
    }

    void drawNeutralSymbol(uint16_t color)
    {
        int barWidth = SEG_THICKNESS;
        int barHeight = DIGIT_HEIGHT - SEG_THICKNESS;
        int originX = (tft.width() - DIGIT_WIDTH) / 2;
        int originY = (tft.height() - barHeight) / 2;

        tft.fillRoundRect(originX, originY, barWidth, barHeight, barWidth / 2, color);
        tft.fillRoundRect(originX + DIGIT_WIDTH - barWidth, originY, barWidth, barHeight, barWidth / 2, color);

        int startX = originX + barWidth;
        int startY = originY;
        int endX = originX + DIGIT_WIDTH - barWidth;
        int endY = originY + barHeight;
        for (int i = 0; i < SEG_THICKNESS; ++i)
        {
            tft.drawLine(startX, startY + i, endX, endY - SEG_THICKNESS + i, color);
        }
    }

    void renderGearDisplay()
    {
        if (!g_displayInitialized)
            return;

        uint16_t bg = g_displayBlink ? tft.color565(50, 0, 0) : ST77XX_BLACK;
        uint16_t digitColor = tft.color565(255, 140, 0);

        tft.fillScreen(bg);

        if (g_displayGear > 0 && g_displayGear < 10)
        {
            drawDigitPattern(DIGIT_PATTERNS[g_displayGear], digitColor);
        }
        else
        {
            drawNeutralSymbol(digitColor);
        }

        tft.setTextColor(ST77XX_WHITE, bg);
        tft.setTextSize(2);
        int textWidth = 4 * 12;
        int textX = (tft.width() - textWidth) / 2;
        tft.setCursor(textX, tft.height() - 24);
        tft.print("GEAR");
    }
}

void displayInit()
{
    SPI.begin(TFT_SCLK, -1, TFT_MOSI, TFT_CS);
    tft.init(240, 240);
    tft.setRotation(2);
    g_displayInitialized = true;
    displayClear();
    displaySetGear(0);
}

void displayClear()
{
    if (!g_displayInitialized)
        return;

    g_shiftBlinkActive = false;
    tft.fillScreen(ST77XX_BLACK);
}

void displayShowTestLogo()
{
    if (!g_displayInitialized)
        return;

    displayClear();

    const int16_t cx = tft.width() / 2;
    const int16_t cy = tft.height() / 2;
    const int16_t outerRadius = 90;
    const int16_t ringThickness = 10;
    const int16_t emblemRadius = outerRadius - ringThickness;
    const uint16_t bmwBlue = tft.color565(0, 105, 180);

    tft.fillCircle(cx, cy, outerRadius, ST77XX_WHITE);
    tft.fillCircle(cx, cy, outerRadius - 4, ST77XX_WHITE);
    tft.fillCircle(cx, cy, outerRadius - ringThickness, ST77XX_BLACK);
    tft.fillCircle(cx, cy, emblemRadius, ST77XX_WHITE);

    for (int16_t y = 0; y <= emblemRadius; ++y)
    {
        int16_t span = static_cast<int16_t>(sqrtf(static_cast<float>(emblemRadius * emblemRadius - y * y)));
        if (span <= 0)
            continue;

        int16_t yTop = cy - y;
        int16_t yBottom = cy + y;

        tft.drawFastHLine(cx - span, yTop, span, bmwBlue);
        tft.drawFastHLine(cx + 1, yBottom, span, bmwBlue);
    }

    tft.drawFastVLine(cx, cy - emblemRadius, emblemRadius * 2, ST77XX_BLACK);
    tft.drawFastHLine(cx - emblemRadius, cy, emblemRadius * 2, ST77XX_BLACK);

    tft.drawCircle(cx, cy, outerRadius, ST77XX_WHITE);
    tft.drawCircle(cx, cy, outerRadius - 1, ST77XX_WHITE);
    tft.drawCircle(cx, cy, emblemRadius, ST77XX_WHITE);
}

void displayShowSimSessionTransition(SimSessionTransitionType)
{
    // Sim-session overlays are S3-only. The legacy ST7789 path stays unchanged.
}

void displaySetGear(int gear)
{
    if (!g_displayInitialized)
        return;

    if (gear < 0)
        gear = 0;

    if (gear == g_displayGear)
        return;

    g_displayGear = gear;
    renderGearDisplay();
}

void displaySetShiftBlink(bool active)
{
    if (!g_displayInitialized)
        return;

    if (g_displayBlink == active)
        return;

    g_displayBlink = active;
    g_shiftBlinkActive = active;
    renderGearDisplay();
}

DisplayDebugInfo displayGetDebugInfo()
{
    DisplayDebugInfo info{};
    info.initAttempted = g_displayInitialized;
    info.ready = g_displayInitialized;
    info.buffersAllocated = g_displayInitialized;
    info.panelInitialized = g_displayInitialized;
    info.touchReady = false;
    info.tickFallback = false;
    info.debugSimpleUi = false;
    info.lastLvglRunMs = 0;
    info.lastError = g_displayInitialized ? "" : "display-not-initialized";
    return info;
}

void displayShowDebugPattern(DisplayDebugPattern pattern)
{
    if (!g_displayInitialized)
        return;

    switch (pattern)
    {
    case DisplayDebugPattern::ColorBars:
        tft.fillScreen(ST77XX_RED);
        delay(150);
        tft.fillScreen(ST77XX_GREEN);
        delay(150);
        tft.fillScreen(ST77XX_BLUE);
        delay(150);
        tft.fillScreen(ST77XX_WHITE);
        delay(150);
        break;
    case DisplayDebugPattern::Grid:
        tft.fillScreen(ST77XX_BLACK);
        for (int y = 0; y < tft.height(); y += 20)
        {
            for (int x = 0; x < tft.width(); x += 20)
            {
                uint16_t color = ((x / 20 + y / 20) % 2 == 0) ? ST77XX_WHITE : ST77XX_BLUE;
                tft.fillRect(x, y, 18, 18, color);
            }
        }
        break;
    case DisplayDebugPattern::UiLabel:
    default:
        displayShowTestLogo();
        break;
    }
}

#endif // !defined(CONFIG_IDF_TARGET_ESP32S3)
