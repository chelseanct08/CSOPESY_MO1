#include "Process.h"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

Operand::Operand() : isVariable(false), variableName(""), value(0) {}
Operand::Operand(int value) : isVariable(false), variableName(""), value(value) {}
Operand::Operand(const std::string& variableName) : isVariable(true), variableName(variableName), value(0) {}

Instruction::Instruction() : type(PRINT), text(""), variable(""), value(0), repeatCount(0) {}

Instruction Instruction::makePrint(const std::string& message) {
    Instruction inst;
    inst.type = PRINT;
    inst.text = message;
    return inst;
}

Instruction Instruction::makeDeclare(const std::string& variable, int value) {
    Instruction inst;
    inst.type = DECLARE;
    inst.variable = variable;
    inst.value = value;
    return inst;
}

Instruction Instruction::makeAdd(const std::string& destination, const Operand& first, const Operand& second) {
    Instruction inst;
    inst.type = ADD;
    inst.variable = destination;
    inst.operand1 = first;
    inst.operand2 = second;
    return inst;
}

Instruction Instruction::makeSubtract(const std::string& destination, const Operand& first, const Operand& second) {
    Instruction inst;
    inst.type = SUBTRACT;
    inst.variable = destination;
    inst.operand1 = first;
    inst.operand2 = second;
    return inst;
}

Instruction Instruction::makeSleep(int ticks) {
    Instruction inst;
    inst.type = SLEEP;
    inst.value = ticks;
    return inst;
}

Instruction Instruction::makeFor(const std::vector<Instruction>& instructions, int repeats) {
    Instruction inst;
    inst.type = FOR;
    inst.body = instructions;
    inst.repeatCount = repeats;
    return inst;
}

std::string Process::getCurrentTimestamp() {
    auto now = std::chrono::system_clock::now();
    std::time_t timeT = std::chrono::system_clock::to_time_t(now);
    std::tm tmNow;
#if defined(_WIN32) || defined(_WIN64)
    localtime_s(&tmNow, &timeT);
#else
    localtime_r(&timeT, &tmNow);
#endif
    std::ostringstream ss;
    ss << std::put_time(&tmNow, "(%m/%d/%Y %I:%M:%S%p)");
    return ss.str();
}

Process::Process(int id, const std::string& name, const std::vector<Instruction>& instructions)
    : id(id), name(name), state(READY), instructionIndex(0), remainingSleepTicks(0), lastCore(0) {
    startTime = getCurrentTimestamp();
    expandInstructions(instructions, 0);
    if (this->instructions.empty()) {
        state = FINISHED;
    }
}

void Process::expandInstructions(const std::vector<Instruction>& source, int depth) {
    for (const auto& instruction : source) {
        if (instruction.type == Instruction::FOR) {
            if (depth >= 3) {
                continue;
            }
            int repeats = std::max(0, instruction.repeatCount);
            for (int i = 0; i < repeats; i++) {
                expandInstructions(instruction.body, depth + 1);
            }
        }
        else {
            instructions.push_back(instruction);
        }
    }
}

int Process::getId() const { return id; }
const std::string& Process::getName() const { return name; }

Process::State Process::getState() const {
    std::lock_guard<std::mutex> lock(processMutex);
    return state;
}

void Process::setState(State newState) {
    std::lock_guard<std::mutex> lock(processMutex);
    state = newState;
}

bool Process::isFinished() const {
    std::lock_guard<std::mutex> lock(processMutex);
    return state == FINISHED;
}

int Process::getInstructionCount() const {
    std::lock_guard<std::mutex> lock(processMutex);
    return static_cast<int>(instructions.size());
}

int Process::getCurrentInstruction() const {
    std::lock_guard<std::mutex> lock(processMutex);
    return instructionIndex;
}

int Process::getRemainingSleepTicks() const {
    std::lock_guard<std::mutex> lock(processMutex);
    return remainingSleepTicks;
}

int Process::getLastCore() const {
    std::lock_guard<std::mutex> lock(processMutex);
    return lastCore;
}

std::string Process::getStartTime() const {
    std::lock_guard<std::mutex> lock(processMutex);
    return startTime;
}

std::vector<std::string> Process::getLogs() const {
    std::lock_guard<std::mutex> lock(processMutex);
    return logs;
}

uint16_t Process::clampValue(int value) {
    if (value < 0) return 0;
    if (value > 65535) return 65535;
    return static_cast<uint16_t>(value);
}

int Process::getOperandValue(const Operand& operand) {
    if (!operand.isVariable) return operand.value;
    if (variables.find(operand.variableName) == variables.end()) {
        variables[operand.variableName] = 0;
    }
    return variables[operand.variableName];
}

void Process::executeNextInstruction(int coreId) {
    std::lock_guard<std::mutex> lock(processMutex);
    lastCore = coreId;

    if (state == FINISHED || state == WAITING) {
        return;
    }

    if (instructionIndex >= static_cast<int>(instructions.size())) {
        state = FINISHED;
        return;
    }

    state = RUNNING;
    const Instruction& instruction = instructions[instructionIndex];

    switch (instruction.type) {
    case Instruction::PRINT: {
        std::string logLine = getCurrentTimestamp() + "   Core:" + std::to_string(coreId) + "   \"" + instruction.text + "\"";
        logs.push_back(logLine);
        instructionIndex++;
        break;
    }
    case Instruction::DECLARE:
        variables[instruction.variable] = clampValue(instruction.value);
        instructionIndex++;
        break;

    case Instruction::ADD: {
        int first = getOperandValue(instruction.operand1);
        int second = getOperandValue(instruction.operand2);
        variables[instruction.variable] = clampValue(first + second);
        instructionIndex++;
        break;
    }
    case Instruction::SUBTRACT: {
        int first = getOperandValue(instruction.operand1);
        int second = getOperandValue(instruction.operand2);
        variables[instruction.variable] = clampValue(first - second);
        instructionIndex++;
        break;
    }
    case Instruction::SLEEP:
        instructionIndex++;
        remainingSleepTicks = std::max(0, instruction.value);
        if (remainingSleepTicks > 0) {
            state = WAITING;
        }
        break;

    case Instruction::FOR:
        instructionIndex++;
        break;
    }

    if (instructionIndex >= static_cast<int>(instructions.size()) && state != WAITING) {
        state = FINISHED;
    }
}

void Process::tickSleep() {
    std::lock_guard<std::mutex> lock(processMutex);
    if (state != WAITING || remainingSleepTicks <= 0) {
        return;
    }

    remainingSleepTicks--;
    if (remainingSleepTicks == 0) {
        if (instructionIndex >= static_cast<int>(instructions.size())) {
            state = FINISHED;
        }
        else {
            state = READY;
        }
    }
}