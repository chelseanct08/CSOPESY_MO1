#ifndef SCHEDULER_H
#define SCHEDULER_H

#include "Config.h"
#include "Process.h"

#include <atomic>
#include <condition_variable>
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
    std::vector<std::shared_ptr<Process>> getProcesses() const;

private:
    Config config;

    std::queue<std::shared_ptr<Process>> readyQueue;
    std::vector<std::shared_ptr<Process>> processes;
    std::vector<std::thread> workers;

    mutable std::mutex queueMutex;
    std::condition_variable queueCondition;
    mutable std::mutex processesMutex;

    std::atomic<bool> running;

    void workerFunction();
};

#endif
