/*
 * PS Launcher - the frontend functions the disc drive code calls, for the PS5 app.
 * Copyright (C) 2026 PS Launcher contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The disc code (discdrive.cpp, ps5/drive_ps5.cpp, ps5/usbdrive_ps5.cpp) was
 * taken from PSSwanStation by Press5elect and calls PSSwanStation's frontend
 * (fe.h). PS Launcher has none of that frontend, so these are the parts the
 * disc code needs: files and time, the drive settings, and stubs for the
 * network shares, the game database and the library, which come later.
 *
 * Based on PSSwanStation by Press5elect.
 */
#include "fe.h"

#include <dirent.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstring>

// main.cpp: writes a line to the app's main.log.
void pslDiscLog(const char *line);

namespace fe
{

// The title's folder on the console, where its games and files are.
std::string rootDir = "/data/SwanStationPS5/";  // with the trailing slash the disc code expects

std::string format(const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	va_list copy;
	va_copy(copy, args);
	int size = std::vsnprintf(nullptr, 0, fmt, copy);
	va_end(copy);
	std::string out;
	if (size > 0)
	{
		out.resize((size_t)size + 1);
		std::vsnprintf(&out[0], out.size(), fmt, args);
		out.resize((size_t)size);
	}
	va_end(args);
	return out;
}

namespace diag
{
void mark(const char *format, ...)
{
	va_list args;
	va_start(args, format);
	char buf[512];
	std::vsnprintf(buf, sizeof buf, format, args);
	va_end(args);
	pslDiscLog(buf);
}
}

bool fileExists(const std::string& path)
{
	struct stat st;
	return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

bool dirExists(const std::string& path)
{
	struct stat st;
	return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

void makeDir(const std::string& path)
{
	// Each parent first, as mkdir -p does.
	for (size_t i = 1; i <= path.size(); ++i)
	{
		if (i != path.size() && path[i] != '/')
			continue;
		std::string part = path.substr(0, i);
		if (part.empty())
			continue;
		if (mkdir(part.c_str(), 0777) != 0 && errno != EEXIST)
			return;
	}
}

std::string trim(const std::string& text)
{
	const char *ws = " \t\r\n";
	size_t begin = text.find_first_not_of(ws);
	if (begin == std::string::npos)
		return "";
	size_t end = text.find_last_not_of(ws);
	return text.substr(begin, end - begin + 1);
}

double now()
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

namespace platform
{
// No USB drive is read by this build yet; the disc drive is found through the
// console's /dev instead (drive_ps5.cpp).
bool usbAvailable()
{
	return false;
}

std::string drivesProblem()
{
	return "";
}

std::vector<std::string> usbGameDirs()
{
	return {};
}

std::vector<std::string> usbDrives()
{
	return {};
}
}

namespace options
{
// The frontend's settings. Until PS Launcher has a settings screen for them,
// the USB disc drive is on, read at the speed the drive allows.
Frontend& frontend()
{
	static Frontend settings;
	settings.usb = true;
	settings.discDrive = true;
	return settings;
}
}

namespace smb
{
const std::vector<std::string>& gameFolders()
{
	static const std::vector<std::string> none;
	return none;
}

bool canWrite(const std::string& path)
{
	(void)path;
	return false;
}

bool writeFile(const std::string& path, const void *data, size_t bytes, std::string& error)
{
	(void)path;
	(void)data;
	(void)bytes;
	error = "network folders are not in this build";
	return false;
}

bool makeFolders(const std::string& path)
{
	(void)path;
	return false;
}

bool remove(const std::string& path)
{
	(void)path;
	return false;
}

int stat(const std::string& path, Entry& entry)
{
	(void)path;
	(void)entry;
	return -1;	// the share cannot be reached
}
}

namespace library
{
void scan(int source, bool force)
{
	(void)source;
	(void)force;	// the library is not built in this version
}
}

}  // namespace fe
