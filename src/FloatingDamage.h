#pragma once

#include "Offsets.h"

struct FloatingDamage
{
	struct DisplayText
	{
		RE::GFxValue	text;
		RE::GFxValue	size;
		RE::GFxValue	color;

		bool			ownString = false;

		void _Init( const char* a_text, uint32_t a_color, uint32_t a_size )
		{
			if( ownString )
				free( (void*)text.GetString() );

			ownString = true;

			auto len = strlen( a_text );
			char* buffer = (char*)malloc( len + 1 );
			strcpy_s( buffer, len + 1, a_text );

			text = buffer;
			color = a_color;
			size = a_size;
		}

		void _Copy( const DisplayText& a_other )
		{
			_Init( 
				(const char*)a_other.text.GetString(),
				(uint32_t)a_other.color.GetNumber(),
				(uint32_t)a_other.size.GetNumber()
			);
		}

		void _Move( DisplayText& a_other )
		{
			memcpy( this, &a_other, sizeof(DisplayText) );

			a_other.ownString = false;
			a_other.text.SetNull();
		}

		DisplayText( const char* a_text, uint32_t a_color, uint32_t a_size )
		{
			_Init( a_text, a_color, a_size );
		}

		DisplayText( const DisplayText& a_other )
		{
			_Copy( a_other );
		}

		DisplayText( DisplayText&& a_other )
		{
			_Move( a_other );
		}

		~DisplayText()
		{
			if( ownString )
				free( (void*)text.GetString() );
		}

		DisplayText& operator=( const DisplayText& a_rhs )
		{
			if( this != &a_rhs )
				_Copy( a_rhs );

			return *this;
		}

		DisplayText& operator=( DisplayText&& a_rhs )
		{
			if( this != &a_rhs )
			{
				if( ownString )
					free( (void*)text.GetString() );

				ownString = true;

				text = a_rhs.text;
				color = a_rhs.color;
				size = a_rhs.size;

				a_rhs.ownString = false;
			}

			return *this;
		}
	};

	static void Initialize( REL::Version a_ver );

	static RE::GFxMovie* GetMenu();

	static bool CreateFloatingText( const char* a_text, uint32_t a_color, uint32_t a_size );

	static void Flush( RE::TESObjectREFR* a_target, RE::NiPoint3* a_location, float a_offsetX, float a_offsetY, float a_alpha = 100, bool a_ignoreLOS = false );
};
