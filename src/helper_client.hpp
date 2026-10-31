#pragma once
#include <string>

// Talks to the PS Launcher helper payload on 127.0.0.1:9133 (payload/helper.c).
// One command per connection; the reply is everything the helper sends back.
namespace helper {

constexpr int kPort = 9133;

// Sends "<command>\n" plus an optional body, then reads the whole reply.
// Returns false when the helper isn't reachable.
bool request(const std::string& command, std::string& reply, const std::string* body = nullptr,
             int timeoutMs = 3000);

// Splits "OK\n<data>" into data; false on "ERR" or no answer.
bool requestData(const std::string& command, std::string& data, const std::string* body = nullptr);

bool ping(std::string& version);

}  // namespace helper
