#include "Utils.h"
#include "Settings.h"

std::vector<Settings::Location> g_LocationalDamageSettings;

bool g_bDebugNotification = true;
bool g_bPlayerNotification = true;
bool g_bPlayerHitSound = true;
bool g_bEffectChanceCap = true;
bool g_bAmplifyEnchantment = true;
bool g_bEnableFloatingText = true;
bool g_bHitEffectNotification = true;
bool g_bNPCFloatingNotification = false;
bool g_bIgnoreHitboxCheck = false;
bool g_bEnableDifficultyBonus = true;
bool g_bEnableLocationMultiplier = true;
float g_fShotDifficultyTimeFactor = 1;
float g_fShotDifficultyDistFactor = 1;
float g_fShotDifficultyMoveFactor = 1;
float g_fShotDifficultyMax = 15.0f;
float g_fShotDifficultyReportMin = 1.1f;
float g_fHPFactor = 0.25f;
float g_fFloatingOffsetX = 0;
float g_fFloatingOffsetY = 0.04f;
long g_nNotificationMode = NotificationMode::Floating;
long g_nEXPNotificationMode = NotificationMode::Screen;
std::regex g_sExcludeRegexp;
std::regex g_PlayerNodes;

void Settings::Load()
{
	CSimpleIniA iniFile;
	iniFile.SetUnicode();
	iniFile.SetMultiKey();
	iniFile.LoadFile( L"Data/SKSE/Plugins/ArcheryLocationalDamage.ini" );

	if( iniFile.GetLongValue( "Version", "Major", 1 ) < 2 )
		stl::report_and_fail( "You are using an old version of the INI file. Please download the new version or read the mod description page on how to upgrade the INI to the new version before continuing." );

	g_bEnableLocationMultiplier		= iniFile.GetBoolValue( "Experience", "EnableLocationMultiplier", g_bEnableLocationMultiplier );
	g_bEnableDifficultyBonus		= iniFile.GetBoolValue( "Experience", "EnableDifficultyBonus", g_bEnableDifficultyBonus );
	g_fShotDifficultyTimeFactor		= (float)iniFile.GetDoubleValue( "Experience", "ShotDifficultyTimeFactor", g_fShotDifficultyTimeFactor );
	g_fShotDifficultyDistFactor		= (float)iniFile.GetDoubleValue( "Experience", "ShotDifficultyDistFactor", g_fShotDifficultyDistFactor );
	g_fShotDifficultyMoveFactor		= (float)iniFile.GetDoubleValue( "Experience", "ShotDifficultyMoveFactor", g_fShotDifficultyMoveFactor );
	g_fShotDifficultyMax			= (float)iniFile.GetDoubleValue( "Experience", "ShotDifficultyMax", g_fShotDifficultyMax );
	g_fShotDifficultyReportMin		= (float)iniFile.GetDoubleValue( "Experience", "ShotDifficultyReportMin", g_fShotDifficultyReportMin );

	g_bDebugNotification			= iniFile.GetBoolValue( "Settings", "DebugNotification", g_bDebugNotification );
	g_bPlayerNotification			= iniFile.GetBoolValue( "Settings", "PlayerHitNotification", g_bPlayerNotification );
	g_bPlayerHitSound				= iniFile.GetBoolValue( "Settings", "PlayerHitSound", g_bPlayerHitSound );
	g_nNotificationMode				= iniFile.GetLongValue( "Settings", "HitNotificationMode", g_nNotificationMode );
	g_nEXPNotificationMode			= iniFile.GetLongValue( "Settings", "EXPNotificationMode", g_nEXPNotificationMode );
	g_bHitEffectNotification		= iniFile.GetBoolValue( "Settings", "HitEffectNotification", g_bHitEffectNotification );
	g_bNPCFloatingNotification		= iniFile.GetBoolValue( "Settings", "NPCHitNotification", g_bNPCFloatingNotification );
	g_bIgnoreHitboxCheck			= iniFile.GetBoolValue( "Settings", "IgnoreHitboxCheck", g_bIgnoreHitboxCheck );
	g_sExcludeRegexp				= iniFile.GetValue( "Settings", "LocationExclude", "" );
	g_PlayerNodes					= iniFile.GetValue( "Settings", "PlayerNodeInclude", ".*" );
	g_fHPFactor						= (float)iniFile.GetDoubleValue( "Settings", "HPFactor", 25 ) / 100.0f;
	g_bEffectChanceCap				= iniFile.GetBoolValue( "Settings", "HPFactorCap", g_bEffectChanceCap );
	g_bAmplifyEnchantment			= iniFile.GetBoolValue( "Settings", "AmplifyEnchantment", g_bAmplifyEnchantment );
	g_fFloatingOffsetX				= (float)iniFile.GetDoubleValue( "Settings", "FloatingTextOffsetX", g_fFloatingOffsetX );
	g_fFloatingOffsetY				= (float)iniFile.GetDoubleValue( "Settings", "FloatingTextOffsetY", g_fFloatingOffsetY );

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
			Location setting;

			setting.shouldContinue		= iniFile.GetBoolValue( sectionIter.pItem, "Continue", false );
			setting.damageMult			= (float)iniFile.GetDoubleValue( sectionIter.pItem, "Multiplier", 1.0 );
			setting.difficulty			= (float)iniFile.GetDoubleValue( sectionIter.pItem, "Difficulty", setting.damageMult );
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
			auto regexp					= iniFile.GetValue( sectionIter.pItem, "Regexp", "" );

			setting.enable		= regexp[ 0 ] != NULL;
			setting.regexp		= CreateRegex( regexp );

			auto sex = iniFile.GetValue( sectionIter.pItem, "Sex", "" );
			if( sex[ 0 ] != '\0' )
				setting.filter.sex = _strcmpi( sex, "M" ) == 0 ? RE::SEX::kMale : RE::SEX::kFemale;
			else
				setting.filter.sex = RE::SEX::kNone;
			
			setting.filter.editorID	= CreateRegex( iniFile.GetValue( sectionIter.pItem, "EditorID", "" ) );
			setting.filter.ammoType	= (AmmoType)iniFile.GetLongValue( sectionIter.pItem, "AmmoType", 0 );

			auto section = iniFile.GetSection( sectionIter.pItem );

			int effectIdx = 0;
			int chanceIdx = 0;
			for( auto iter = section->begin(); iter != section->end(); ++iter )
			{
				std::string currentKey = iter->first.pItem;

				if( currentKey == "Effect" )
					ParseLocationEffect( setting, iter->second );
				else if( currentKey == "EffectName" )
					SetLocationEffect( setting, effectIdx++, iter->second );
				else if( currentKey == "EffectChance" )
					SetLocationChance( setting, chanceIdx++, atoi( iter->second ) );
				else if( currentKey == "KeywordInclude" && *iter->second != 0 )
					ExtractFilterStrings( setting.filter.keywordInclude, iter->second );
				else if( currentKey == "KeywordExclude" && *iter->second != 0 )
					ExtractFilterStrings( setting.filter.keywordExclude, iter->second );
				else if( currentKey == "RaceInclude" && *iter->second != 0 )
					setting.filter.raceInclude.push_back( CreateRegex( iter->second ) );
				else if( currentKey == "RaceExclude" && *iter->second != 0 )
					setting.filter.raceExclude.push_back( CreateRegex( iter->second ) );
			}

			g_LocationalDamageSettings.push_back( setting );
		}
	}
}
