#include <iostream>
#include <cstdlib>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>

#include <SDL.h>
#include <lvgl.h>

#include "simulator_app.h"
#include "sdl_lvgl_window.h"
#include "telemetry/telemetry_types.h"
#include "ui/ui_s3_main.h"

namespace
{
    constexpr uint32_t kCaptureSettlingMs = 150;

    bool env_flag_enabled(const char *name, bool defaultValue)
    {
        const char *value = std::getenv(name);
        if (!value)
        {
            return defaultValue;
        }

        std::string normalized(value);
        std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char ch)
                       { return static_cast<char>(std::tolower(ch)); });
        return normalized == "1" || normalized == "true" || normalized == "yes" || normalized == "on";
    }

    uint16_t env_port_value(const char *name, uint16_t defaultValue)
    {
        const char *value = std::getenv(name);
        if (!value)
        {
            return defaultValue;
        }

        try
        {
            const int parsed = std::stoi(value);
            if (parsed > 0 && parsed <= 65535)
            {
                return static_cast<uint16_t>(parsed);
            }
        }
        catch (...)
        {
        }

        return defaultValue;
    }

    std::string env_string_value(const char *name)
    {
        const char *value = std::getenv(name);
        return value ? std::string(value) : std::string();
    }

    TelemetryServiceConfig load_telemetry_config()
    {
        TelemetryServiceConfig config{};
        config.mode = env_flag_enabled("SIM_MODE", false) ? TelemetryInputMode::Simulator : TelemetryInputMode::SimHub;
        config.simHubTransport = SimHubTransport::HttpApi;
        config.httpPort = env_port_value("SIMHUB_HTTP_PORT", 8888);
        config.udpPort = env_port_value("SIMHUB_UDP_PORT", 20888);
        config.pollIntervalMs = std::max<uint32_t>(15U, env_port_value("SIMHUB_POLL_INTERVAL_MS", 25));
        config.staleTimeoutMs = 2000;
        config.debugLogging = env_flag_enabled("SIM_DEBUG_TELEMETRY", false);
        config.allowSimulatorFallback = env_flag_enabled("SIM_ALLOW_FALLBACK_SIMULATOR", false);

        const std::string source = env_string_value("SIMHUB_SOURCE");
        if (!source.empty())
        {
            std::string normalized(source);
            std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char ch)
                           { return static_cast<char>(std::tolower(ch)); });
            if (normalized == "udp" || normalized == "udpjson" || normalized == "jsonudp")
            {
                config.simHubTransport = SimHubTransport::JsonUdp;
            }
        }
        return config;
    }

    bool should_capture_frame(const TelemetryServiceConfig &config, const UiRuntimeState &state)
    {
        if (config.mode == TelemetryInputMode::SimHub)
        {
            return state.telemetrySource == UiTelemetrySource::SimHubUdp && !state.telemetryStale;
        }

        return true;
    }

    bool handle_ui_hotkey(SDL_Keycode key)
    {
        switch (key)
        {
        case SDLK_LEFT:
            ui_s3_debug_dispatch(UiDebugAction::PreviousCard);
            return true;
        case SDLK_RIGHT:
            ui_s3_debug_dispatch(UiDebugAction::NextCard);
            return true;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
            ui_s3_debug_dispatch(UiDebugAction::OpenSelectedCard);
            return true;
        case SDLK_ESCAPE:
            ui_s3_debug_dispatch(UiDebugAction::GoHome);
            return true;
        case SDLK_l:
            ui_s3_debug_dispatch(UiDebugAction::ShowLogo);
            return true;
        default:
            return false;
        }
    }

    bool handle_simulator_hotkey(SDL_Keycode key, SimulatorApp &app)
    {
        switch (key)
        {
        case SDLK_b:
            app.execute(SimulatorCommand::ToggleBleState);
            return true;
        case SDLK_w:
            app.execute(SimulatorCommand::CycleWifiState);
            return true;
        case SDLK_UP:
            app.execute(SimulatorCommand::IncreaseRpm);
            return true;
        case SDLK_DOWN:
            app.execute(SimulatorCommand::DecreaseRpm);
            return true;
        case SDLK_s:
            app.execute(SimulatorCommand::ToggleShift);
            return true;
        case SDLK_SPACE:
            app.execute(SimulatorCommand::ToggleAnimation);
            return true;
        case SDLK_r:
            app.execute(SimulatorCommand::ResetState);
            return true;
        default:
            return false;
        }
    }
}

int main()
{
    const TelemetryServiceConfig telemetryConfig = load_telemetry_config();
    const std::string captureFramePath = env_string_value("SIM_CAPTURE_FRAME_PATH");
    const bool exitOnCapture = env_flag_enabled("SIM_EXIT_ON_CAPTURE", false);
    lv_init();

    SdlLvglWindow window;
    if (!window.init({}))
    {
        std::cerr << "Failed to initialize SDL/LVGL window: " << SDL_GetError() << '\n';
        return 1;
    }

    SimulatorApp app;
    app.configureTelemetry(telemetryConfig);
    UiDisplayHooks hooks{};
    hooks.userData = &app;
    hooks.setBrightness = [](uint8_t value, void *userData)
    {
        static_cast<SimulatorApp *>(userData)->setBrightness(value);
    };
    hooks.saveSettings = [](const UiSettings &settings, void *userData)
    {
        static_cast<SimulatorApp *>(userData)->saveSettings(settings);
    };

    ui_s3_init(window.display(), hooks, app.state());

    std::cout << "Telemetry mode: "
              << (telemetryConfig.mode == TelemetryInputMode::Simulator ? "internal simulator" : "SimHub")
              << '\n';
    if (telemetryConfig.mode == TelemetryInputMode::SimHub)
    {
        std::cout << "SimHub transport: "
                  << (telemetryConfig.simHubTransport == SimHubTransport::HttpApi ? "HTTP API" : "JSON UDP")
                  << '\n';
        if (telemetryConfig.simHubTransport == SimHubTransport::HttpApi)
        {
            std::cout << "SimHub API port: " << telemetryConfig.httpPort << '\n';
        }
        else
        {
            std::cout << "SimHub UDP port: " << telemetryConfig.udpPort << '\n';
        }
    }
    if (!captureFramePath.empty())
    {
        std::cout << "Frame capture path: " << captureFramePath << '\n';
    }
    std::cout << "ShiftLight simulator controls:\n";
    std::cout << "  Mouse drag/click: navigate LVGL UI\n";
    std::cout << "  Left/Right: switch cards\n";
    std::cout << "  Enter: open selected card, Esc: home, L: logo\n";
    std::cout << "  B: BLE state, W: WiFi state, Up/Down: RPM, S: shift, Space: pause RPM, R: reset\n";

    bool running = true;
    bool capturedFrame = false;
    uint32_t captureEligibleSinceMs = 0;
    while (running)
    {
        SDL_Event event{};
        while (SDL_PollEvent(&event))
        {
            if (event.type == SDL_KEYDOWN && event.key.repeat == 0)
            {
                if (!handle_ui_hotkey(event.key.keysym.sym))
                {
                    handle_simulator_hotkey(event.key.keysym.sym, app);
                }
            }

            if (!window.processEvent(event))
            {
                running = false;
            }
        }

        window.pumpTime();
        const uint32_t nowMs = SDL_GetTicks();
        app.tick(nowMs);
        ui_s3_loop(app.state());
        lv_timer_handler();
        window.render();

        const bool captureReady = !captureFramePath.empty() && should_capture_frame(telemetryConfig, app.state());
        if (captureReady)
        {
            if (captureEligibleSinceMs == 0)
            {
                captureEligibleSinceMs = nowMs;
            }
        }
        else
        {
            captureEligibleSinceMs = 0;
        }

        if (!capturedFrame &&
            captureReady &&
            captureEligibleSinceMs != 0 &&
            nowMs - captureEligibleSinceMs >= kCaptureSettlingMs)
        {
            try
            {
                std::filesystem::path outputPath(captureFramePath);
                if (outputPath.has_parent_path())
                {
                    std::filesystem::create_directories(outputPath.parent_path());
                }

                if (window.saveFramebufferBmp(captureFramePath.c_str()))
                {
                    capturedFrame = true;
                    std::cout << "Captured LVGL frame to " << captureFramePath << '\n';
                    if (exitOnCapture)
                    {
                        running = false;
                    }
                }
                else
                {
                    std::cerr << "Failed to capture LVGL frame to " << captureFramePath << '\n';
                }
            }
            catch (const std::exception &ex)
            {
                std::cerr << "Failed to prepare capture path " << captureFramePath << ": " << ex.what() << '\n';
            }
        }
        SDL_Delay(5);
    }

    return 0;
}
