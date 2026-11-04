#include "Config.h"
#include "Process.h"
#include "Scheduler.h"

#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <conio.h>
#include <condition_variable>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

std::vector<Instruction> generateInstructions(
    const std::string& name,
    const Config& config
) {
    thread_local std::mt19937 generator(std::random_device{}());

    std::uniform_int_distribution<int> countDistribution(
        config.minIns, config.maxIns
    );
    std::uniform_int_distribution<int> operationDistribution(0, 3);
    std::uniform_int_distribution<int> variableDistribution(1, 5);
    std::uniform_int_distribution<int> valueDistribution(0, 100);
    std::uniform_int_distribution<int> sleepDistribution(1, 3);

    int instructionCount = countDistribution(generator);
    std::vector<Instruction> instructions;
    instructions.reserve(instructionCount);

    instructions.push_back(
        Instruction::makePrint("Hello world from " + name + "!")
    );

    for (int i = 1; i < instructionCount; i++) {
        int operation = operationDistribution(generator);

        std::string destination =
            "v" + std::to_string(variableDistribution(generator));

        std::string source =
            "v" + std::to_string(variableDistribution(generator));

        int value = valueDistribution(generator);

        if (operation == 0) {
            instructions.push_back(
                Instruction::makeDeclare(destination, value)
            );
        }
        else if (operation == 1) {
            instructions.push_back(
                Instruction::makeAdd(
                    destination,
                    Operand(source),
                    Operand(value)
                )
            );
        }
        else if (operation == 2) {
            instructions.push_back(
                Instruction::makeSubtract(
                    destination,
                    Operand(source),
                    Operand(value)
                )
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

void sortProcesses(
    std::vector<std::shared_ptr<Process>>& processList
) {
    std::sort(
        processList.begin(),
        processList.end(),
        [](const std::shared_ptr<Process>& a,
            const std::shared_ptr<Process>& b) {
                return a->getName() < b->getName();
        }
    );
}

void clearCurrentConsoleLine() {
    HANDLE console = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO info;

    if (!GetConsoleScreenBufferInfo(console, &info)) {
        return;
    }

    COORD lineStart = { 0, info.dwCursorPosition.Y };
    DWORD charactersWritten = 0;

    FillConsoleOutputCharacterA(
        console,
        ' ',
        info.dwSize.X,
        lineStart,
        &charactersWritten
    );

    SetConsoleCursorPosition(console, lineStart);
}

bool readCommand(std::string& command) {
    command.clear();
    std::cout << "\n> " << std::flush;

    while (true) {
        std::vector<std::string> messages = takeQueuedProcessOutput();

        if (!messages.empty()) {
            clearCurrentConsoleLine();

            for (const std::string& message : messages) {
                std::cout << message << '\n';
            }

            std::cout << "> " << command << std::flush;
        }

        if (_kbhit()) {
            int character = _getch();

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

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

int main() {
    Config config;
    std::unique_ptr<Scheduler> scheduler;

    std::unordered_map<std::string, std::shared_ptr<Process>> screens;
    std::mutex screensMutex;

    bool generating = false;
    std::mutex generatorMutex;
    std::condition_variable generatorCondition;
    std::thread generatorThread;

    int nextProcessNumber = 1;
    bool initialized = false;
    bool running = true;

    std::string command;

    std::cout << "CSOPESY Process Scheduler CLI\n";
    std::cout << "Type initialize to begin.\n";

    while (running) {
        if (!readCommand(command)) {
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
        }
        else if (action == "initialize") {
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
        }
        else if (!initialized) {
            std::cout
                << "Please run initialize before using this command.\n";
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
                        std::cout
                            << "A process with that name already exists.\n";
                        continue;
                    }

                    std::vector<Instruction> instructions =
                        generateInstructions(name, config);

                    process = std::make_shared<Process>(
                        name,
                        instructions
                    );

                    screens[name] = process;
                }

                scheduler->addProcess(process);
                std::cout << "Created process screen: " << name << '\n';
            }
            else if (option == "-r") {
                if (name.empty()) {
                    std::cout << "Usage: screen -r <name>\n";
                    continue;
                }

                std::shared_ptr<Process> process;

                {
                    std::lock_guard<std::mutex> lock(screensMutex);
                    auto found = screens.find(name);

                    if (found != screens.end()) {
                        process = found->second;
                    }
                }

                if (!process) {
                    std::cout << "Process screen not found.\n";
                }
                else if (process->isFinished()) {
                    std::cout << "This process has already finished.\n";
                }
                else {
                    std::cout << "Process: "
                        << process->getName() << '\n';

                    std::cout << "Instructions: "
                        << process->getCurrentInstruction()
                        << " / "
                        << process->getInstructionCount()
                        << '\n';

                    std::cout << "Status: "
                        << getProcessStatus(process->getState())
                        << '\n';
                }
            }
            else if (option == "-ls") {
                std::vector<std::shared_ptr<Process>> processList;

                {
                    std::lock_guard<std::mutex> lock(screensMutex);

                    for (const auto& entry : screens) {
                        processList.push_back(entry.second);
                    }
                }

                sortProcesses(processList);

                std::cout
                    << "Process Name\tStatus\tInstructions\n";

                int runningCount = 0;
                int finishedCount = 0;

                for (const auto& process : processList) {
                    Process::State state = process->getState();

                    if (state == Process::FINISHED) {
                        finishedCount++;
                    }
                    else {
                        runningCount++;
                    }

                    std::cout
                        << process->getName() << '\t'
                        << getProcessStatus(state) << '\t'
                        << process->getCurrentInstruction()
                        << " / "
                        << process->getInstructionCount()
                        << '\n';
                }

                std::cout << "\nRunning processes: " << runningCount << '\n';
                std::cout << "Finished processes: " << finishedCount << '\n';

            }
            else {
                std::cout
                    << "Usage: screen -s <name>, screen -r <name>, "
                    << "or screen -ls\n";
            }
        }
        else if (action == "scheduler-start") {
            {
                std::lock_guard<std::mutex> lock(generatorMutex);

                if (generating) {
                    std::cout
                        << "Process generation is already running.\n";
                    continue;
                }
            }

            if (generatorThread.joinable()) {
                generatorThread.join();
            }

            {
                std::lock_guard<std::mutex> lock(generatorMutex);
                generating = true;
            }

            generatorThread = std::thread([&]() {
                while (true) {
                    std::unique_lock<std::mutex> lock(generatorMutex);

                    bool stopped = generatorCondition.wait_for(
                        lock,
                        std::chrono::seconds(
                            config.batchProcessFreq < 1
                            ? 1
                            : config.batchProcessFreq
                        ),
                        [&]() {
                            return !generating;
                        }
                    );

                    if (stopped || !generating) {
                        break;
                    }

                    int processNumber = nextProcessNumber++;

                    std::string name = "p";

                    if (processNumber < 10) {
                        name += "0";
                    }

                    name += std::to_string(processNumber);

                    lock.unlock();

                    std::vector<Instruction> instructions =
                        generateInstructions(name, config);

                    auto process = std::make_shared<Process>(
                        name,
                        instructions
                    );

                    lock.lock();

                    if (!generating) {
                        break;
                    }

                    {
                        std::lock_guard<std::mutex> screensLock(screensMutex);

                        while (screens.find(name) != screens.end()) {
                            processNumber = nextProcessNumber++;

                            name = "p";

                            if (processNumber < 10) {
                                name += "0";
                            }

                            name += std::to_string(processNumber);

                            instructions = generateInstructions(name, config);

                            process = std::make_shared<Process>(
                                name,
                                instructions
                            );
                        }

                        screens[name] = process;
                    }

                    scheduler->addProcess(process);
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
            std::vector<std::shared_ptr<Process>> processList;

            {
                std::lock_guard<std::mutex> lock(screensMutex);

                for (const auto& entry : screens) {
                    processList.push_back(entry.second);
                }
            }

            sortProcesses(processList);

            std::ofstream report("csopesy-log.txt");

            if (!report.is_open()) {
                std::cout << "Could not create csopesy-log.txt.\n";
                continue;
            }

            report << "Process Name\tStatus\tInstructions\n";

            int runningCount = 0;
            int finishedCount = 0;

            for (const auto& process : processList) {
                Process::State state = process->getState();

                if (state == Process::FINISHED) {
                    finishedCount++;
                }
                else {
                    runningCount++;
                }

                report
                    << process->getName() << '\t'
                    << getProcessStatus(state) << '\t'
                    << process->getCurrentInstruction()
                    << " / "
                    << process->getInstructionCount()
                    << '\n';
            }

            report << "\nRunning processes: " << runningCount << '\n';
            report << "Finished processes: " << finishedCount << '\n';

            report.close();
            std::cout << "Saved report to csopesy-log.txt.\n";
        }
        else {
            std::cout << "Unknown command: " << action << '\n';
        }

        displayQueuedProcessOutput();
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

    displayQueuedProcessOutput();

    std::cout << "Exiting CSOPESY CLI.\n";

    return 0;
}
