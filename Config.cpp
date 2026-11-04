#include "Config.h"

#include <fstream>
#include <iostream>
#include <stdexcept>
#include <sstream>
#include <unordered_map>

namespace {
    const std::uint64_t maximumSetting = UINT64_C(1) << 32;

    bool parseUnsigned(
        const std::string& text,
        std::uint64_t& value
    ) {
        if (text.empty()) {
            return false;
        }

        std::size_t parsedLength = 0;

        try {
            value = std::stoull(text, &parsedLength, 10);
        }
        catch (const std::exception&) {
            return false;
        }

        return parsedLength == text.size()
            && text.front() != '-'
            && value <= maximumSetting;
    }
}

Config::Config() {
    numCpu = 4;
    scheduler = "fcfs";
    quantumCycles = 5;
    batchProcessFreq = 1;
    minIns = 10;
    maxIns = 20;
    delaysPerExec = 0;
}

bool Config::loadConfig(const std::string& filename) {
    std::ifstream file(filename);

    if (!file.is_open()) {
        std::cout << "Error: Could not open " << filename << ".\n";
        return false;
    }

    std::unordered_map<std::string, std::string> settings;
    std::string line;
    std::size_t lineNumber = 0;

    while (std::getline(file, line)) {
        ++lineNumber;
        std::istringstream input(line);
        std::string key;
        std::string value;

        if (!(input >> key) || key.front() == '#') {
            continue;
        }

        if (!(input >> value)) {
            std::cout << "Error: Missing value on config.txt line "
                << lineNumber << ".\n";
            return false;
        }

        std::string extra;

        if (input >> extra && extra.front() != '#') {
            std::cout << "Error: Too many values on config.txt line "
                << lineNumber << ".\n";
            return false;
        }

        if (!settings.emplace(key, value).second) {
            std::cout << "Error: Duplicate setting " << key << ".\n";
            return false;
        }
    }

    const char* acceptedKeys[] = {
        "num-cpu",
        "scheduler",
        "quantum-cycles",
        "batch-process-freq",
        "min-ins",
        "max-ins",
        "delay-per-exec",
        "delays-per-exec"
    };

    for (const auto& setting : settings) {
        bool recognized = false;
        for (const char* key : acceptedKeys) {
            if (setting.first == key) {
                recognized = true;
                break;
            }
        }

        if (!recognized) {
            std::cout << "Error: Unsupported setting "
                << setting.first << ".\n";
            return false;
        }
    }

    const char* requiredKeys[] = {
        "num-cpu",
        "scheduler",
        "quantum-cycles",
        "batch-process-freq",
        "min-ins",
        "max-ins"
    };

    for (const char* key : requiredKeys) {
        if (settings.find(key) == settings.end()) {
            std::cout << "Error: Missing setting " << key << ".\n";
            return false;
        }
    }

    const bool hasDelayPerExec =
        settings.find("delay-per-exec") != settings.end();
    const bool hasDelaysPerExec =
        settings.find("delays-per-exec") != settings.end();

    if (!hasDelayPerExec && !hasDelaysPerExec) {
        std::cout << "Error: Missing setting delay-per-exec "
            << "(or delays-per-exec).\n";
        return false;
    }

    std::uint64_t parsedNumCpu = 0;
    std::uint64_t parsedQuantum = 0;
    std::uint64_t parsedBatchFrequency = 0;
    std::uint64_t parsedMinIns = 0;
    std::uint64_t parsedMaxIns = 0;
    std::uint64_t parsedDelay = 0;

    const std::string parsedScheduler = settings["scheduler"];
    if (parsedScheduler != "fcfs" && parsedScheduler != "rr") {
        std::cout << "Error: scheduler must be fcfs or rr.\n";
        return false;
    }

    if (!parseUnsigned(settings["num-cpu"], parsedNumCpu)
        || parsedNumCpu < 1 || parsedNumCpu > 128) {
        std::cout << "Error: num-cpu must be between 1 and 128.\n";
        return false;
    }

    if (!parseUnsigned(settings["quantum-cycles"], parsedQuantum)
        || parsedQuantum < 1) {
        std::cout << "Error: quantum-cycles must be between 1 and 2^32.\n";
        return false;
    }

    if (!parseUnsigned(settings["batch-process-freq"], parsedBatchFrequency)
        || parsedBatchFrequency < 1) {
        std::cout << "Error: batch-process-freq must be between 1 and 2^32.\n";
        return false;
    }

    if (!parseUnsigned(settings["min-ins"], parsedMinIns)
        || parsedMinIns < 1) {
        std::cout << "Error: min-ins must be between 1 and 2^32.\n";
        return false;
    }

    if (!parseUnsigned(settings["max-ins"], parsedMaxIns)
        || parsedMaxIns < 1) {
        std::cout << "Error: max-ins must be between 1 and 2^32.\n";
        return false;
    }

    const std::string& delayText = settings[
        hasDelayPerExec ? "delay-per-exec" : "delays-per-exec"
    ];

    if (!parseUnsigned(delayText, parsedDelay)) {
        std::cout << "Error: execution delay must be between 0 and 2^32.\n";
        return false;
    }

    if (hasDelayPerExec && hasDelaysPerExec) {
        std::uint64_t alternateDelay = 0;
        if (!parseUnsigned(settings["delays-per-exec"], alternateDelay)
            || alternateDelay != parsedDelay) {
            std::cout << "Error: delay-per-exec and delays-per-exec "
                << "must match when both are specified.\n";
            return false;
        }
    }

    if (parsedMinIns > parsedMaxIns) {
        std::cout << "Error: min-ins cannot exceed max-ins.\n";
        return false;
    }

    numCpu = static_cast<std::uint32_t>(parsedNumCpu);
    scheduler = parsedScheduler;
    quantumCycles = parsedQuantum;
    batchProcessFreq = parsedBatchFrequency;
    minIns = parsedMinIns;
    maxIns = parsedMaxIns;
    delaysPerExec = parsedDelay;

    std::cout << "Configuration loaded successfully.\n";
    return true;
}
