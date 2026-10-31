#pragma once
#include <memory>
#include <string>
#include <vector>

#include "config.hpp"

struct DirEntry {
    std::string name;
    bool isDir = false;
};

// Everything that differs between the desktop build and the PS5 build. On the PS5,
// file access outside /app0 and launching go through the helper payload when it
// runs (the app's sandbox can't see other titles' folders and can't start titles).
class Platform {
public:
    virtual ~Platform() = default;
    virtual const char* name() const = 0;

    virtual bool listDir(const std::string& path, std::vector<DirEntry>& out) = 0;
    virtual bool exists(const std::string& path) = 0;
    virtual void makeDirs(const std::string& path) = 0;
    virtual bool readFile(const std::string& path, std::string& out) = 0;
    virtual bool writeFile(const std::string& path, const std::string& data) = 0;

    // Start the emulator for `sys`. gamePath may be empty (open the emulator's own menu).
    // Desktop: blocks until the emulator exits. PS5: hands the request to the helper,
    // which starts the title once the launcher has closed.
    virtual bool launch(const SystemConfig& sys, const std::string& gamePath, std::string& err) = 0;
    virtual bool launchBlocks() const = 0;

    virtual void log(const std::string& msg) = 0;
    virtual void setLogPath(const std::string& path) = 0;

    // PS5: a title must not return from main (exit() raises SIGSYS); this asks the
    // system to close it and never returns. Desktop: does nothing.
    virtual void closeApp() = 0;
};

// useHelper: route file access and launching through the helper payload.
// useHelper: file reads of /data go through the helper. helperUp: the helper answers, so a
// title can be launched through it even when the app reads /data itself.
std::unique_ptr<Platform> makePlatform(bool useHelper, bool helperUp = false);
