/*
 * PS Launcher - what is known about a game by its serial number: its name, its
 * developer and the Redump name of its disc.
 * Copyright (C) 2026 Press5elect (PSSwanStation's gamedb.cpp, ported)
 * SPDX-FileCopyrightText: 2026 Press5elect
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The data (assets/gamedb/*.tsv) is from libretro-database's PlayStation lists,
 * CC BY-SA 4.0, made by PSSwanStation's tools/make-gamedb.py. See docs/THIRD-PARTY.txt.
 */
#include "fe.h"

#include <cstdio>
#include <cstdlib>
#include <unordered_map>

namespace fe::gamedb
{
namespace
{
std::string gamesText;
std::unordered_map<std::string, uint32_t> bySerialIndex;	// serial -> the line's start
std::unordered_map<std::string, std::string> byName;		// reduced name -> serial
std::unordered_map<std::string, std::string> discName;	// serial -> Redump name (discs.tsv)
std::unordered_map<std::string, std::string> ps3Titles;	// PS3 serial -> title (ps3.tsv, from GameTDB)
std::unordered_map<std::string, std::string> gameDbTitles;	// PS2 serial -> title (gamedb-ps2.tsv, from GameDB-PS2)
std::unordered_map<std::string, std::string> psxTitles;	// PS1 serial -> title (psx.tsv, from GameDB-PSX)
std::unordered_map<std::string, std::string> psxNames;	// PS1 serial -> Redump-style name (psx.tsv, from GameDB-PSX)
std::string summaryText = "not installed (assets/gamedb)";

// The file's bytes, or false when it cannot be read.
bool readAll(const std::string& path, std::string& out)
{
	FILE *f = std::fopen(path.c_str(), "rb");
	if (f == nullptr)
		return false;
	char buf[65536];
	size_t n;
	while ((n = std::fread(buf, 1, sizeof buf, f)) > 0)
		out.append(buf, n);
	std::fclose(f);
	return !out.empty();
}

// As make-gamedb.py reduces a name: its letters and digits, lower case.
std::string reduce(const std::string& name)
{
	std::string out;
	for (const char c : name)
	{
		if (c >= 'A' && c <= 'Z')
			out += (char)(c - 'A' + 'a');
		else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))
			out += c;
	}
	return out;
}

// Each line of a tab-separated list: its first field, and the rest of the line.
template <typename Each>
void eachLine(const std::string& text, Each each)
{
	for (size_t at = 0; at < text.size();)
	{
		size_t end = text.find('\n', at);
		if (end == std::string::npos)
			end = text.size();
		const size_t tab = text.find('\t', at);
		if (tab != std::string::npos && tab < end)
			each(at, tab, end);
		at = end + 1;
	}
}
}

void init()
{
	// The PS1 lists (gamedb/) and the PS2 lists (gamedb2/), merged into one index. Each set is
	// read from the installed copy under /data/homebrew first, then the app's own folder.
	const char *roots[] = {"/data/homebrew/PPSA97500/assets/", "/app0/assets/"};
	int loaded = 0;
	for (const char *set : {"gamedb/", "gamedb2/"})
	{
		for (const char *root : roots)
		{
			const std::string dir = std::string(root) + set;
			std::string games, names, discs;
			if (!readAll(dir + "games.tsv", games))
				continue;
			const bool ok = readAll(dir + "names.tsv", names);
			(void)readAll(dir + "discs.tsv", discs);
			if (!ok)
				continue;
			if (games.back() != '\n')
				games += '\n';
			const size_t base = gamesText.size();
			eachLine(games, [&](size_t at, size_t tab, size_t) {
				bySerialIndex.emplace(games.substr(at, tab - at), (uint32_t)(base + at));
			});
			gamesText += games;
			eachLine(names, [&](size_t at, size_t tab, size_t end) {
				byName.emplace(names.substr(at, tab - at), names.substr(tab + 1, end - tab - 1));
			});
			eachLine(discs, [&](size_t at, size_t tab, size_t end) {
				discName.emplace(discs.substr(at, tab - at), discs.substr(tab + 1, end - tab - 1));
			});
			loaded++;
			break;
		}
	}
	// PS2 titles (gamedb-ps2.tsv, GameDB-PS2): serial, tab, title. The first copy found is used.
	for (const char *root : roots)
	{
		std::string text;
		if (!readAll(std::string(root) + "gamedb2/gamedb-ps2.tsv", text))
			continue;
		eachLine(text, [&](size_t at, size_t tab, size_t end) {
			if (text[at] == '#')
				return;
			gameDbTitles.emplace(text.substr(at, tab - at), text.substr(tab + 1, end - tab - 1));
		});
		break;
	}
	// PS1 titles and names (psx.tsv, GameDB-PSX): serial, tab, title, tab, release name. The first copy found is used.
		for (const char *root : roots) {
			std::string text;
			if (!readAll(std::string(root) + "gamedb/psx.tsv", text))
				continue;
			eachLine(text, [&](size_t at, size_t tab, size_t end) {
				if (text[at] == '#')
					return;
				const size_t tab2 = text.find('\t', tab + 1);
				const std::string serial = text.substr(at, tab - at);
				psxTitles.emplace(serial, text.substr(tab + 1, (tab2 < end ? tab2 : end) - tab - 1));
				if (tab2 < end)
					psxNames.emplace(serial, text.substr(tab2 + 1, end - tab2 - 1));
			});
			break;
		}
		// PS3 titles (ps3.tsv, from GameTDB): serial, tab, title. The first copy found is used.
	for (const char *root : roots)
	{
		std::string text;
		if (!readAll(std::string(root) + "gamedb/ps3.tsv", text))
			continue;
		eachLine(text, [&](size_t at, size_t tab, size_t end) {
			if (text[at] == '#')
				return;
			ps3Titles.emplace(text.substr(at, tab - at), text.substr(tab + 1, end - tab - 1));
		});
		break;
	}

	if (loaded == 0)
	{
		diag::mark("gamedb: %s", summaryText.c_str());
		return;
	}
	summaryText = format("%d games described, %d disc names", (int)bySerialIndex.size(), (int)byName.size());
	diag::mark("gamedb: %s", summaryText.c_str());
}

bool find(const std::string& serial, Info& out)
{
	const auto it = bySerialIndex.find(serial);
	if (it == bySerialIndex.end())
		return false;
	size_t end = gamesText.find('\n', it->second);
	if (end == std::string::npos)
		end = gamesText.size();
	std::string fields[9];
	size_t at = it->second;
	for (int i = 0; i < 9 && at <= end; i++)
	{
		size_t tab = i == 8 ? end : gamesText.find('\t', at);
		if (tab == std::string::npos || tab > end)
			tab = end;
		fields[i] = gamesText.substr(at, tab - at);
		at = tab + 1;
	}
	out.serial = fields[0];
	out.name = fields[1];
	out.developer = fields[2];
	out.publisher = fields[3];
	out.year = std::atoi(fields[4].c_str());
	out.month = std::atoi(fields[5].c_str());
	out.players = std::atoi(fields[6].c_str());
	out.genre = fields[7];
	out.description = fields[8];
	return true;
}

std::string ps3Title(const std::string& serial)
{
	const auto it = ps3Titles.find(serial);
	return it == ps3Titles.end() ? std::string() : it->second;
}

std::string serialByName(const std::string& fileTitle)
{
	const auto it = byName.find(reduce(fileTitle));
	return it == byName.end() ? "" : it->second;
}

std::string officialTitle(const std::string& serial)
{
	const auto it = gameDbTitles.find(serial);
	if (it != gameDbTitles.end())
		return it->second;
	const auto psx = psxTitles.find(serial);
	return psx == psxTitles.end() ? "" : psx->second;
}

std::string redumpName(const std::string& serial)
{
	const auto it = discName.find(serial);
	if (it != discName.end())
		return it->second;
	const auto psx = psxNames.find(serial);
	return psx == psxNames.end() ? "" : psx->second;
}

std::string summary()
{
	return summaryText;
}

}  // namespace fe::gamedb
