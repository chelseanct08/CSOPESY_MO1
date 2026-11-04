#include "Config.h"
#include <fstream>
#include <iostream>
#include <sstream>
#include <unordered_map>
#include <stdexcept>
#include <algorithm>

Config::Config() {
    numCpu = 4;
    scheduler = "fcfs";
    quantumCycles = 5;
    batchProcessFreq = 1;
    minIns = 10;
    maxIns = 20;
    delaysPerExec = 0;
}

static std::string sanitizeValue(std::string str) {
    while (!str.empty() && (str.front() == ' ' || str.front() == '\t' || str.front() == '\"' || str.front() == '\'')) {
        str.erase(str.begin());
    }
    while (!str.empty() && (str.back() == ' ' || str.back() == '\t' || str.back() == '\"' || str.back() == '\'')) {
        str.pop_back();
    }
    return str;
}

bool Config::loadConfig(const std::string& filename) {
    std::ifstream file(filename);

    if (!file.is_open()) {
        std::cout << "Error: Could not open " << filename << ".\n";
        return false;
    }

    std::unordered_map<std::string, std::string> settings;
    std::string line;

    while (std::getline(file, line)) {
        size_t commentPos = line.find('#');
        if (commentPos != std::string::npos) {
            line = line.substr(0, commentPos);
        }

        std::istringstream input(line);
        std::string key;
        std::string value;

        if (!(input >> key)) {
            continue;
        }

        if (!(input >> value)) {
            std::cout << "Error: Missing value for " << key << ".\n";
            return false;
        }

        settings[key] = sanitizeValue(value);
    }

    file.close();

    
    if (settings.find("delays-per-exec") == settings.end() && settings.find("delay-per-exec") != settings.end()) {
        settings["delays-per-exec"] = settings["delay-per-exec"];
    }

    const std::string requiredKeys[] = {
        "num-cpu",
        "scheduler",
        "quantum-cycles",
        "batch-process-freq",
        "min-ins",
        "max-ins",
        "delays-per-exec"
    };

    for (const std::string& key : requiredKeys) {
        if (settings.find(key) == settings.end()) {
            std::cout << "Error: Missing setting " << key << ".\n";
            return false;
        }
    }

    try {
        numCpu = std::stoi(settings["num-cpu"]);
        scheduler = settings["scheduler"];
        quantumCycles = std::stoi(settings["quantum-cycles"]);
        batchProcessFreq = std::stoi(settings["batch-process-freq"]);
        minIns = std::stoi(settings["min-ins"]);
        maxIns = std::stoi(settings["max-ins"]);
        delaysPerExec = std::stoi(settings["delays-per-exec"]);
    }
    catch (const std::exception&) {
        std::cout << "Error: A configuration value is not a valid integer.\n";
        return false;
    }

    if (numCpu < 1 || numCpu > 128) {
        std::cout << "Error: num-cpu must be between 1 and 128.\n";
        return false;
    }

    if (scheduler != "fcfs" && scheduler != "rr") {
        std::cout << "Error: scheduler must be fcfs or rr.\n";
        return false;
    }

    if (quantumCycles < 1) {
        std::cout << "Error: quantum-cycles must be at least 1.\n";
        return false;
    }

    if (batchProcessFreq < 1) {
        std::cout << "Error: batch-process-freq must be at least 1.\n";
        return false;
    }

    if (minIns < 1 || maxIns < minIns) {
        std::cout << "Error: Invalid min-ins or max-ins values.\n";
        return false;
    }

    if (delaysPerExec < 0) {
        std::cout << "Error: delays-per-exec cannot be negative.\n";
        return false;
    }

    std::cout << "Configuration loaded successfully.\n";
    return true;
}