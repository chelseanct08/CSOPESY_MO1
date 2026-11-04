#ifndef CONFIG_H
#define CONFIG_H

#include <string>

class Config {
public:
    int numCpu;
    std::string scheduler;
    int quantumCycles;
    int batchProcessFreq;
    int minIns;
    int maxIns;
    int delaysPerExec;

    Config();

    bool loadConfig(const std::string& filename);
};

#endif
#pragma once
