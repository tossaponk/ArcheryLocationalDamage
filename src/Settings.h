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
	std::vector<StringFilterList>	race;

	LocationalDamageSetting()
	{
		effects.resize( 5 );
	}

	static StringFilter CreateFilterFromString( const char* a_filter )
	{
		auto filterOption = split( a_filter, ":" );
		StringFilter filter;
		filter.str = a_filter;

		if( filterOption.size() == 2 )
		{
			if( filterOption[ 0 ] == "S" )
				filter.type = StringFilter::Type::kActorKeyword;
			else if( filterOption[ 0 ] == "A" )
				filter.type = StringFilter::Type::kArmorKeyword;
			else if( filterOption[ 0 ] == "M" )
				filter.type = StringFilter::Type::kMagicKeyword;

			filter.str = filterOption[ 1 ];
		}

		// Set negate to true and remove a minus from the string if there is one
		if( filter.str[ 0 ] != NULL && filter.str[ 0 ] == '-' )
		{
			filter.isNegate = true;
			filter.str = filter.str.substr( 1 );
		}

		return filter;
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
