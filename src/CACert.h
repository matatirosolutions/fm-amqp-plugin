#pragma once
#include <string>

// Returns the path to the bundled Mozilla CA bundle (cacert.pem), located
// relative to the plugin binary itself. Returns an empty string if not found.
std::string GetBundledCACertPath();
