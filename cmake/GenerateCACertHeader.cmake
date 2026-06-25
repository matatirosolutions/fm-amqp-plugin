# Reads INPUT (a PEM file) and writes OUTPUT (a C++ header with embedded bytes).
# Run at configure time via execute_process.

file(READ "${INPUT}" data HEX)
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," data "${data}")

file(WRITE "${OUTPUT}"
"#pragma once
// Auto-generated from cacert.pem — do not edit.
static const unsigned char kCACertData[] = {
${data}
};
static const unsigned int kCACertSize = sizeof(kCACertData);
")
