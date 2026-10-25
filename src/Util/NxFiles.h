//////////////////////////////////////////////////////////////////////////////////
//	This file is part of the continued Journey MMORPG client					//
//	Copyright (C) 2015-2019  Daniel Allendorf, Ryan Payton						//
//																				//
//	This program is free software: you can redistribute it and/or modify		//
//	it under the terms of the GNU Affero General Public License as published by	//
//	the Free Software Foundation, either version 3 of the License, or			//
//	(at your option) any later version.											//
//																				//
//	This program is distributed in the hope that it will be useful,				//
//	but WITHOUT ANY WARRANTY; without even the implied warranty of				//
//	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the				//
//	GNU Affero General Public License for more details.							//
//																				//
//	You should have received a copy of the GNU Affero General Public License	//
//	along with this program.  If not, see <https://www.gnu.org/licenses/>.		//
//////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "../MapleStory.h"

#ifdef USE_NX
#include "../Error.h"

#include <array>
#include <stdint.h>
#include <nlnx/node.hpp>

namespace ms
{
	namespace NxFiles
	{
		// Number of needed files
		const constexpr uint8_t NUM_FILES = 17;

		// Names of the needed game files
		const constexpr std::array<const char*, NUM_FILES> filenames =
		{
			"Base.nx",
			"Character.nx",
			"Effect.nx",
			"Etc.nx",
			"Item.nx",
			"Map.nx",
			// "Map001.nx",	// From latest
			// "MapLatest.nx",	// From latest
			// "MapPretty.nx",	// From v167
			"Mob.nx",
			"Morph.nx",
			"Npc.nx",
			"Quest.nx",
			"Reactor.nx",
			"Skill.nx",
			"Sound.nx",		// From latest
			"String.nx",
			"TamingMob.nx",
			"UI.nx",			// From latest
			"UI-v83.nx"		// Login/world/character select artwork
		};

		// Initialize NX
		// When successful test if the UI file is the correct version
		Error init();

		// Login screens use v83 artwork/layout even when gameplay uses a newer UI.nx.
		// UI-v83.nx is required so this cannot silently fall back to mismatched UI.nx assets.
		nl::node login_ui();
	};
}
#endif
