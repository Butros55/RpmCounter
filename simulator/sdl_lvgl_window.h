#pragma once

#include <cstdint>
#include <vector>

#include <SDL.h>
#include <lvgl.h>

#include "virtual_led_bar.h"

struct SdlLvglWindowConfig
{
    int width = 800;
    int height = 400;
    int scale = 1;
    bool showLedBarPreview = true;
    int ledBarPreviewHeight = 92;
    const char *title = "RpmCounter LVGL Simulator";
};

class SdlLvglWindow
{
public:
    SdlLvglWindow() = default;
    ~SdlLvglWindow();

    bool init(const SdlLvglWindowConfig &config);
    void shutdown();

    bool processEvent(const SDL_Event &event);
    void pumpTime();
    void render();
    bool saveFramebufferBmp(const char *path) const;
    void setLedBarPreview(const VirtualLedBarFrame &frame);

    lv_disp_t *display() const;
    int windowWidth() const;
    int windowHeight() const;

private:
    struct PointerState
    {
        int x = 0;
        int y = 0;
        bool pressed = false;
    };

    static void flush_cb(lv_disp_drv_t *dispDrv, const lv_area_t *area, lv_color_t *color_p);
    static void pointer_read_cb(lv_indev_drv_t *indevDrv, lv_indev_data_t *data);

    void blitArea(const lv_area_t *area, const lv_color_t *colors);
    void composeFrame();
    void fillRect(int x, int y, int w, int h, uint32_t color);
    void fillCircle(int cx, int cy, int radius, uint32_t color);

    int width_ = 0;
    int height_ = 0;
    int scale_ = 1;
    int ledBarPreviewHeight_ = 0;
    int displayOffsetY_ = 0;
    int windowWidth_ = 0;
    int windowHeight_ = 0;
    uint32_t lastTickMs_ = 0;

    SDL_Window *window_ = nullptr;
    SDL_Renderer *renderer_ = nullptr;
    SDL_Texture *texture_ = nullptr;

    lv_disp_draw_buf_t drawBuffer_{};
    lv_disp_drv_t dispDrv_{};
    lv_indev_drv_t pointerDrv_{};
    lv_disp_t *disp_ = nullptr;

    PointerState pointer_{};
    VirtualLedBarFrame ledBarFrame_{};
    std::vector<uint32_t> framebuffer_;
    std::vector<uint32_t> composedFramebuffer_;
    std::vector<lv_color_t> drawBuf1_;
    std::vector<lv_color_t> drawBuf2_;
};
