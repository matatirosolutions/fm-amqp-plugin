#pragma once

#include "FMWrapper/FMXText.h"
#include "FMWrapper/FMXData.h"
#include <string>

// Convert a UTF-8 std::string to an fmx::TextUniquePtr
fmx::TextUniquePtr TextFromString(const std::string& s);

// Extract UTF-8 text from an fmx::Data parameter
std::string StringFromData(const fmx::Data& data);

// Write a UTF-8 result string into an fmx::Data result
void SetResultString(const std::string& s, fmx::Data& result);

void SetResultOK(fmx::Data& result);
void SetResultError(const std::string& message, fmx::Data& result);
