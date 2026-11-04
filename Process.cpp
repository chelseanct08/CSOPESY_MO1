#include "Process.h"

#include <algorithm>
#include <atomic>
#include <limits>

namespace {
    std::atomic<std::uint64_t> nextProcessId{ 0 };
}

Operand::Operand() {
    isVariable = false;
    variableName.clear();
    value = 0;
}

Operand::Operand(int value) {
    isVariable = false;
    variableName.clear();
    this->value = value;
}

Operand::Operand(const std::string& variableName) {
    isVariable = true;
    this->variableName = variableName;
    value = 0;
}

Instruction::Instruction() {
    type = PRINT;
    text.clear();
    variable.clear();
    value = 0;
    sleepTicks = 0;
    repeatCount = 0;
}

Instruction Instruction::makePrint(const std::string& message) {
    Instruction instruction;
    instruction.type = PRINT;
    instruction.text = message;
    return instruction;
}

Instruction Instruction::makeDeclare(const std::string& variable, int value) {
    Instruction instruction;
    instruction.type = DECLARE;
    instruction.variable = variable;
    instruction.value = value;
    return instruction;
}

Instruction Instruction::makeAdd(
    const std::string& destination,
    const Operand& first,
    const Operand& second
) {
    Instruction instruction;
    instruction.type = ADD;
    instruction.variable = destination;
    instruction.operand1 = first;
    instruction.operand2 = second;
    return instruction;
}

Instruction Instruction::makeSubtract(
    const std::string& destination,
    const Operand& first,
    const Operand& second
) {
    Instruction instruction;
    instruction.type = SUBTRACT;
    instruction.variable = destination;
    instruction.operand1 = first;
    instruction.operand2 = second;
    return instruction;
}

Instruction Instruction::makeSleep(std::uint64_t ticks) {
    Instruction instruction;
    instruction.type = SLEEP;
    instruction.sleepTicks = ticks;
    return instruction;
}

Instruction Instruction::makeFor(
    const std::vector<Instruction>& instructions,
    int repeats
) {
    Instruction instruction;
    instruction.type = FOR;
    instruction.body = instructions;
    instruction.repeatCount = repeats;
    return instruction;
}

Process::Process(
    const std::string& name,
    const std::vector<Instruction>& instructions
) : name(name),
pid(nextProcessId.fetch_add(1)),
state(READY),
instructionIndex(0),
remainingSleepTicks(0) {
    expandInstructions(instructions);

    if (this->instructions.empty()) {
        state = FINISHED;
    }
}

void Process::expandInstructions(
    const std::vector<Instruction>& source
) {
    for (const Instruction& instruction : source) {
        if (instruction.type != Instruction::FOR) {
            instructions.push_back(instruction);
            continue;
        }

        if (instruction.repeatCount <= 0) {
            continue;
        }

        for (int repeat = 0; repeat < instruction.repeatCount; ++repeat) {
            expandInstructions(instruction.body);
        }
    }
}

const std::string& Process::getName() const {
    return name;
}

std::uint64_t Process::getPid() const {
    return pid;
}

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

std::size_t Process::getInstructionCount() const {
    std::lock_guard<std::mutex> lock(processMutex);
    return instructions.size();
}

std::size_t Process::getCurrentInstruction() const {
    std::lock_guard<std::mutex> lock(processMutex);
    return instructionIndex;
}

std::uint64_t Process::getRemainingSleepTicks() const {
    std::lock_guard<std::mutex> lock(processMutex);
    return remainingSleepTicks;
}

std::vector<std::string> Process::getLogs() const {
    std::lock_guard<std::mutex> lock(processMutex);
    return logs;
}

std::uint16_t Process::clampValue(int value) {
    if (value < 0) {
        return 0;
    }

    if (value > std::numeric_limits<std::uint16_t>::max()) {
        return std::numeric_limits<std::uint16_t>::max();
    }

    return static_cast<std::uint16_t>(value);
}

int Process::getOperandValue(const Operand& operand) {
    if (!operand.isVariable) {
        return clampValue(operand.value);
    }

    const auto found = variables.find(operand.variableName);
    if (found == variables.end()) {
        return variables.emplace(operand.variableName, 0).first->second;
    }

    return found->second;
}

void Process::executeNextInstruction() {
    std::lock_guard<std::mutex> lock(processMutex);

    if (state == FINISHED || state == WAITING) {
        return;
    }

    if (instructionIndex >= instructions.size()) {
        state = FINISHED;
        return;
    }

    state = RUNNING;
    const Instruction& instruction = instructions[instructionIndex];

    switch (instruction.type) {
    case Instruction::PRINT:
        logs.push_back(instruction.text);
        ++instructionIndex;
        break;

    case Instruction::DECLARE:
        variables[instruction.variable] = clampValue(instruction.value);
        ++instructionIndex;
        break;

    case Instruction::ADD: {
        const int first = getOperandValue(instruction.operand1);
        const int second = getOperandValue(instruction.operand2);
        variables[instruction.variable] = clampValue(first + second);
        ++instructionIndex;
        break;
    }

    case Instruction::SUBTRACT: {
        const int first = getOperandValue(instruction.operand1);
        const int second = getOperandValue(instruction.operand2);
        variables[instruction.variable] = clampValue(first - second);
        ++instructionIndex;
        break;
    }

    case Instruction::SLEEP:
        ++instructionIndex;
        remainingSleepTicks = instruction.sleepTicks;
        if (remainingSleepTicks > 0) {
            state = WAITING;
        }
        break;

    case Instruction::FOR:
        ++instructionIndex;
        break;
    }

    if (instructionIndex >= instructions.size() && state != WAITING) {
        state = FINISHED;
    }
}

bool Process::tickSleep() {
    std::lock_guard<std::mutex> lock(processMutex);

    if (state != WAITING || remainingSleepTicks == 0) {
        return false;
    }

    --remainingSleepTicks;
    if (remainingSleepTicks != 0) {
        return false;
    }

    if (instructionIndex >= instructions.size()) {
        state = FINISHED;
        return false;
    }

    state = READY;
    return true;
}
