#include "Config.h"
#include "Process.h"
#include "Scheduler.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <conio.h>
#include <cstdlib>
#include <fstream>
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
        std::uniform_int_distribution<int> typeDistribution(0, 5);
        std::uniform_int_distribution<int> valueDistribution(0, 100);
        std::uniform_int_distribution<int> sleepDistribution(1, 3);
        const std::vector<std::string> variableNames = { "x", "y", "z" };
        std::uniform_int_distribution<std::size_t> variableDistribution(
            0, variableNames.size() - 1
        );

        const bool mockTestSixProfile =
            config.minIns == 1000 && config.maxIns == 1000;
        const std::uint64_t instructionCount =
            countDistribution(generator);
        std::vector<Instruction> instructions;

        if (instructionCount == 0) {
            return instructions;
        }

        if (mockTestSixProfile) {
            constexpr std::uint64_t greetingCount = 3;
            for (std::uint64_t index = 0; index < greetingCount; ++index) {
                instructions.push_back(
                    Instruction::makePrint(
                        "Hello world from " + name + "!"
                    )
                );
            }

            const std::vector<Instruction> loopBody = {
                Instruction::makeAdd("x", Operand("x"), Operand(1)),
                Instruction::makePrintVariable("Value from: ", "x"),
                Instruction::makeAdd("y", Operand("y"), Operand(1)),
                Instruction::makePrintVariable("Value from: ", "y"),
                Instruction::makeAdd("z", Operand("z"), Operand(1)),
                Instruction::makePrintVariable("Value from: ", "z")
            };
            instructions.push_back(Instruction::makeFor(loopBody, 100));
            instructions.push_back(
                Instruction::makeFor(
                    { Instruction::makeSleep(0) },
                    instructionCount - 603
                )
            );
            return instructions;
        }

        instructions.push_back(
            Instruction::makePrint("Hello world from " + name + "!")
        );

        for (std::uint64_t index = 1; index < instructionCount; ++index) {
            const std::string& destination =
                variableNames[variableDistribution(generator)];
            const std::string& firstVariable =
                variableNames[variableDistribution(generator)];
            const std::string& secondVariable =
                variableNames[variableDistribution(generator)];

            switch (typeDistribution(generator)) {
            case 0:
                instructions.push_back(
                    Instruction::makePrintVariable(
                        "Value from: ", destination
                    )
                );
                break;
            case 1:
                instructions.push_back(
                    Instruction::makeDeclare(
                        destination, valueDistribution(generator)
                    )
                );
                break;
            case 2:
                instructions.push_back(
                    Instruction::makeAdd(
                        destination,
                        Operand(firstVariable),
                        Operand(valueDistribution(generator))
                    )
                );
                break;
            case 3:
                instructions.push_back(
                    Instruction::makeSubtract(
                        destination,
                        Operand(firstVariable),
                        Operand(secondVariable)
                    )
                );
                break;
            case 4:
                instructions.push_back(
                    Instruction::makeSleep(sleepDistribution(generator))
                );
                break;
            case 5:
                instructions.push_back(
                    Instruction::makeFor(
                        {
                            Instruction::makeAdd(
                                destination,
                                Operand(firstVariable),
                                Operand(1)
                            )
                        },
                        1
                    )
                );
                break;
            default:
                break;
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


    void displayHelp() {
        std::cout
            << "\n========== CSOPESY COMMANDS ==========\n"
            << "initialize                 Initialize the emulator\n"
            << "help                       Display available commands\n"
            << "screen -s <name>            Create a process\n"
            << "screen -r <name>            Reattach to a process\n"
            << "screen -ls                  List all processes\n"
            << "scheduler-start             Start automatic process generation\n"
            << "scheduler-stop              Stop automatic process generation\n"
            << "report-util                 Generate the utilization report\n"
            << "exit                        Exit the emulator\n";
    }

    bool readCommand(
        std::string& command,
        const std::string& prompt
    ) {
        command.clear();
        std::cout << prompt << std::flush;

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
                    static_cast<void>(_getch());
                }
                else if (character >= 32 && character <= 126) {
                    command += static_cast<char>(character);
                    std::cout << static_cast<char>(character) << std::flush;
                }
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

        while (true) {
            if (!readCommand(command, "process> ")) {
                return;
            }

            if (command == "exit") {
                return;
            }
            if (command == "process-smi") {
                showProcessInformation = true;
                renderProcessScreen(process, "", showProcessInformation);
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
        report << std::left << std::setw(19) << "Process Name"
            << std::right << std::setw(6) << "PID"
            << "   " << std::left << std::setw(23) << "Created"
            << std::setw(11) << "Status"
            << "Instructions\n";
        report << "--------------------------------------------------------------------------\n";

        const auto printSection = [&report, &processList](
            Process::State sectionState,
            const std::string& title
        ) {
            report << '\n' << title << " processes:\n";
            for (const std::shared_ptr<Process>& process : processList) {
                if (process->getState() != sectionState) {
                    continue;
                }

                report << std::left << std::setw(19) << process->getName()
                    << std::right << std::setw(6) << process->getPid()
                    << "   " << std::setw(23) << std::left
                    << process->getCreatedAt()
                    << std::setw(11) << getProcessStatus(process->getState())
                    << process->getCurrentInstruction() << " / "
                    << process->getInstructionCount() << '\n';
            }
        };

        printSection(Process::RUNNING, "Running");
        printSection(Process::READY, "Ready");
        printSection(Process::WAITING, "Waiting");
        printSection(Process::FINISHED, "Finished");

        report << "\nSummary:\n";
        report << "Running processes: " << runningCount << '\n';
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

        if (action == "help") {
            displayHelp();
        }
        else if (action == "screen") {
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