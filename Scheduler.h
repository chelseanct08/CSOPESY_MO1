#ifndef SCHEDULER_H
#define SCHEDULER_H

#include "Config.h"
#include "Process.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

class Scheduler {
public:
    explicit Scheduler(const Config& config);
    ~Scheduler();

    void addProcess(const std::shared_ptr<Process>& process);
    void start();
    void stop();

    bool isRunning() const;
    std::uint64_t getCpuTicks() const;
    std::uint32_t getUsedCoreCount() const;
    std::uint32_t getAvailableCoreCount() const;
    double getCpuUtilization() const;
    std::vector<std::shared_ptr<Process>> getProcesses() const;
    bool waitForCpuTicks(
        std::uint64_t targetTick,
        const std::atomic<bool>& cancel
    ) const;
    void notifyTickWaiters();

private:
    Config config;

    std::queue<std::shared_ptr<Process>> readyQueue;
    std::vector<std::shared_ptr<Process>> processes;
    std::vector<std::thread> workers;
    std::thread clockThread;

    mutable std::mutex queueMutex;
    std::condition_variable queueCondition;
    mutable std::mutex processesMutex;
    mutable std::mutex tickMutex;
    mutable std::condition_variable tickCondition;

    std::atomic<bool> running;
    std::atomic<std::uint64_t> cpuTicks;
    std::atomic<std::uint32_t> activeCores;

    void workerFunction();
    void clockFunction();
    bool waitForCpuTick(
        std::uint64_t targetTick,
        std::uint64_t& observedTick
    );
};

#endif
