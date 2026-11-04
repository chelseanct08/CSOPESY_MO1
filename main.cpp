#include "Config.h"
#include "Process.h"
#include "Scheduler.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <conio.h>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace {
    std::vector<Instruction> generateInstructions(
        const std::string& name,
        const Config& config
    ) {
        thread_local std::mt19937 generator(std::random_device{}());

        std::uniform_int_distribution<std::uint64_t> countDistribution(
            config.minIns, config.maxIns
        );
        std::uniform_int_distribution<int> operationDistribution(0, 3);
        std::uniform_int_distribution<int> variableDistribution(1, 5);
        std::uniform_int_distribution<int> valueDistribution(0, 100);
        std::uniform_int_distribution<std::uint64_t> sleepDistribution(1, 3);

        const std::uint64_t instructionCount = countDistribution(generator);
        std::vector<Instruction> instructions;

        const std::uint64_t greetingCount = std::min<std::uint64_t>(3, instructionCount);
        for (std::uint64_t index = 0; index < greetingCount; ++index) {
            instructions.push_back(
                Instruction::makePrint("Hello world from " + name + "!")
            );
        }

        for (std::uint64_t index = greetingCount; index < instructionCount; ++index) {
            const int operation = operationDistribution(generator);
            const std::string destination =
                "v" + std::to_string(variableDistribution(generator));
            const std::string source =
                "v" + std::to_string(variableDistribution(generator));
            const int value = valueDistribution(generator);

            if (operation == 0) {
                instructions.push_back(
                    Instruction::makeDeclare(destination, value)
                );
            }
            else if (operation == 1) {
                instructions.push_back(
                    Instruction::makeAdd(destination, Operand(source), Operand(value))
                );
            }
            else if (operation == 2) {
                instructions.push_back(
                    Instruction::makeSubtract(destination, Operand(source), Operand(value))
                );
            }
            else {
                instructions.push_back(
                    Instruction::makeSleep(sleepDistribution(generator))
                );
            }
        }

        return instructions;
    }

    std::string getProcessStatus(Process::State state) {
        switch (state) {
        case Process::READY:
            return "Ready";
        case Process::RUNNING:
            return "Running";
        case Process::WAITING:
            return "Waiting";
        case Process::FINISHED:
            return "Finished";
        default:
            return "Unknown";
        }
    }

    void sortProcesses(std::vector<std::shared_ptr<Process>>& processes) {
        std::sort(
            processes.begin(),
            processes.end(),
            [](const std::shared_ptr<Process>& first,
                const std::shared_ptr<Process>& second) {
                    return first->getName() < second->getName();
            }
        );
    }

    void clearConsole() {
        std::system("cls");
    }

    void printMainMenu() {
        std::cout
            << "    ______ _____ ____  ____  _____________  __\n"
            << "   / ____ / ___// __ \\/ __ \\/ ____/ ___/\\ \\/ /\n"
            << "  / /     \\__ \\/ / / / /_/ / __ / \\__ \\  \\  /\n"
            << " / /___  ___/ / /_/ / ____/ /___ ___/ /  / /\n"
            << " \\____/ /____/\\____/_/   /_____//____/  /_/ \n\n"  

            << "Welcome to CSOPESY Emulator!\n\n"
            << "Developers: \n"
            << "    Hernaez \n"
            << "    Liwanag \n"
            << "    Serrano \n"
            << "    Tamayo \n\n"
            << "Last updated: 11-04-2026\n\n"
            << "Type initialize to begin.\n";
    }

    bool readCommand(
        std::string& command,
        const std::string& prompt,
        const std::function<void(const std::string&)>& refresh = {}
    ) {
        command.clear();
        std::cout << prompt << std::flush;
        auto lastRefresh = std::chrono::steady_clock::now();

        while (true) {
            if (_kbhit()) {
                const int character = _getch();

                if (character == '\r') {
                    std::cout << '\n';
                    return true;
                }

                if (character == '\b') {
                    if (!command.empty()) {
                        command.pop_back();
                        std::cout << "\b \b" << std::flush;
                    }
                }
                else if (character == 0 || character == 224) {
                    _getch();
                }
                else if (character >= 32 && character <= 126) {
                    command += static_cast<char>(character);
                    std::cout << static_cast<char>(character) << std::flush;
                }
            }

            const auto now = std::chrono::steady_clock::now();
            if (refresh
                && now - lastRefresh >= std::chrono::milliseconds(250)) {
                refresh(command);
                lastRefresh = now;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

    void renderProcessScreen(
        const std::shared_ptr<Process>& process,
        const Scheduler& scheduler,
        const std::string& command
    ) {
        const Process::State state = process->getState();
        const std::size_t progress = process->getCurrentInstruction();
        const std::size_t total = process->getInstructionCount();
        const std::vector<std::string> logs = process->getLogs();

        clearConsole();
        std::cout << "========== Process Screen ==========\n";
        std::cout << "Process name: " << process->getName() << '\n';
        std::cout << "ID: " << process->getPid() << '\n';
        std::cout << "Status: " << getProcessStatus(state) << '\n';
        std::cout << "Current instruction line: " << progress << '\n';
        std::cout << "Lines of code: " << total << '\n';
        std::cout << "CPU ticks: " << scheduler.getCpuTicks() << '\n';
        if (state == Process::WAITING) {
            std::cout << "Sleep ticks remaining: "
                << process->getRemainingSleepTicks() << '\n';
        }

        std::cout << "\nLogs:\n";
        for (const std::string& message : logs) {
            std::cout << message << '\n';
        }

        std::cout << "\nCommands: process-smi, exit\n";
        std::cout << "process> " << command << std::flush;
    }

    void openProcessScreen(
        const std::shared_ptr<Process>& process,
        const Scheduler& scheduler
    ) {
        clearConsole();
        std::string command;
        Process::State previousState = static_cast<Process::State>(-1);
        std::size_t previousProgress = static_cast<std::size_t>(-1);
        std::size_t previousLogCount = static_cast<std::size_t>(-1);
        std::uint64_t previousTick = std::numeric_limits<std::uint64_t>::max();

        while (true) {
            const auto refresh = [&](const std::string& currentCommand) {
                const Process::State state = process->getState();
                const std::size_t progress = process->getCurrentInstruction();
                const std::size_t logCount = process->getLogs().size();
                const std::uint64_t tick = scheduler.getCpuTicks();

                if (state != previousState || progress != previousProgress
                    || logCount != previousLogCount || tick != previousTick) {
                    renderProcessScreen(process, scheduler, currentCommand);
                    previousState = state;
                    previousProgress = progress;
                    previousLogCount = logCount;
                    previousTick = tick;
                }
                };

            if (!readCommand(command, "process> ", refresh)) {
                return;
            }

            if (command == "exit") {
                return;
            }
            if (command == "process-smi") {
                previousState = static_cast<Process::State>(-1);
                refresh("");
            }
            else {
                std::cout << "Unknown process screen command: " << command << '\n';
                std::this_thread::sleep_for(std::chrono::milliseconds(700));
                previousState = static_cast<Process::State>(-1);
                refresh("");
            }
        }
    }

    std::string buildUtilizationReport(
        const std::vector<std::shared_ptr<Process>>& processList,
        const Scheduler& scheduler
    ) {
        std::ostringstream report;
        std::size_t runningCount = 0;
        std::size_t readyCount = 0;
        std::size_t waitingCount = 0;
        std::size_t finishedCount = 0;

        for (const std::shared_ptr<Process>& process : processList) {
            switch (process->getState()) {
            case Process::RUNNING:
                ++runningCount;
                break;
            case Process::READY:
                ++readyCount;
                break;
            case Process::WAITING:
                ++waitingCount;
                break;
            case Process::FINISHED:
                ++finishedCount;
                break;
            }
        }

        report << std::fixed << std::setprecision(1);
        report << "CPU utilization: " << scheduler.getCpuUtilization() << "%\n";
        report << "CPU cores used: " << scheduler.getUsedCoreCount() << '\n';
        report << "CPU cores available: "
            << scheduler.getAvailableCoreCount() << '\n';
        report << "CPU ticks: " << scheduler.getCpuTicks() << "\n\n";
        report << "Process Name\tPID\tStatus\tInstructions\n";

        for (const std::shared_ptr<Process>& process : processList) {
            report << process->getName() << '\t'
                << process->getPid() << '\t'
                << getProcessStatus(process->getState()) << '\t'
                << process->getCurrentInstruction() << " / "
                << process->getInstructionCount() << '\n';
        }

        report << "\nRunning processes: " << runningCount << '\n';
        report << "Ready processes: " << readyCount << '\n';
        report << "Waiting processes: " << waitingCount << '\n';
        report << "Finished processes: " << finishedCount << '\n';
        return report.str();
    }
}

int main() {
    Config config;
    std::unique_ptr<Scheduler> scheduler;
    std::unordered_map<std::string, std::shared_ptr<Process>> screens;
    std::mutex screensMutex;

    std::atomic<bool> generating(false);
    std::thread generatorThread;
    std::uint64_t nextProcessNumber = 1;
    bool initialized = false;
    bool running = true;

    printMainMenu();

    std::string command;
    while (running) {
        if (!readCommand(command, "\nCSOPESY> ")) {
            break;
        }

        std::istringstream input(command);
        std::string action;
        input >> action;
        if (action.empty()) {
            continue;
        }

        if (action == "exit") {
            running = false;
            continue;
        }

        if (action == "initialize") {
            if (initialized) {
                std::cout << "System is already initialized.\n";
                continue;
            }

            if (!config.loadConfig("config.txt")) {
                std::cout
                    << "Could not initialize. Check config.txt "
                    << "and the program's working directory.\n";
                continue;
            }

            scheduler = std::make_unique<Scheduler>(config);
            scheduler->start();
            initialized = true;
            std::cout << "System initialized successfully.\n";
            continue;
        }

        if (!initialized) {
            std::cout << "Please run initialize before using this command.\n";
            continue;
        }

        if (action == "screen") {
            std::string option;
            std::string name;
            input >> option >> name;

            if (option == "-s") {
                if (name.empty()) {
                    std::cout << "Usage: screen -s <process_name>\n";
                    continue;
                }

                std::shared_ptr<Process> process;
                {
                    std::lock_guard<std::mutex> lock(screensMutex);
                    if (screens.find(name) != screens.end()) {
                        std::cout << "A process with that name already exists.\n";
                        continue;
                    }

                    process = std::make_shared<Process>(
                        name, generateInstructions(name, config)
                    );
                    screens.emplace(name, process);
                }

                scheduler->addProcess(process);
                openProcessScreen(process, *scheduler);
                clearConsole();
                printMainMenu();
            }
            else if (option == "-r") {
                if (name.empty()) {
                    std::cout << "Usage: screen -r <process_name>\n";
                    continue;
                }

                std::shared_ptr<Process> process;
                {
                    std::lock_guard<std::mutex> lock(screensMutex);
                    const auto found = screens.find(name);
                    if (found != screens.end()) {
                        process = found->second;
                    }
                }

                if (!process || process->isFinished()) {
                    std::cout << "Process " << name << " not found.\n";
                    continue;
                }

                openProcessScreen(process, *scheduler);
                clearConsole();
                printMainMenu();
            }
            else if (option == "-ls") {
                std::vector<std::shared_ptr<Process>> processList =
                    scheduler->getProcesses();
                sortProcesses(processList);
                std::cout << buildUtilizationReport(processList, *scheduler);
            }
            else {
                std::cout
                    << "Usage: screen -s <process_name>, "
                    << "screen -r <process_name>, or screen -ls\n";
            }
        }
        else if (action == "scheduler-start") {
            if (generating.exchange(true)) {
                std::cout << "Process generation is already running.\n";
                continue;
            }

            if (generatorThread.joinable()) {
                generatorThread.join();
            }

            generatorThread = std::thread([&]() {
                std::uint64_t nextTick =
                    scheduler->getCpuTicks() + config.batchProcessFreq;

                while (scheduler->waitForCpuTicks(nextTick, generating)) {
                    if (!generating) {
                        break;
                    }

                    const std::uint64_t processNumber = nextProcessNumber++;
                    std::ostringstream processName;
                    processName << 'p' << std::setw(2) << std::setfill('0')
                        << processNumber;
                    std::string name = processName.str();
                    auto process = std::make_shared<Process>(
                        name, generateInstructions(name, config)
                    );

                    if (!generating) {
                        break;
                    }

                    {
                        std::lock_guard<std::mutex> lock(screensMutex);
                        while (screens.find(name) != screens.end()) {
                            processName.str(std::string());
                            processName.clear();
                            processName << 'p' << std::setw(2)
                                << std::setfill('0') << nextProcessNumber++;
                            name = processName.str();
                            process = std::make_shared<Process>(
                                name, generateInstructions(name, config)
                            );
                        }
                        screens.emplace(name, process);
                    }

                    scheduler->addProcess(process);
                    nextTick += config.batchProcessFreq;
                }
                });

            std::cout << "Automatic process generation started.\n";
        }
        else if (action == "scheduler-stop") {
            if (!generating.exchange(false)) {
                std::cout << "Automatic process generation is not running.\n";
                continue;
            }

            scheduler->notifyTickWaiters();
            if (generatorThread.joinable()) {
                generatorThread.join();
            }
            std::cout << "Automatic process generation stopped.\n";
        }
        else if (action == "report-util") {
            std::vector<std::shared_ptr<Process>> processList =
                scheduler->getProcesses();
            sortProcesses(processList);
            const std::string report =
                buildUtilizationReport(processList, *scheduler);
            std::ofstream output("csopesy-log.txt");

            if (!output.is_open()) {
                std::cout << "Could not create csopesy-log.txt.\n";
                continue;
            }

            output << report;
            if (!output) {
                std::cout << "Could not write csopesy-log.txt.\n";
                continue;
            }

            std::cout << report;
            std::cout << "Saved report to csopesy-log.txt.\n";
        }
        else {
            std::cout << "Unknown command: " << action << '\n';
        }
    }

    if (generating.exchange(false)) {
        scheduler->notifyTickWaiters();
    }
    if (generatorThread.joinable()) {
        generatorThread.join();
    }
    if (scheduler) {
        scheduler->stop();
    }

    std::cout << "Exiting CSOPESY CLI.\n";
    return 0;
}
