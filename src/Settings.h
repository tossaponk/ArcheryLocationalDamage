#pragma once

struct LocationalDamageSetting
{
	struct Effect
	{
		std::string effectID = "";
		int			effectChance = 0;
	};

	bool							enable = false;
	bool							shouldContinue = false;
	float							damageMult = 1.0f;
	bool							deflectProjectile = false;
	int								successChance = 100;
	float							successHPFactor = 0;
	bool							successHPFactorCap = true;
	unsigned int					floatingColorEnemy = 0xFFC800;
	unsigned int					floatingColorSelf = 0xFF4040;
	int								floatingSize = 24;
	std::string						message;
	std::string						messageFloating;
	std::string						sound;
	std::string						impactData;
	std::regex						regexp;
	std::vector<Effect>				effects;
	RE::SEX							sex;
	std::vector<StringFilterList>	filterInclude;
	std::vector<StringFilterList>	filterExclude;
	std::vector<StringFilterList>	raceInclude;
	std::vector<StringFilterList>	raceExclude;

	LocationalDamageSetting()
	{
		effects.resize( 5 );
	}

	static StringFilter CreateFilterFromString( const char* a_filter )
	{
		auto filterOption = split( a_filter, ":" );
		if( filterOption.size() == 2 )
		{
			StringFilter filter;
			if( filterOption[ 0 ] == "S" )
				filter.type = StringFilter::Type::kActorKeyword;
			else if( filterOption[ 0 ] == "A" )
				filter.type = StringFilter::Type::kArmorKeyword;
			else if( filterOption[ 0 ] == "M" )
				filter.type = StringFilter::Type::kMagicKeyword;

			auto keywordList = split( filterOption[ 1 ].c_str(), "\\+" );
			for( auto keyword : keywordList )
			{
				if( keyword[ 0 ] == '-' )
					filter.AddFilter( keyword.substr( 1 ), true );
				else
					filter.AddFilter( keyword );
			}

			return filter;
		}
		else if( filterOption.size() == 1 )
		{
			StringFilter filter;
			if( filterOption[ 0 ][ 0 ] == '-' )
				filter.AddFilter( filterOption[ 0 ].substr( 1 ), true );
			else
				filter.AddFilter( filterOption[ 0 ] );

			return filter;
		}

		stl::report_and_fail( fmt::format( "Invalid keyword format. Expecting <= 1 ':' but found {}.", filterOption.size() - 1 ) );
	}

	static void ExtractFilterStrings( std::vector<StringFilterList>& a_settingList, const char* a_filter )
	{
		auto keywords = split( a_filter, "[\\s,]+" );
		StringFilterList filterList;
		for( auto str : keywords )
			filterList.Add( CreateFilterFromString( str.c_str() ) );

		a_settingList.push_back( filterList );
	}
};

enum NotificationMode
{
	None,
	Floating,
	Screen,
	Both
};

struct Settings
{
	static void SetLocationEffect( LocationalDamageSetting& a_setting, int a_index, std::string a_effect )
	{
		if( a_setting.effects.size() < a_index + 1 )
			a_setting.effects.resize( a_index + 1 );

		a_setting.effects[ a_index ].effectID = a_effect;
	}

	static void SetLocationChance( LocationalDamageSetting& a_setting, int a_index, int a_chance )
	{
		if( a_setting.effects.size() < a_index + 1 )
			a_setting.effects.resize( a_index + 1 );

		a_setting.effects[ a_index ].effectChance = a_chance;
	}

	static void Load();
};
