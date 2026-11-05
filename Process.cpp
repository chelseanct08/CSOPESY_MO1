#include "Process.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace {
    std::atomic<std::uint64_t> nextProcessId{ 0 };

    std::string getCurrentDateTime() {
        const std::time_t currentTime = std::time(nullptr);
        std::tm localTime{};
        localtime_s(&localTime, &currentTime);

        std::ostringstream formattedTime;
        formattedTime << std::put_time(&localTime, "%m/%d/%Y %I:%M:%S%p");
        return formattedTime.str();
    }

    std::size_t getMaximumForDepth(
        const std::vector<Instruction>& instructions
    ) {
        std::size_t maximumDepth = 0;
        for (const Instruction& instruction : instructions) {
            if (instruction.type == Instruction::FOR) {
                maximumDepth = std::max(
                    maximumDepth,
                    static_cast<std::size_t>(1)
                        + getMaximumForDepth(instruction.body)
                );
            }
        }
        return maximumDepth;
    }
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
    printVariable.clear();
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

Instruction Instruction::makePrintVariable(
    const std::string& prefix,
    const std::string& variable
) {
    Instruction instruction;
    instruction.type = PRINT;
    instruction.text = prefix;
    instruction.printVariable = variable;
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
    instruction.sleepTicks = static_cast<std::uint8_t>(
        std::min<std::uint64_t>(ticks, std::numeric_limits<std::uint8_t>::max())
    );
    return instruction;
}

Instruction Instruction::makeFor(
    const std::vector<Instruction>& instructions,
    std::uint64_t repeats
) {
    if (getMaximumForDepth(instructions) >= 3) {
        throw std::invalid_argument(
            "FOR instructions may be nested at most three levels."
        );
    }

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
createdAt(getCurrentDateTime()),
pid(nextProcessId.fetch_add(1)),
state(READY),
instructionIndex(0),
instructionCount(0),
remainingSleepTicks(0) {
    variables.emplace("x", 0);
    variables.emplace("y", 0);
    variables.emplace("z", 0);
    this->instructions = instructions;
    instructionCount = countExecutableInstructions(this->instructions);
    if (!this->instructions.empty()) {
        executionStack.push_back({ &this->instructions, 0, 1 });
    }

    if (instructionCount == 0) {
        state = FINISHED;
    }
}

const Instruction* Process::nextExecutableInstruction() {
    while (!executionStack.empty()) {
        ExecutionFrame& frame = executionStack.back();

        if (frame.nextIndex >= frame.body->size()) {
            if (frame.repeatsRemaining > 1) {
                --frame.repeatsRemaining;
                frame.nextIndex = 0;
            }
            else {
                executionStack.pop_back();
            }
            continue;
        }

        const Instruction& instruction = (*frame.body)[frame.nextIndex++];
        if (instruction.type != Instruction::FOR) {
            return &instruction;
        }

        if (instruction.repeatCount > 0 && !instruction.body.empty()) {
            executionStack.push_back({
                &instruction.body,
                0,
                instruction.repeatCount
                });
        }
    }

    return nullptr;
}

std::size_t Process::countExecutableInstructions(
    const std::vector<Instruction>& source
) {
    std::size_t count = 0;

    for (const Instruction& instruction : source) {
        std::size_t amount = 1;
        if (instruction.type == Instruction::FOR) {
            amount = 0;
            if (instruction.repeatCount > 0) {
                const std::size_t bodyCount =
                    countExecutableInstructions(instruction.body);
                if (bodyCount != 0
                    && instruction.repeatCount
                        > (static_cast<std::uint64_t>(
                            static_cast<std::size_t>(-1)
                            ) / bodyCount)) {
                    amount = static_cast<std::size_t>(-1);
                }
                else {
                    amount = bodyCount
                        * static_cast<std::size_t>(instruction.repeatCount);
                }
            }
        }

        if (amount > static_cast<std::size_t>(-1) - count) {
            return static_cast<std::size_t>(-1);
        }

        count += amount;
    }

    return count;
}

const std::string& Process::getName() const {
    return name;
}

const std::string& Process::getCreatedAt() const {
    return createdAt;
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
    return instructionCount;
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

std::unordered_map<std::string, std::uint16_t> Process::getVariables() const {
    std::lock_guard<std::mutex> lock(processMutex);
    return variables;
}

std::uint16_t Process::clampValue(std::int64_t value) {
    if (value < 0) {
        return 0;
    }

    if (value > std::numeric_limits<std::uint16_t>::max()) {
        return std::numeric_limits<std::uint16_t>::max();
    }

    return static_cast<std::uint16_t>(value);
}

std::int64_t Process::getOperandValue(const Operand& operand) {
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

    const Instruction* nextInstruction = nextExecutableInstruction();
    if (nextInstruction == nullptr) {
        state = FINISHED;
        return;
    }

    state = RUNNING;
    const Instruction& instruction = *nextInstruction;

    switch (instruction.type) {
    case Instruction::PRINT:
        if (instruction.printVariable.empty()) {
            logs.push_back(instruction.text);
        }
        else {
            logs.push_back(
                instruction.text
                + std::to_string(getOperandValue(
                    Operand(instruction.printVariable)
                ))
            );
        }
        break;

    case Instruction::DECLARE:
        variables[instruction.variable] = clampValue(instruction.value);
        break;

    case Instruction::ADD: {
        const std::int64_t first = getOperandValue(instruction.operand1);
        const std::int64_t second = getOperandValue(instruction.operand2);
        variables[instruction.variable] = clampValue(first + second);
        break;
    }

    case Instruction::SUBTRACT: {
        const std::int64_t first = getOperandValue(instruction.operand1);
        const std::int64_t second = getOperandValue(instruction.operand2);
        variables[instruction.variable] = clampValue(first - second);
        break;
    }

    case Instruction::SLEEP:
        remainingSleepTicks = instruction.sleepTicks;
        if (remainingSleepTicks > 0) {
            state = WAITING;
        }
        break;

    case Instruction::FOR:
        break;
    }

    ++instructionIndex;
    if (instructionIndex >= instructionCount && state != WAITING) {
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

    if (instructionIndex >= instructionCount) {
        state = FINISHED;
        return false;
    }

    state = READY;
    return true;
}
