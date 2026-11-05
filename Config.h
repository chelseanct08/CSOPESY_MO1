#ifndef CONFIG_H
#define CONFIG_H

#include <cstdint>
#include <string>

class Config {
public:
    std::uint32_t numCpu;
    std::string scheduler;
    std::uint64_t quantumCycles;
    std::uint64_t batchProcessFreq;
    std::uint64_t minIns;
    std::uint64_t maxIns;
    std::uint64_t delaysPerExec;

    Config();

    bool loadConfig(const std::string& filename);
};

#endif

