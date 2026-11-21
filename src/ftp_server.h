#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ftp {

class Server {
public:
    bool start(uint16_t port = 1337);
    void stop();

    bool isRunning() const;
    uint16_t port() const;
    int activeClients() const;
    uint64_t bytesTransferred() const;

    // Returns true once whenever a file upload/rename/delete modified /switch/
    bool consumeLibraryDirty(std::string &lastFileOut);
    std::vector<std::string> recentLogs() const;
};

}  // namespace ftp
