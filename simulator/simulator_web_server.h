#pragma once

#include <atomic>
#include <cstdint>
#include <thread>

class SimulatorApp;

class SimulatorWebServer
{
public:
    explicit SimulatorWebServer(SimulatorApp &app);
    ~SimulatorWebServer();

    bool start(uint16_t port);
    void stop();

    bool isRunning() const;
    uint16_t port() const;

private:
    void run();

    SimulatorApp &app_;
    std::atomic<bool> stopRequested_{false};
    std::atomic<bool> running_{false};
    std::thread worker_{};
    intptr_t listenSocket_ = -1;
    uint16_t port_ = 0;
#ifdef _WIN32
    bool winsockReady_ = false;
#endif
};
