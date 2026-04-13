#include "sdl_lvgl_window.h"

#include <algorithm>

namespace
{
    constexpr int kBufferLines = 48;

    uint32_t lv_color_to_argb8888(lv_color_t color)
    {
        lv_color32_t color32{};
        color32.full = lv_color_to32(color);
        return (0xFFu << 24) |
               (static_cast<uint32_t>(color32.ch.red) << 16) |
               (static_cast<uint32_t>(color32.ch.green) << 8) |
               static_cast<uint32_t>(color32.ch.blue);
    }
}

SdlLvglWindow::~SdlLvglWindow()
{
    shutdown();
}

bool SdlLvglWindow::init(const SdlLvglWindowConfig &config)
{
    width_ = config.width;
    height_ = config.height;
    scale_ = std::max(1, config.scale);

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0)
    {
        return false;
    }

    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");

    window_ = SDL_CreateWindow(config.title,
                               SDL_WINDOWPOS_CENTERED,
                               SDL_WINDOWPOS_CENTERED,
                               width_ * scale_,
                               height_ * scale_,
                               SDL_WINDOW_SHOWN);
    if (!window_)
    {
        shutdown();
        return false;
    }

    renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer_)
    {
        renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_SOFTWARE);
    }
    if (!renderer_)
    {
        shutdown();
        return false;
    }

    texture_ = SDL_CreateTexture(renderer_,
                                 SDL_PIXELFORMAT_ARGB8888,
                                 SDL_TEXTUREACCESS_STREAMING,
                                 width_,
                                 height_);
    if (!texture_)
    {
        shutdown();
        return false;
    }

    framebuffer_.assign(static_cast<size_t>(width_ * height_), 0xFF000000u);
    drawBuf1_.resize(static_cast<size_t>(width_ * kBufferLines));
    drawBuf2_.resize(static_cast<size_t>(width_ * kBufferLines));

    lv_disp_draw_buf_init(&drawBuffer_, drawBuf1_.data(), drawBuf2_.data(), width_ * kBufferLines);

    lv_disp_drv_init(&dispDrv_);
    dispDrv_.hor_res = width_;
    dispDrv_.ver_res = height_;
    dispDrv_.draw_buf = &drawBuffer_;
    dispDrv_.flush_cb = &SdlLvglWindow::flush_cb;
    dispDrv_.user_data = this;
    disp_ = lv_disp_drv_register(&dispDrv_);

    lv_indev_drv_init(&pointerDrv_);
    pointerDrv_.type = LV_INDEV_TYPE_POINTER;
    pointerDrv_.read_cb = &SdlLvglWindow::pointer_read_cb;
    pointerDrv_.user_data = this;
    lv_indev_drv_register(&pointerDrv_);

    lastTickMs_ = SDL_GetTicks();
    return disp_ != nullptr;
}

void SdlLvglWindow::shutdown()
{
    if (texture_)
    {
        SDL_DestroyTexture(texture_);
        texture_ = nullptr;
    }
    if (renderer_)
    {
        SDL_DestroyRenderer(renderer_);
        renderer_ = nullptr;
    }
    if (window_)
    {
        SDL_DestroyWindow(window_);
        window_ = nullptr;
    }
    if (SDL_WasInit(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0)
    {
        SDL_Quit();
    }
}

bool SdlLvglWindow::processEvent(const SDL_Event &event)
{
    switch (event.type)
    {
    case SDL_QUIT:
        return false;
    case SDL_MOUSEMOTION:
        pointer_.x = std::clamp(event.motion.x / scale_, 0, width_ - 1);
        pointer_.y = std::clamp(event.motion.y / scale_, 0, height_ - 1);
        break;
    case SDL_MOUSEBUTTONDOWN:
        if (event.button.button == SDL_BUTTON_LEFT)
        {
            pointer_.pressed = true;
            pointer_.x = std::clamp(event.button.x / scale_, 0, width_ - 1);
            pointer_.y = std::clamp(event.button.y / scale_, 0, height_ - 1);
        }
        break;
    case SDL_MOUSEBUTTONUP:
        if (event.button.button == SDL_BUTTON_LEFT)
        {
            pointer_.pressed = false;
            pointer_.x = std::clamp(event.button.x / scale_, 0, width_ - 1);
            pointer_.y = std::clamp(event.button.y / scale_, 0, height_ - 1);
        }
        break;
    default:
        break;
    }

    return true;
}

void SdlLvglWindow::pumpTime()
{
    const uint32_t now = SDL_GetTicks();
    lv_tick_inc(now - lastTickMs_);
    lastTickMs_ = now;
}

void SdlLvglWindow::render()
{
    SDL_UpdateTexture(texture_, nullptr, framebuffer_.data(), width_ * static_cast<int>(sizeof(uint32_t)));
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
    SDL_RenderClear(renderer_);

    SDL_Rect dstRect{0, 0, width_ * scale_, height_ * scale_};
    SDL_RenderCopy(renderer_, texture_, nullptr, &dstRect);
    SDL_RenderPresent(renderer_);
}

bool SdlLvglWindow::saveFramebufferBmp(const char *path) const
{
    if (!path || !*path || framebuffer_.empty())
    {
        return false;
    }

    SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormatFrom(
        const_cast<uint32_t *>(framebuffer_.data()),
        width_,
        height_,
        32,
        width_ * static_cast<int>(sizeof(uint32_t)),
        SDL_PIXELFORMAT_ARGB8888);
    if (!surface)
    {
        return false;
    }

    const int result = SDL_SaveBMP(surface, path);
    SDL_FreeSurface(surface);
    return result == 0;
}

lv_disp_t *SdlLvglWindow::display() const
{
    return disp_;
}

void SdlLvglWindow::flush_cb(lv_disp_drv_t *dispDrv, const lv_area_t *area, lv_color_t *color_p)
{
    auto *window = static_cast<SdlLvglWindow *>(dispDrv->user_data);
    window->blitArea(area, color_p);
    lv_disp_flush_ready(dispDrv);
}

void SdlLvglWindow::pointer_read_cb(lv_indev_drv_t *indevDrv, lv_indev_data_t *data)
{
    auto *window = static_cast<SdlLvglWindow *>(indevDrv->user_data);
    data->state = window->pointer_.pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    data->point.x = window->pointer_.x;
    data->point.y = window->pointer_.y;
}

void SdlLvglWindow::blitArea(const lv_area_t *area, const lv_color_t *colors)
{
    for (int y = area->y1; y <= area->y2; ++y)
    {
        for (int x = area->x1; x <= area->x2; ++x)
        {
            const size_t index = static_cast<size_t>(y * width_ + x);
            framebuffer_[index] = lv_color_to_argb8888(*colors++);
        }
    }
}
