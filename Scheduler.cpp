#include "Scheduler.h"

#include <chrono>

Scheduler::Scheduler(const Config& config)
    : config(config),
    running(false),
    cpuTicks(0),
    activeCores(0) {
}

Scheduler::~Scheduler() {
    stop();
}

void Scheduler::addProcess(const std::shared_ptr<Process>& process) {
    if (!process || process->isFinished() || !running) {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(processesMutex);
        processes.push_back(process);
    }

    {
        std::lock_guard<std::mutex> lock(queueMutex);
        readyQueue.push(process);
    }

    queueCondition.notify_one();
}

void Scheduler::start() {
    bool expected = false;
    if (!running.compare_exchange_strong(expected, true)) {
        return;
    }

    clockThread = std::thread(&Scheduler::clockFunction, this);
    workers.reserve(config.numCpu);
    for (std::uint32_t core = 0; core < config.numCpu; ++core) {
        workers.emplace_back(&Scheduler::workerFunction, this);
    }
}

void Scheduler::stop() {
    if (!running.exchange(false)) {
        return;
    }

    queueCondition.notify_all();
    tickCondition.notify_all();

    if (clockThread.joinable()) {
        clockThread.join();
    }

    for (std::thread& worker : workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    workers.clear();
}

bool Scheduler::isRunning() const {
    return running.load();
}

std::uint64_t Scheduler::getCpuTicks() const {
    return cpuTicks.load();
}

std::uint32_t Scheduler::getUsedCoreCount() const {
    return activeCores.load();
}

std::uint32_t Scheduler::getAvailableCoreCount() const {
    const std::uint32_t used = activeCores.load();
    return used >= config.numCpu ? 0 : config.numCpu - used;
}

double Scheduler::getCpuUtilization() const {
    if (config.numCpu == 0) {
        return 0.0;
    }

    return static_cast<double>(activeCores.load()) * 100.0
        / static_cast<double>(config.numCpu);
}

std::vector<std::shared_ptr<Process>> Scheduler::getProcesses() const {
    std::lock_guard<std::mutex> lock(processesMutex);
    return processes;
}

bool Scheduler::waitForCpuTicks(
    std::uint64_t targetTick,
    const std::atomic<bool>& cancel
) const {
    std::unique_lock<std::mutex> lock(tickMutex);
    tickCondition.wait(lock, [this, targetTick, &cancel]() {
        return cpuTicks.load() >= targetTick
            || !running.load()
            || !cancel.load();
        });

    return cpuTicks.load() >= targetTick && running.load() && cancel.load();
}

void Scheduler::notifyTickWaiters() {
    tickCondition.notify_all();
}

bool Scheduler::waitForCpuTick(
    std::uint64_t targetTick,
    std::uint64_t& observedTick
) {
    std::unique_lock<std::mutex> lock(tickMutex);
    tickCondition.wait(lock, [this, targetTick]() {
        return cpuTicks.load() >= targetTick || !running.load();
        });

    if (!running) {
        return false;
    }

    observedTick = cpuTicks.load();
    return observedTick >= targetTick;
}

void Scheduler::clockFunction() {
    while (running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        if (!running) {
            break;
        }

        cpuTicks.fetch_add(1);
        const std::vector<std::shared_ptr<Process>> processSnapshot =
            getProcesses();

        bool processBecameReady = false;
        for (const std::shared_ptr<Process>& process : processSnapshot) {
            if (process->tickSleep()) {
                std::lock_guard<std::mutex> lock(queueMutex);
                readyQueue.push(process);
                processBecameReady = true;
            }
        }

        if (processBecameReady) {
            queueCondition.notify_all();
        }
        tickCondition.notify_all();
    }
}

void Scheduler::workerFunction() {
    while (running) {
        std::shared_ptr<Process> process;

        {
            std::unique_lock<std::mutex> lock(queueMutex);
            queueCondition.wait(lock, [this]() {
                return !running || !readyQueue.empty();
                });

            if (!running) {
                return;
            }

            process = readyQueue.front();
            readyQueue.pop();
        }

        if (!process || process->isFinished()
            || process->getState() != Process::READY) {
            continue;
        }

        process->setState(Process::RUNNING);
        activeCores.fetch_add(1);
        std::uint64_t executedTicks = 0;
        std::uint64_t observedTick = cpuTicks.load();
        std::uint64_t nextExecutionTick = observedTick + 1;

        while (running) {
            if (!waitForCpuTick(nextExecutionTick, observedTick)) {
                break;
            }

            process->executeNextInstruction();
            ++executedTicks;

            const Process::State state = process->getState();
            if (state == Process::FINISHED || state == Process::WAITING) {
                break;
            }

            if (config.scheduler == "rr"
                && executedTicks >= config.quantumCycles) {
                break;
            }

            nextExecutionTick = observedTick
                + (config.delaysPerExec == 0
                    ? 1
                    : config.delaysPerExec);
        }

        activeCores.fetch_sub(1);

        if (process->getState() == Process::RUNNING) {
            process->setState(Process::READY);
        }

        if (running && process->getState() == Process::READY) {
            {
                std::lock_guard<std::mutex> lock(queueMutex);
                readyQueue.push(process);
            }
            queueCondition.notify_one();
        }
    }
}
