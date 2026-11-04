#include "Scheduler.h"

#include <chrono>

Scheduler::Scheduler(const Config& config)
    : config(config), running(false) {
}

Scheduler::~Scheduler() {
    stop();
}

void Scheduler::addProcess(const std::shared_ptr<Process>& process) {
    if (!process || process->isFinished()) {
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

    for (int i = 0; i < config.numCpu; i++) {
        workers.emplace_back(&Scheduler::workerFunction, this);
    }
}

void Scheduler::stop() {
    running = false;
    queueCondition.notify_all();

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

std::vector<std::shared_ptr<Process>> Scheduler::getProcesses() const {
    std::lock_guard<std::mutex> lock(processesMutex);
    return processes;
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

        if (!process || process->isFinished()) {
            continue;
        }

        if (process->getState() == Process::WAITING) {
            process->tickSleep();

            if (process->isFinished()) {
                continue;
            }

            if (process->getState() == Process::WAITING) {
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(1)
                );
            }

            {
                std::lock_guard<std::mutex> lock(queueMutex);
                readyQueue.push(process);
            }

            queueCondition.notify_one();
            continue;
        }

        process->setState(Process::RUNNING);

        int instructionsToRun;

        if (config.scheduler == "rr") {
            instructionsToRun = config.quantumCycles;
        }
        else {
            instructionsToRun = process->getInstructionCount();
        }

        for (int i = 0; i < instructionsToRun && running; i++) {
            if (process->isFinished()) {
                break;
            }

            process->executeNextInstruction();

            if (process->getState() == Process::WAITING) {
                break;
            }

            if (config.delaysPerExec > 0) {
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(config.delaysPerExec)
                );
            }
        }

        if (process->isFinished()) {
            continue;
        }

        if (process->getState() == Process::RUNNING) {
            process->setState(Process::READY);
        }

        {
            std::lock_guard<std::mutex> lock(queueMutex);
            readyQueue.push(process);
        }

        queueCondition.notify_one();
    }
}
