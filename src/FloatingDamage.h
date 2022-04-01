#pragma once

#include "Offsets.h"

struct FloatingDamage
{
	struct DisplayText
	{
		RE::GFxValue	text;
		RE::GFxValue	size;
		RE::GFxValue	color;
	};

	static void Initialize( REL::Version a_ver );

	static RE::GFxMovie* GetMenu();

	static bool CreateFloatingText( const char* a_text, unsigned int a_color, int a_size );

	static void Flush( RE::TESObjectREFR* a_target, RE::NiPoint3* a_location, float a_offsetX, float a_offsetY, float a_alpha = 100 );
};
