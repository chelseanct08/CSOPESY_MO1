
#ifndef PROCESS_H
#define PROCESS_H

#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

void queueProcessOutput(const std::string& message);
std::vector<std::string> takeQueuedProcessOutput();
void displayQueuedProcessOutput();

struct Operand {
    bool isVariable;
    std::string variableName;
    int value;

    Operand();
    Operand(int value);
    Operand(const std::string& variableName);
};

struct Instruction {
    enum Type {
        PRINT,
        DECLARE,
        ADD,
        SUBTRACT,
        SLEEP,
        FOR
    };

    Type type;
    std::string text;
    std::string variable;
    Operand operand1;
    Operand operand2;
    int value;
    int repeatCount;
    std::vector<Instruction> body;

    Instruction();

    static Instruction makePrint(const std::string& message);
    static Instruction makeDeclare(const std::string& variable, int value);
    static Instruction makeAdd(const std::string& destination, const Operand& first, const Operand& second);
    static Instruction makeSubtract(const std::string& destination, const Operand& first, const Operand& second);
    static Instruction makeSleep(int ticks);
    static Instruction makeFor(const std::vector<Instruction>& instructions, int repeats);
};

class Process {
public:
    enum State {
        READY,
        RUNNING,
        WAITING,
        FINISHED
    };

    Process(const std::string& name, const std::vector<Instruction>& instructions);

    const std::string& getName() const;
    State getState() const;
    void setState(State newState);
    bool isFinished() const;
    int getInstructionCount() const;
    int getCurrentInstruction() const;
    int getRemainingSleepTicks() const;

    void executeNextInstruction();
    void tickSleep();

private:
    std::string name;
    State state;
    std::vector<Instruction> instructions;
    std::unordered_map<std::string, uint16_t> variables;
    int instructionIndex;
    int remainingSleepTicks;
    mutable std::mutex processMutex;

    void expandInstructions(const std::vector<Instruction>& source, int depth);
    int getOperandValue(const Operand& operand);
    uint16_t clampValue(int value);
};

#endif
