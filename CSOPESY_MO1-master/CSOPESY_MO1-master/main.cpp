#include "Config.h"
#include "Process.h"
#include "Scheduler.h"

#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

BOOL WINAPI ConsoleHandler(DWORD signal) {
    if (signal == CTRL_CLOSE_EVENT || signal == CTRL_C_EVENT) {
        ExitProcess(0);
    }
    return TRUE;
}

std::vector<Instruction> generateInstructions(
    const std::string& name,
    const Config& config
) {
    thread_local std::mt19937 generator(std::random_device{}());

    std::uniform_int_distribution<int> countDistribution(config.minIns, config.maxIns);
    std::uniform_int_distribution<int> operationDistribution(0, 3);
    std::uniform_int_distribution<int> variableDistribution(1, 5);
    std::uniform_int_distribution<int> valueDistribution(0, 100);
    std::uniform_int_distribution<int> sleepDistribution(1, 3);

    int instructionCount = countDistribution(generator);
    std::vector<Instruction> instructions;
    instructions.reserve(instructionCount);

    // Initial instruction must match requirement
    instructions.push_back(Instruction::makePrint("Hello world from " + name + "!"));

    for (int i = 1; i < instructionCount; i++) {
        int operation = operationDistribution(generator);
        std::string destination = "v" + std::to_string(variableDistribution(generator));
        std::string source = "v" + std::to_string(variableDistribution(generator));
        int value = valueDistribution(generator);

        if (operation == 0) {
            instructions.push_back(Instruction::makeDeclare(destination, value));
        }
        else if (operation == 1) {
            instructions.push_back(Instruction::makeAdd(destination, Operand(source), Operand(value)));
        }
        else if (operation == 2) {
            instructions.push_back(Instruction::makeSubtract(destination, Operand(source), Operand(value)));
        }
        else {
            instructions.push_back(Instruction::makeSleep(sleepDistribution(generator)));
        }
    }

    return instructions;
}

void enterProcessScreen(const std::shared_ptr<Process>& process) {
    system("cls");

    // Print initial state as shown in mock-up
    std::cout << "Process name: " << process->getName() << "\n";
    std::cout << "ID: " << process->getId() << "\n\n";
    std::cout << "Logs:\n";

    for (const auto& log : process->getLogs()) {
        std::cout << log << "\n";
    }

    if (process->isFinished()) {
        std::cout << "Finished!\n";
    }
    else {
        std::cout << "\nCurrent instruction line: " << process->getCurrentInstruction() << "\n";
        std::cout << "Lines of code: " << process->getInstructionCount() << "\n";
    }

    std::string subCommand;
    while (true) {
        std::cout << "\nroot:\\> ";
        if (!std::getline(std::cin, subCommand)) {
            break;
        }

        std::istringstream iss(subCommand);
        std::string action;
        iss >> action;

        if (action.empty()) {
            continue;
        }

        if (action == "exit") {
            system("cls");
            break;
        }
        else if (action == "process-smi") {
            std::cout << "Process name: " << process->getName() << "\n";
            std::cout << "ID: " << process->getId() << "\n\n";
            std::cout << "Logs:\n";

            for (const auto& log : process->getLogs()) {
                std::cout << log << "\n";
            }

            if (process->isFinished()) {
                std::cout << "Finished!\n";
            }
            else {
                std::cout << "\nCurrent instruction line: " << process->getCurrentInstruction() << "\n";
                std::cout << "Lines of code: " << process->getInstructionCount() << "\n";
            }
        }
        else {
            std::cout << "Unknown command inside screen. Available: process-smi, exit\n";
        }
    }
}

std::string generateUtilizationReportString(
    Scheduler* scheduler,
    const std::unordered_map<std::string, std::shared_ptr<Process>>& screens
) {
    std::ostringstream ss;

    int totalCores = scheduler ? scheduler->getTotalCores() : 0;
    int usedCores = scheduler ? scheduler->getActiveCoreCount() : 0;
    if (usedCores > totalCores) usedCores = totalCores;
    int availableCores = totalCores - usedCores;

    double utilPercent = totalCores > 0 ? (static_cast<double>(usedCores) / totalCores) * 100.0 : 0.0;

    ss << "CPU utilization: " << static_cast<int>(utilPercent) << "%\n";
    ss << "Cores used: " << usedCores << "\n";
    ss << "Cores available: " << availableCores << "\n\n";
    ss << "--------------------------------------------------\n";
    ss << "Running processes:\n";

    std::vector<std::shared_ptr<Process>> runningList;
    std::vector<std::shared_ptr<Process>> finishedList;

    for (const auto& pair : screens) {
        if (pair.second->isFinished()) {
            finishedList.push_back(pair.second);
        }
        else {
            runningList.push_back(pair.second);
        }
    }

    auto comp = [](const std::shared_ptr<Process>& a, const std::shared_ptr<Process>& b) {
        return a->getId() < b->getId();
        };
    std::sort(runningList.begin(), runningList.end(), comp);
    std::sort(finishedList.begin(), finishedList.end(), comp);

    for (const auto& p : runningList) {
        ss << std::left << std::setw(14) << p->getName()
            << " " << p->getStartTime()
            << "   Core: " << p->getLastCore()
            << "   " << p->getCurrentInstruction() << " / " << p->getInstructionCount() << "\n";
    }

    ss << "\nFinished processes:\n";
    for (const auto& p : finishedList) {
        ss << std::left << std::setw(14) << p->getName()
            << " " << p->getStartTime()
            << "   Finished"
            << "   " << p->getCurrentInstruction() << " / " << p->getInstructionCount() << "\n";
    }

    return ss.str();
}

int main() {
    SetConsoleCtrlHandler(ConsoleHandler, TRUE);

    Config config;
    std::unique_ptr<Scheduler> scheduler;

    std::unordered_map<std::string, std::shared_ptr<Process>> screens;
    std::mutex screensMutex;

    bool generating = false;
    std::mutex generatorMutex;
    std::condition_variable generatorCondition;
    std::thread generatorThread;

    int nextProcessId = 1;
    bool initialized = false;
    bool running = true;

    std::cout << "  _____  _____  ____  _____  ______  _______     __\n";
    std::cout << " / ____|/ ____|/ __ \\|  __ \\|  ____|/ ____\\ \\   / /\n";
    std::cout << "| |    | (___ | |  | | |__) | |__  | (___  \\ \\_/ / \n";
    std::cout << "| |     \\___ \\| |  | |  ___/|  __|  \\___ \\  \\   /  \n";
    std::cout << "| |____ ____) | |__| | |    | |____ ____) |  | |   \n";
    std::cout << " \\_____|_____/ \\____/|_|    |______|_____/   |_|   \n\n";
    std::cout << "Welcome to CSOPESY Emulator!\n";
    std::cout << "Developers: Group 11\n";
    std::cout << "Last updated: 2026\n\n";
    std::cout << "Type initialize to begin.\n";

    std::string line;
    while (running) {
        std::cout << "root:\\> ";
        if (!std::getline(std::cin, line)) {
            break;
        }

        std::istringstream input(line);
        std::string action;
        input >> action;

        if (action.empty()) {
            continue;
        }

        if (action == "exit") {
            running = false;
        }
        else if (action == "initialize") {
            if (initialized) {
                std::cout << "System is already initialized.\n";
                continue;
            }

            // Fallback check: look directly in current directory, or one level down in Debug
            std::string configFile = "config.txt";
            if (!std::filesystem::exists(configFile) && std::filesystem::exists("Debug/config.txt")) {
                configFile = "Debug/config.txt";
            }

            if (!config.loadConfig(configFile)) {
                std::cout << "Could not initialize. Check config.txt and program's working directory.\n";
                continue;
            }

            scheduler = std::make_unique<Scheduler>(config);
            scheduler->start();
            initialized = true;
            std::cout << "System initialized successfully.\n";
        }
        else if (!initialized) {
            std::cout << "Please run initialize before using this command.\n";
        }
        else if (action == "screen") {
            std::string option;
            std::string name;
            input >> option >> name;

            if (option == "-s") {
                if (name.empty()) {
                    std::cout << "Usage: screen -s <name>\n";
                    continue;
                }

                std::shared_ptr<Process> process;
                {
                    std::lock_guard<std::mutex> lock(screensMutex);
                    if (screens.find(name) != screens.end()) {
                        std::cout << "A process with that name already exists.\n";
                        continue;
                    }

                    std::vector<Instruction> instructions = generateInstructions(name, config);
                    process = std::make_shared<Process>(nextProcessId++, name, instructions);
                    screens[name] = process;
                }

                scheduler->addProcess(process);
                enterProcessScreen(process);

            }
            else if (option == "-r") {
                if (name.empty()) {
                    std::cout << "Usage: screen -r <name>\n";
                    continue;
                }

                std::shared_ptr<Process> process = nullptr;
                {
                    std::lock_guard<std::mutex> lock(screensMutex);
                    auto it = screens.find(name);
                    if (it != screens.end()) {
                        process = it->second;
                    }
                }

                // Per spec: finished processes cannot have their screen accessed
                if (!process || process->isFinished()) {
                    std::cout << "Process " << name << " not found.\n";
                }
                else {
                    enterProcessScreen(process);
                }

            }
            else if (option == "-ls") {
                std::lock_guard<std::mutex> lock(screensMutex);
                std::cout << generateUtilizationReportString(scheduler.get(), screens);
            }
            else {
                std::cout << "Usage: screen -s <name>, screen -r <name>, or screen -ls\n";
            }
        }
        else if (action == "scheduler-start") {
            {
                std::lock_guard<std::mutex> lock(generatorMutex);
                if (generating) {
                    std::cout << "Process generation is already running.\n";
                    continue;
                }
                generating = true;
            }

            if (generatorThread.joinable()) {
                generatorThread.join();
            }

            generatorThread = std::thread([&]() {
                while (true) {
                    std::unique_lock<std::mutex> lock(generatorMutex);
                    bool stopped = generatorCondition.wait_for(
                        lock,
                        std::chrono::seconds(config.batchProcessFreq < 1 ? 1 : config.batchProcessFreq),
                        [&]() { return !generating; }
                    );

                    if (stopped || !generating) {
                        break;
                    }

                    int curId = nextProcessId++;
                    std::ostringstream nameStream;
                    nameStream << "p" << std::setfill('0') << std::setw(2) << curId;
                    std::string processName = nameStream.str();

                    lock.unlock();

                    std::vector<Instruction> instructions = generateInstructions(processName, config);
                    auto proc = std::make_shared<Process>(curId, processName, instructions);

                    {
                        std::lock_guard<std::mutex> sLock(screensMutex);
                        screens[processName] = proc;
                    }

                    scheduler->addProcess(proc);
                }
                });

            std::cout << "Automatic process generation started.\n";
        }
        else if (action == "scheduler-stop") {
            {
                std::lock_guard<std::mutex> lock(generatorMutex);
                generating = false;
            }
            generatorCondition.notify_all();

            if (generatorThread.joinable()) {
                generatorThread.join();
            }

            std::cout << "Automatic process generation stopped.\n";
        }
        else if (action == "report-util") {
            std::lock_guard<std::mutex> lock(screensMutex);
            std::string reportData = generateUtilizationReportString(scheduler.get(), screens);

            std::ofstream report("csopesy-log.txt");
            if (!report.is_open()) {
                std::cout << "Could not create csopesy-log.txt.\n";
                continue;
            }

            report << reportData;
            report.close();
            std::cout << "Report generated at csopesy-log.txt!\n";
        }
        else {
            std::cout << "Unknown command: " << action << "\n";
        }
    }

    {
        std::lock_guard<std::mutex> lock(generatorMutex);
        generating = false;
    }
    generatorCondition.notify_all();

    if (generatorThread.joinable()) {
        generatorThread.join();
    }

    if (scheduler) {
        scheduler->stop();
    }

    std::cout << "Exiting CSOPESY CLI.\n";
    return 0;
}