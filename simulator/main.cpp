#include <iostream>
#include <cstdlib>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <sstream>
#include <string>
#include <vector>

#include <SDL.h>
#include <lvgl.h>

#include "simulator_app.h"
#include "simulator_web_server.h"
#include "sdl_lvgl_window.h"
#include "telemetry/telemetry_types.h"
#include "ui/ui_s3_main.h"
#include "virtual_led_bar.h"

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

    bool env_has_value(const char *name)
    {
        const char *value = std::getenv(name);
        return value != nullptr && value[0] != '\0';
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

    int env_int_value(const char *name, int defaultValue, int minValue, int maxValue)
    {
        const char *value = std::getenv(name);
        if (!value)
        {
            return defaultValue;
        }

        try
        {
            const int parsed = std::stoi(value);
            if (parsed >= minValue && parsed <= maxValue)
            {
                return parsed;
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

    TelemetryServiceConfig apply_telemetry_env_overrides(const TelemetryServiceConfig &baseConfig)
    {
        TelemetryServiceConfig config = baseConfig;
        if (env_has_value("SIM_MODE"))
        {
            config.mode = env_flag_enabled("SIM_MODE", false) ? TelemetryInputMode::Simulator : TelemetryInputMode::SimHub;
        }
        if (env_has_value("SIMHUB_HTTP_PORT"))
        {
            config.httpPort = env_port_value("SIMHUB_HTTP_PORT", config.httpPort);
        }
        if (env_has_value("SIMHUB_UDP_PORT"))
        {
            config.udpPort = env_port_value("SIMHUB_UDP_PORT", config.udpPort);
        }
        if (env_has_value("SIMHUB_POLL_INTERVAL_MS"))
        {
            config.pollIntervalMs = std::max<uint32_t>(15U, env_port_value("SIMHUB_POLL_INTERVAL_MS", static_cast<uint16_t>(config.pollIntervalMs)));
        }
        if (env_has_value("SIM_DEBUG_TELEMETRY"))
        {
            config.debugLogging = env_flag_enabled("SIM_DEBUG_TELEMETRY", config.debugLogging);
        }
        if (env_has_value("SIM_ALLOW_FALLBACK_SIMULATOR"))
        {
            config.allowSimulatorFallback = env_flag_enabled("SIM_ALLOW_FALLBACK_SIMULATOR", config.allowSimulatorFallback);
        }

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
            return state.telemetryUsingFallback ||
                   (state.telemetrySource == UiTelemetrySource::SimHubNetwork && !state.telemetryStale);
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

    void dispatch_startup_action(const std::string &rawToken)
    {
        std::string token = rawToken;
        std::transform(token.begin(), token.end(), token.begin(), [](unsigned char ch)
                       { return static_cast<char>(std::tolower(ch)); });

        if (token == "left" || token == "prev" || token == "previous")
        {
            ui_s3_debug_dispatch(UiDebugAction::PreviousCard);
        }
        else if (token == "right" || token == "next")
        {
            ui_s3_debug_dispatch(UiDebugAction::NextCard);
        }
        else if (token == "enter" || token == "open" || token == "select")
        {
            ui_s3_debug_dispatch(UiDebugAction::OpenSelectedCard);
        }
        else if (token == "esc" || token == "home")
        {
            ui_s3_debug_dispatch(UiDebugAction::GoHome);
        }
        else if (token == "logo" || token == "showlogo")
        {
            ui_s3_debug_dispatch(UiDebugAction::ShowLogo);
        }
    }

    void apply_startup_actions(const std::string &actions)
    {
        if (actions.empty())
        {
            return;
        }

        std::stringstream parser(actions);
        std::string token;
        while (std::getline(parser, token, ','))
        {
            token.erase(std::remove_if(token.begin(), token.end(), [](unsigned char ch)
                                       { return std::isspace(ch) != 0; }),
                        token.end());
            if (!token.empty())
            {
                dispatch_startup_action(token);
            }
        }
    }
}

int main()
{
    try
    {
        const std::string captureFramePath = env_string_value("SIM_CAPTURE_FRAME_PATH");
        const std::string startupActions = env_string_value("SIM_UI_ACTIONS");
        const bool exitOnCapture = env_flag_enabled("SIM_EXIT_ON_CAPTURE", false);
        const bool webEnabled = env_flag_enabled("SIM_WEB_ENABLED", true);
        const uint16_t webPort = env_port_value("SIM_WEB_PORT", 8765);
        const bool showLedBarPreview = env_flag_enabled("SIM_SHOW_LED_BAR", true);
        const int ledBarPreviewHeight = env_int_value("SIM_LED_BAR_HEIGHT", 92, 56, 220);
        const int windowWidth = env_int_value("SIM_WINDOW_WIDTH", 800, 320, 3840);
        const int windowHeight = env_int_value("SIM_WINDOW_HEIGHT", 400, 240, 2160);
        const int windowScale = env_int_value("SIM_WINDOW_SCALE", 1, 1, 4);
        lv_init();

        SdlLvglWindow window;
        SdlLvglWindowConfig windowConfig{};
        windowConfig.width = windowWidth;
        windowConfig.height = windowHeight;
        windowConfig.scale = windowScale;
        windowConfig.showLedBarPreview = showLedBarPreview;
        windowConfig.ledBarPreviewHeight = ledBarPreviewHeight;
        if (!window.init(windowConfig))
        {
            std::cerr << "Failed to initialize SDL/LVGL window: " << SDL_GetError() << '\n';
            return 1;
        }

        SimulatorApp app;
        const TelemetryServiceConfig telemetryConfig = apply_telemetry_env_overrides(app.telemetryConfigSnapshot());
        app.configureTelemetry(telemetryConfig);
        app.setWebServerPort(webPort);
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

        SimulatorWebServer webServer(app);
        if (webEnabled && !webServer.start(webPort))
        {
            std::cerr << "Failed to start simulator web server on http://127.0.0.1:" << webPort << '\n';
        }

        ui_s3_init(window.display(), hooks, app.stateSnapshot());
        apply_startup_actions(startupActions);

        std::cout << "Telemetry mode: ";
        if (telemetryConfig.mode == TelemetryInputMode::Simulator)
        {
            std::cout << "internal simulator";
        }
        else if (telemetryConfig.allowSimulatorFallback)
        {
            std::cout << "auto (SimHub preferred, simulator fallback until live data arrives)";
        }
        else
        {
            std::cout << "SimHub only";
        }
        std::cout << '\n';
        std::cout << "Display: " << windowWidth << "x" << windowHeight << " scale " << windowScale << '\n';
        std::cout << "Window: " << window.windowWidth() << "x" << window.windowHeight() << '\n';
        if (showLedBarPreview)
        {
            std::cout << "Virtual LED bar preview: enabled (" << ledBarPreviewHeight << " px)\n";
        }
        if (webEnabled)
        {
            std::cout << "Simulator web UI: http://127.0.0.1:" << webPort << '\n';
        }
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
        if (!startupActions.empty())
        {
            std::cout << "Startup UI actions: " << startupActions << '\n';
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

            const std::vector<UiDebugAction> pendingUiActions = app.takePendingUiActions();
            for (UiDebugAction action : pendingUiActions)
            {
                ui_s3_debug_dispatch(action);
            }

            app.tick(nowMs);
            const UiRuntimeState currentState = app.stateSnapshot();
            ui_s3_loop(currentState);
            lv_timer_handler();
            app.updateUiDebugSnapshot(ui_s3_debug_snapshot());
            window.setDisplayBrightness(static_cast<uint8_t>(std::clamp(currentState.settings.displayBrightness, 0, 255)));
            window.setLedBarPreview(build_virtual_led_bar_frame(currentState, app.ledBarConfigSnapshot(), nowMs));
            window.render();

            const TelemetryServiceConfig currentTelemetryConfig = app.telemetryConfigSnapshot();
            const bool captureReady = !captureFramePath.empty() && should_capture_frame(currentTelemetryConfig, currentState);
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

        webServer.stop();
        return 0;
    }
    catch (const std::exception &ex)
    {
        std::cerr << "[sim] exception: " << ex.what() << '\n';
        return 1;
    }
}
