/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2026 TheSuperHackers
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "Common/WorkingDirectory.h"

namespace rts
{

Bool WorkingDirectory::s_hasSetWorkingDirectory = FALSE;
Char WorkingDirectory::s_startupWorkingDirectory[_MAX_PATH] = "";

struct WorkingDirectoryInitializer
{
	WorkingDirectoryInitializer()
	{
		WorkingDirectory::saveStartupWorkingDirectory();
	}
};

// Request capture at startup, even if no setter is called.
static WorkingDirectoryInitializer s_workingDirectoryInitializer;

Bool WorkingDirectory::saveStartupWorkingDirectory()
{
	// An earlier static constructor may call a setter before our initializer runs.
	// Cache the first capture, including failure, without logging or allocating.
	static const DWORD len = ::GetCurrentDirectory(ARRAY_SIZE(s_startupWorkingDirectory), s_startupWorkingDirectory);
	return len > 0 && len < ARRAY_SIZE(s_startupWorkingDirectory);
}

Bool WorkingDirectory::setWorkingDirectory(const char *path)
{
	if (path == nullptr || path[0] == '\0')
	{
		DEBUG_LOG(("Cannot set an empty working directory"));
		return FALSE;
	}

	if (::SetCurrentDirectory(path) == 0)
	{
		DEBUG_LOG(("Failed to set working directory to '%s' (error %d)", path, GetLastError()));
		return FALSE;
	}

	s_hasSetWorkingDirectory = TRUE;
	return TRUE;
}

Bool WorkingDirectory::setStartupWorkingDirectory()
{
	if (!saveStartupWorkingDirectory())
	{
		DEBUG_LOG(("Startup working directory is unavailable"));
		return FALSE;
	}
	return setWorkingDirectory(s_startupWorkingDirectory);
}

Bool WorkingDirectory::setExecutableWorkingDirectory()
{
	saveStartupWorkingDirectory();

	Char buffer[_MAX_PATH];
	const DWORD len = GetModuleFileName(nullptr, buffer, ARRAY_SIZE(buffer));
	if (len == 0)
	{
		DEBUG_LOG(("Failed to get executable path for working directory (error %d)", GetLastError()));
		return FALSE;
	}
	if (len >= ARRAY_SIZE(buffer))
	{
		DEBUG_LOG(("Executable path exceeds the working directory path buffer"));
		return FALSE;
	}

	Char *pEnd = strrchr(buffer, '\\');
	if (pEnd == nullptr)
	{
		DEBUG_LOG(("Executable path has no directory: '%s'", buffer));
		return FALSE;
	}
	// TheSuperHackers @bugfix For "C:\game.exe", retain "C:\" as the directory.
	// Removing the backslash leaves "C:", which refers to that drive's current directory.
	pEnd[1] = '\0';

	// TheSuperHackers @feature A build kept in a subfolder of the game folder, such as the 64-bit build in x64,
	// runs on the game data of the folder above it when its own folder has none.
	if (!hasGameData(buffer))
	{
		Char parent[_MAX_PATH];
		strlcpy(parent, buffer, ARRAY_SIZE(parent));
		const size_t parentLen = strlen(parent);
		if (parentLen >= 2)
		{
			parent[parentLen - 1] = '\0'; // drop the trailing backslash
			Char *pParentEnd = strrchr(parent, '\\');
			if (pParentEnd != nullptr)
			{
				pParentEnd[1] = '\0';
				if (hasGameData(parent))
					return setWorkingDirectory(parent);
			}
		}
	}

	return setWorkingDirectory(buffer);
}

Bool WorkingDirectory::hasGameData(const char *directory)
{
	// One of the archives every install has, Zero Hour first.
	static const char *const dataFiles[] = { "INIZH.big", "INI.big" };

	for (size_t i = 0; i < ARRAY_SIZE(dataFiles); ++i)
	{
		Char path[_MAX_PATH];
		snprintf(path, ARRAY_SIZE(path), "%s%s", directory, dataFiles[i]);
		if (::GetFileAttributes(path) != 0xFFFFFFFF) // INVALID_FILE_ATTRIBUTES, which VC6 does not define
			return TRUE;
	}
	return FALSE;
}

Bool WorkingDirectory::setCustomWorkingDirectory(const char *path)
{
	saveStartupWorkingDirectory();
	return setWorkingDirectory(path);
}

Bool WorkingDirectory::hasSetWorkingDirectory()
{
	return s_hasSetWorkingDirectory;
}

} // namespace rts
