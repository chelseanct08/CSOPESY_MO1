
#include "Process.h"

#include <algorithm>
#include <iostream>

namespace {
    std::deque<std::string> outputQueue;
    std::mutex outputQueueMutex;
}

void queueProcessOutput(const std::string& message) {
    std::lock_guard<std::mutex> lock(outputQueueMutex);
    outputQueue.push_back(message);
}

std::vector<std::string> takeQueuedProcessOutput() {
    std::deque<std::string> messages;

    {
        std::lock_guard<std::mutex> lock(outputQueueMutex);
        messages.swap(outputQueue);
    }

    return std::vector<std::string>(messages.begin(), messages.end());
}

void displayQueuedProcessOutput() {
    std::vector<std::string> messages = takeQueuedProcessOutput();

    for (const std::string& message : messages) {
        std::cout << message << '\n';
    }
}

Operand::Operand() {
    isVariable = false;
    variableName = "";
    value = 0;
}

Operand::Operand(int value) {
    isVariable = false;
    variableName = "";
    this->value = value;
}

Operand::Operand(const std::string& variableName) {
    isVariable = true;
    this->variableName = variableName;
    value = 0;
}

Instruction::Instruction() {
    type = PRINT;
    text = "";
    variable = "";
    value = 0;
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

Instruction Instruction::makeSleep(int ticks) {
    Instruction instruction;
    instruction.type = SLEEP;
    instruction.value = ticks;
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
) {
    this->name = name;
    state = READY;
    instructionIndex = 0;
    remainingSleepTicks = 0;

    expandInstructions(instructions, 0);

    if (this->instructions.empty()) {
        state = FINISHED;
    }
}

void Process::expandInstructions(
    const std::vector<Instruction>& source,
    int depth
) {
    for (const Instruction& instruction : source) {
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

const std::string& Process::getName() const {
    return name;
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

uint16_t Process::clampValue(int value) {
    if (value < 0) {
        return 0;
    }

    if (value > 65535) {
        return 65535;
    }

    return static_cast<uint16_t>(value);
}

int Process::getOperandValue(const Operand& operand) {
    if (!operand.isVariable) {
        return operand.value;
    }

    if (variables.find(operand.variableName) == variables.end()) {
        variables[operand.variableName] = 0;
    }

    return variables[operand.variableName];
}

void Process::executeNextInstruction() {
    std::lock_guard<std::mutex> lock(processMutex);

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
    case Instruction::PRINT:
        queueProcessOutput(instruction.text);
        instructionIndex++;
        break;

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

    if (instructionIndex >= static_cast<int>(instructions.size())
        && state != WAITING) {
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
