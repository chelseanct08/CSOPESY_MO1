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
#include <map>
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

        const std::uint64_t instructionCount = countDistribution(generator);
        std::vector<Instruction> instructions;

        // The greetings and required FOR sequence establish a 603-line minimum.
        constexpr std::uint64_t greetingCount = 3;
        constexpr std::uint64_t requiredForInstructionCount = 603;
        for (std::uint64_t index = 0; index < greetingCount; ++index) {
            instructions.push_back(
                Instruction::makePrint("Hello world from " + name + "!")
            );
        }

        std::vector<Instruction> loopBody = {
            Instruction::makeAdd("x", Operand("x"), Operand(1)),
            Instruction::makePrintVariable("Value from: ", "x"),
            Instruction::makeAdd("y", Operand("y"), Operand(1)),
            Instruction::makePrintVariable("Value from: ", "y"),
            Instruction::makeAdd("z", Operand("z"), Operand(1)),
            Instruction::makePrintVariable("Value from: ", "z")
        };
        instructions.push_back(Instruction::makeFor(loopBody, 100));

        const std::uint64_t paddingCount =
            instructionCount > requiredForInstructionCount
            ? instructionCount - requiredForInstructionCount
            : 0;
        if (paddingCount > 0) {
            instructions.push_back(
                Instruction::makeFor(
                    { Instruction::makeSleep(0) },
                    paddingCount
                )
            );
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
                && now - lastRefresh >= std::chrono::milliseconds(100)) {
                refresh(command);
                lastRefresh = now;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

    void printProcessInformation(const std::shared_ptr<Process>& process);

    void renderProcessScreen(
        const std::shared_ptr<Process>& process,
        const std::string& command,
        bool showProcessInformation,
        bool showPrompt = false
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
        std::cout << "\nLogs:\n";
        for (const std::string& message : logs) {
            std::cout << message << '\n';
        }

        std::cout << "\nCurrent instruction line: " << progress << '\n';
        std::cout << "Lines of code: " << total << '\n';
        if (showProcessInformation) {
            printProcessInformation(process);
        }

        std::cout << "\nCommands: process-smi, exit\n";
        if (showPrompt) {
            std::cout << "process> " << command << std::flush;
        }
    }

    void printProcessInformation(const std::shared_ptr<Process>& process) {
        std::cout << "\n========== PROCESS INFORMATION ==========\n";
        std::cout << "Process name: " << process->getName() << '\n';
        std::cout << "PID: " << process->getPid() << '\n';
        std::cout << "Status: " << getProcessStatus(process->getState()) << '\n';
        std::cout << "Current instruction: "
            << process->getCurrentInstruction() << '\n';
        std::cout << "Total instructions: "
            << process->getInstructionCount() << '\n';

        std::map<std::string, std::uint16_t> variables;
        for (const auto& variable : process->getVariables()) {
            variables.emplace(variable.first, variable.second);
        }

        std::cout << "Variables:\n";
        for (const auto& variable : variables) {
            std::cout << variable.first << " = " << variable.second << '\n';
        }
    }

    void openProcessScreen(const std::shared_ptr<Process>& process) {
        clearConsole();
        std::string command;
        bool showProcessInformation = false;
        renderProcessScreen(process, command, showProcessInformation);
        Process::State previousState = process->getState();
        std::size_t previousProgress = process->getCurrentInstruction();
        std::size_t previousLogCount = process->getLogs().size();

        while (true) {
            const auto refresh = [&](const std::string& currentCommand) {
                const Process::State state = process->getState();
                const std::size_t progress = process->getCurrentInstruction();
                const std::size_t logCount = process->getLogs().size();

                if (state != previousState || progress != previousProgress
                    || logCount != previousLogCount) {
                    renderProcessScreen(
                        process,
                        currentCommand,
                        showProcessInformation,
                        true
                    );
                    previousState = state;
                    previousProgress = progress;
                    previousLogCount = logCount;
                }
                };

            if (!readCommand(command, "process> ", refresh)) {
                return;
            }

            if (command == "exit") {
                return;
            }
            if (command == "process-smi") {
                showProcessInformation = true;
                renderProcessScreen(process, "", showProcessInformation);
                previousState = process->getState();
                previousProgress = process->getCurrentInstruction();
                previousLogCount = process->getLogs().size();
            }
            else {
                std::cout << "Unknown process screen command: " << command << '\n';
                std::this_thread::sleep_for(std::chrono::milliseconds(700));
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
                openProcessScreen(process);
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

                openProcessScreen(process);
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
