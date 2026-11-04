#ifndef PROCESS_H
#define PROCESS_H

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

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
    std::uint64_t sleepTicks;
    int repeatCount;
    std::vector<Instruction> body;

    Instruction();

    static Instruction makePrint(const std::string& message);
    static Instruction makeDeclare(const std::string& variable, int value);
    static Instruction makeAdd(const std::string& destination, const Operand& first, const Operand& second);
    static Instruction makeSubtract(const std::string& destination, const Operand& first, const Operand& second);
    static Instruction makeSleep(std::uint64_t ticks);
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
    std::uint64_t getPid() const;
    State getState() const;
    void setState(State newState);
    bool isFinished() const;
    std::size_t getInstructionCount() const;
    std::size_t getCurrentInstruction() const;
    std::uint64_t getRemainingSleepTicks() const;
    std::vector<std::string> getLogs() const;

    void executeNextInstruction();
    bool tickSleep();

private:
    std::string name;
    std::uint64_t pid;
    State state;
    std::vector<Instruction> instructions;
    std::unordered_map<std::string, std::uint16_t> variables;
    std::vector<std::string> logs;
    std::size_t instructionIndex;
    std::uint64_t remainingSleepTicks;
    mutable std::mutex processMutex;

    void expandInstructions(const std::vector<Instruction>& source);
    int getOperandValue(const Operand& operand);
    static std::uint16_t clampValue(int value);
};

#endif
