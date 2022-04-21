#include "Utils.h"
#include "Settings.h"

std::vector<LocationalDamageSetting> g_LocationalDamageSettings;

bool g_bDebugNotification = true;
bool g_bPlayerNotification = true;
bool g_bPlayerHitSound = true;
bool g_bEffectChanceCap = true;
bool g_bAmplifyEnchantment = true;
bool g_bEnableFloatingText = true;
bool g_bHitEffectNotification = true;
bool g_bNPCFloatingNotification = false;
bool g_bIgnoreHitboxCheck = false;
float g_fHPFactor = 0.5f;
float g_fFloatingOffsetX = 0;
float g_fFloatingOffsetY = 0.05f;
long g_nNotificationMode = 1;
std::regex g_sExcludeRegexp;
std::regex g_PlayerNodes;

void Settings::Load()
{
	CSimpleIniA iniFile;
	iniFile.SetUnicode();
	iniFile.SetMultiKey();
	iniFile.LoadFile( L"Data/SKSE/Plugins/ArcheryLocationalDamage.ini" );

	g_bDebugNotification		= iniFile.GetBoolValue( "Settings", "DebugNotification", false );
	g_bPlayerNotification		= iniFile.GetBoolValue( "Settings", "PlayerHitNotification", false );
	g_bPlayerHitSound			= iniFile.GetBoolValue( "Settings", "PlayerHitSound", true );
	g_nNotificationMode			= iniFile.GetLongValue( "Settings", "HitNotificationMode", NotificationMode::Floating );
	g_bHitEffectNotification	= iniFile.GetBoolValue( "Settings", "HitEffectNotification", true );
	g_bNPCFloatingNotification	= iniFile.GetBoolValue( "Settings", "NPCHitNotification", false );
	g_bIgnoreHitboxCheck		= iniFile.GetBoolValue( "Settings", "IgnoreHitboxCheck", false );
	g_sExcludeRegexp			= iniFile.GetValue( "Settings", "LocationExclude", "" );
	g_PlayerNodes				= iniFile.GetValue( "Settings", "PlayerNodeInclude", ".*" );
	g_fHPFactor					= (float)iniFile.GetDoubleValue( "Settings", "HPFactor", 50 ) / 100.0f;
	g_bEffectChanceCap			= iniFile.GetBoolValue( "Settings", "HPFactorCap", true );
	g_bAmplifyEnchantment		= iniFile.GetBoolValue( "Settings", "AmplifyEnchantment", true );
	g_fFloatingOffsetX			= (float)iniFile.GetDoubleValue( "Settings", "FloatingTextOffsetX", 0 );
	g_fFloatingOffsetY			= (float)iniFile.GetDoubleValue( "Settings", "FloatingTextOffsetY", 0.04 );

	// Sort section priority by their number
	std::list<CSimpleIni::Entry> sectionList;
	std::regex sectionRegex( "Location([0-9]+)" );
	iniFile.GetAllSections( sectionList );
	sectionList.sort( [ sectionRegex ]( const CSimpleIni::Entry& x, const CSimpleIni::Entry& y )
		{
			std::cmatch match_x, match_y;
			std::regex_match( x.pItem, match_x, sectionRegex );
			std::regex_match( y.pItem, match_y, sectionRegex );

			return atoi( match_x[1].str().c_str() ) < atoi( match_y[1].str().c_str() );
		});

	for( auto& sectionIter : sectionList )
	{
		if( std::regex_match( sectionIter.pItem, sectionRegex ) )
		{
			LocationalDamageSetting setting;

			setting.damageMult			= (float)iniFile.GetDoubleValue( sectionIter.pItem, "Multiplier", 1.0 );
			setting.successHPFactor		= (float)iniFile.GetDoubleValue( sectionIter.pItem, "SuccessHPFactor", 0 ) / 100.0f;
			setting.successChance		= iniFile.GetLongValue( sectionIter.pItem, "SuccessChance", 100 );
			setting.successHPFactorCap	= iniFile.GetBoolValue( sectionIter.pItem, "SuccessHPFactorCap", true );
			setting.floatingColorEnemy	= iniFile.GetLongValue( sectionIter.pItem, "FloatingColorEnemy", 0xFF8000 );
			setting.floatingColorSelf	= iniFile.GetLongValue( sectionIter.pItem, "FloatingColorSelf", 0xFF4040 );
			setting.floatingSize		= iniFile.GetLongValue( sectionIter.pItem, "FloatingTextSize", 24 );
			setting.deflectProjectile	= iniFile.GetBoolValue( sectionIter.pItem, "Deflect", false );
			setting.impactData			= iniFile.GetValue( sectionIter.pItem, "ImpactData", "" );
			setting.message				= iniFile.GetValue( sectionIter.pItem, "Message", "" );
			setting.messageFloating		= iniFile.GetValue( sectionIter.pItem, "MessageFloating", "" );
			setting.sound				= iniFile.GetValue( sectionIter.pItem, "HitSound", "" );
			std::string regexp			= iniFile.GetValue( sectionIter.pItem, "Regexp", "" );

			auto sex = iniFile.GetValue( sectionIter.pItem, "Sex", "" );
			if( sex[ 0 ] != '\0' )
				setting.sex = _strcmpi( sex, "M" ) == 0 ? RE::SEX::kMale : RE::SEX::kFemale;
			else
				setting.sex = RE::SEX::kNone;

			setting.enable = regexp != "";
			setting.regexp = regexp;

			auto section = iniFile.GetSection( sectionIter.pItem );

			int effectIdx = 0;
			int chanceIdx = 0;
			for( auto iter = section->begin(); iter != section->end(); ++iter )
			{
				std::string currentKey = iter->first.pItem;

				if( currentKey == "EffectName" )
					SetLocationEffect( setting, effectIdx++, iter->second );
				else if( currentKey == "EffectChance" )
					SetLocationChance( setting, chanceIdx++, atoi( iter->second ) );
				else if( currentKey == "KeywordInclude" && *iter->second != 0 )
					LocationalDamageSetting::ExtractFilterString( setting.keywordInclude, iter->second );
				else if( currentKey == "KeywordExclude" && *iter->second != 0 )
					LocationalDamageSetting::ExtractFilterString( setting.keywordExclude, iter->second );
				else if( currentKey == "MagicKeywordInclude" && *iter->second != 0 )
					LocationalDamageSetting::ExtractFilterString( setting.magicInclude, iter->second );
				else if( currentKey == "MagicKeywordExclude" && *iter->second != 0 )
					LocationalDamageSetting::ExtractFilterString( setting.magicExclude, iter->second );
				else if( currentKey == "RaceInclude" && *iter->second != 0 )
					setting.raceInclude.push_back( iter->second );
				else if( currentKey == "RaceExclude" && *iter->second != 0 )
					setting.raceExclude.push_back( iter->second );
			}

			g_LocationalDamageSettings.push_back( setting );
		}
	}
}
