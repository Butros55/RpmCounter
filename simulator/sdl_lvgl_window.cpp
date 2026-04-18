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

    uint32_t rgb_to_argb(uint32_t rgb)
    {
        return 0xFF000000u | rgb;
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
    ledBarPreviewHeight_ = config.showLedBarPreview ? std::max(56, config.ledBarPreviewHeight) : 0;
    displayOffsetY_ = ledBarPreviewHeight_;
    windowWidth_ = width_;
    windowHeight_ = height_ + ledBarPreviewHeight_;

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0)
    {
        return false;
    }

    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");

    window_ = SDL_CreateWindow(config.title,
                               SDL_WINDOWPOS_CENTERED,
                               SDL_WINDOWPOS_CENTERED,
                               windowWidth_ * scale_,
                               windowHeight_ * scale_,
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
                                 windowWidth_,
                                 windowHeight_);
    if (!texture_)
    {
        shutdown();
        return false;
    }

    framebuffer_.assign(static_cast<size_t>(width_ * height_), 0xFF000000u);
    composedFramebuffer_.assign(static_cast<size_t>(windowWidth_ * windowHeight_), 0xFF000000u);
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
    {
        const int localX = event.motion.x / scale_;
        const int localY = event.motion.y / scale_ - displayOffsetY_;
        pointer_.x = std::clamp(localX, 0, width_ - 1);
        pointer_.y = std::clamp(localY, 0, height_ - 1);
        break;
    }
    case SDL_MOUSEBUTTONDOWN:
        if (event.button.button == SDL_BUTTON_LEFT)
        {
            const int localX = event.button.x / scale_;
            const int localY = event.button.y / scale_ - displayOffsetY_;
            pointer_.pressed = localX >= 0 && localX < width_ && localY >= 0 && localY < height_;
            if (pointer_.pressed)
            {
                pointer_.x = std::clamp(localX, 0, width_ - 1);
                pointer_.y = std::clamp(localY, 0, height_ - 1);
            }
        }
        break;
    case SDL_MOUSEBUTTONUP:
        if (event.button.button == SDL_BUTTON_LEFT)
        {
            pointer_.pressed = false;
            const int localX = event.button.x / scale_;
            const int localY = event.button.y / scale_ - displayOffsetY_;
            if (localX >= 0 && localX < width_ && localY >= 0 && localY < height_)
            {
                pointer_.x = std::clamp(localX, 0, width_ - 1);
                pointer_.y = std::clamp(localY, 0, height_ - 1);
            }
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

void SdlLvglWindow::setLedBarPreview(const VirtualLedBarFrame &frame)
{
    ledBarFrame_ = frame;
}

void SdlLvglWindow::render()
{
    composeFrame();
    SDL_UpdateTexture(texture_, nullptr, composedFramebuffer_.data(), windowWidth_ * static_cast<int>(sizeof(uint32_t)));
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
    SDL_RenderClear(renderer_);

    SDL_Rect dstRect{0, 0, windowWidth_ * scale_, windowHeight_ * scale_};
    SDL_RenderCopy(renderer_, texture_, nullptr, &dstRect);
    SDL_RenderPresent(renderer_);
}

bool SdlLvglWindow::saveFramebufferBmp(const char *path) const
{
    if (!path || !*path || composedFramebuffer_.empty())
    {
        return false;
    }

    SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormatFrom(
        const_cast<uint32_t *>(composedFramebuffer_.data()),
        windowWidth_,
        windowHeight_,
        32,
        windowWidth_ * static_cast<int>(sizeof(uint32_t)),
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

int SdlLvglWindow::windowWidth() const
{
    return windowWidth_;
}

int SdlLvglWindow::windowHeight() const
{
    return windowHeight_;
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

void SdlLvglWindow::composeFrame()
{
    std::fill(composedFramebuffer_.begin(), composedFramebuffer_.end(), rgb_to_argb(0x08090C));

    if (ledBarPreviewHeight_ > 0)
    {
        fillRect(0, 0, windowWidth_, ledBarPreviewHeight_, rgb_to_argb(0x090B10));
        fillRect(12, 12, windowWidth_ - 24, ledBarPreviewHeight_ - 24, rgb_to_argb(0x11151C));

        const int ledCount = static_cast<int>(ledBarFrame_.leds.size());
        if (ledCount > 0)
        {
            const int paddingX = 32;
            const int availableWidth = windowWidth_ - paddingX * 2;
            const int gap = ledCount > 24 ? 6 : 8;
            const int radius = std::max(6, std::min(12, (availableWidth - gap * (ledCount - 1)) / (ledCount * 2)));
            const int diameter = radius * 2;
            const int totalWidth = ledCount * diameter + (ledCount - 1) * gap;
            const int startX = std::max(paddingX, (windowWidth_ - totalWidth) / 2);
            const int centerY = ledBarPreviewHeight_ / 2;

            for (int i = 0; i < ledCount; ++i)
            {
                fillCircle(startX + radius + i * (diameter + gap),
                           centerY,
                           radius,
                           rgb_to_argb(ledBarFrame_.leds[static_cast<size_t>(i)]));
            }
        }
    }

    for (int y = 0; y < height_; ++y)
    {
        const size_t srcOffset = static_cast<size_t>(y * width_);
        const size_t dstOffset = static_cast<size_t>((y + displayOffsetY_) * windowWidth_);
        std::copy_n(framebuffer_.data() + srcOffset, width_, composedFramebuffer_.data() + dstOffset);
    }
}

void SdlLvglWindow::fillRect(int x, int y, int w, int h, uint32_t color)
{
    const int xStart = std::max(0, x);
    const int yStart = std::max(0, y);
    const int xEnd = std::min(windowWidth_, x + w);
    const int yEnd = std::min(windowHeight_, y + h);

    for (int py = yStart; py < yEnd; ++py)
    {
        const size_t rowOffset = static_cast<size_t>(py * windowWidth_);
        for (int px = xStart; px < xEnd; ++px)
        {
            composedFramebuffer_[rowOffset + static_cast<size_t>(px)] = color;
        }
    }
}

void SdlLvglWindow::fillCircle(int cx, int cy, int radius, uint32_t color)
{
    const int xMin = std::max(0, cx - radius);
    const int xMax = std::min(windowWidth_ - 1, cx + radius);
    const int yMin = std::max(0, cy - radius);
    const int yMax = std::min(windowHeight_ - 1, cy + radius);
    const int radiusSq = radius * radius;

    for (int py = yMin; py <= yMax; ++py)
    {
        const size_t rowOffset = static_cast<size_t>(py * windowWidth_);
        for (int px = xMin; px <= xMax; ++px)
        {
            const int dx = px - cx;
            const int dy = py - cy;
            if (dx * dx + dy * dy <= radiusSq)
            {
                composedFramebuffer_[rowOffset + static_cast<size_t>(px)] = color;
            }
        }
    }
}
