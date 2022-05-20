#pragma once
#pragma warning(push)
#pragma warning(disable: 4505)

enum class AmmoType
{
	Both,
	Arrow,
	Bolt
};

struct StringFilter
{
	enum class Type
	{
		kNone = -1,
		kActorKeyword,
		kEquipKeyword,
		kMagicKeyword,
		kShooterKeyword,

		kTotal
	};

	struct FilterData
	{
		std::string	str;
		bool		isNegate = false;
	};

	std::vector<FilterData>	data;
	Type					type = Type::kNone;

	void AddFilter( std::string a_filter, bool isNegate = false )
	{
		FilterData newFilter;
		newFilter.str = a_filter;
		newFilter.isNegate = isNegate;
		
		data.push_back( newFilter );
	}
};

class StringFilterList
{
	enum class Flag
	{
		kActorKeyword	= 1 << (uint32_t)StringFilter::Type::kActorKeyword,
		kEquipKeyword	= 1 << (uint32_t)StringFilter::Type::kEquipKeyword,
		kMagicKeyword	= 1 << (uint32_t)StringFilter::Type::kMagicKeyword,
		kShooterKeyword = 1 << (uint32_t)StringFilter::Type::kShooterKeyword,
	};

	std::vector<StringFilter>		data;
	stl::enumeration<Flag,uint32_t>	flags;

public:
	void Add( StringFilter a_filter )
	{
		data.push_back( a_filter );
		flags.set( (Flag)( 1 << (uint32_t)a_filter.type ) );
	}

	bool HasFilterType( StringFilter::Type a_type )
	{
		return flags.any( (Flag)( 1 << (uint32_t)a_type ) );
	}

	static bool FormHasKeywords( RE::BGSKeywordForm* a_form, StringFilter& a_filter )
	{
		for( auto& keyword : a_filter.data )
		{
			bool hasKeyword = a_form->HasKeywordString( keyword.str );

			if( keyword.isNegate )
				hasKeyword = !hasKeyword;

			if( !hasKeyword )
				return false;
		}

		return true;
	}

	bool ActiveEffectsHasKeywords( RE::Actor* a_actor )
	{
		if( !HasFilterType( StringFilter::Type::kMagicKeyword ) )
			return true;

		std::vector<StringFilter*> lookupFilter;
		for( auto& filter : data )
		{
			if( filter.type == StringFilter::Type::kMagicKeyword )
				lookupFilter.push_back( &filter );
		}

		auto activeEffects = a_actor->GetActiveEffectList();
		for( auto activeEffect : *activeEffects )
		{
			if( lookupFilter.size() == 0 )
				break;

			for( auto iter = lookupFilter.begin(); iter != lookupFilter.end(); )
			{
				// Effect must active to count for keyword matching
				if( activeEffect->flags.none( RE::ActiveEffect::Flag::kInactive ) &&
					FormHasKeywords( activeEffect->effect->baseEffect, **iter ) )
				{
					iter = lookupFilter.erase( iter );
				}
				else
					++iter;
			}
		}

		// Success when all filters matched
		return lookupFilter.size() == 0;
	}

	bool ActorHasKeywords( RE::Actor* a_actor )
	{
		if( !HasFilterType( StringFilter::Type::kActorKeyword ) )
			return true;

		for( auto& keywordList : data )
		{
			for( auto& keyword : keywordList.data )
			{
				bool hasKeyword = a_actor->HasKeywordString( keyword.str );
				if( keyword.isNegate )
					hasKeyword = !hasKeyword;

				if( !hasKeyword && keywordList.type == StringFilter::Type::kActorKeyword )
					return false;
			}
		}

		return true;
	}

	bool ArmorHasKeywords( RE::Actor* a_actor )
	{
		if( !HasFilterType( StringFilter::Type::kEquipKeyword ) )
			return true;

		std::vector<StringFilter*> lookupFilter;
		for( auto& filter : data )
		{
			if( filter.type == StringFilter::Type::kEquipKeyword )
				lookupFilter.push_back( &filter );
		}

		const auto inv = a_actor->GetInventory([](RE::TESBoundObject& a_object) {
			return a_object.IsArmor();
			});

		for( const auto& [item, invData] : inv ) 
		{
			if( lookupFilter.size() == 0 )
				break;

			const auto& [count, entry] = invData;
			if( count > 0 && entry->IsWorn() ) 
			{
				const auto armor = item->As<RE::TESObjectARMO>();
				for( auto iter = lookupFilter.begin(); iter != lookupFilter.end(); )
				{
					if( armor && FormHasKeywords( armor, **iter ) )
					{
						iter = lookupFilter.erase( iter );
					}
					else
						++iter;
				}
			}
		}

		// Success when all filters matched
		return lookupFilter.size() == 0;
	}

	bool ShooterHasKeyword( RE::Projectile* a_projectile )
	{
		if( a_projectile == nullptr )
			return false;

		if( !HasFilterType( StringFilter::Type::kShooterKeyword ) )
			return true;

		std::vector<StringFilter*> lookupFilter;
		for( auto& filter : data )
		{
			if( filter.type == StringFilter::Type::kShooterKeyword )
				lookupFilter.push_back( &filter );
		}

		auto weapon = a_projectile->weaponSource;
		auto ammo	= a_projectile->ammoSource;
		if( !weapon || !ammo )
			return false;

		for( auto iter = lookupFilter.begin(); iter != lookupFilter.end(); )
		{
			if( FormHasKeywords( weapon, **iter ) || FormHasKeywords( ammo, **iter ) )
				iter = lookupFilter.erase( iter );
			else
				++iter;
		}

		return lookupFilter.size() == 0;
	}

	bool Evaluate( RE::Actor* a_actor, RE::Projectile* a_source )
	{
		bool isActorHasKeyword	= ActorHasKeywords( a_actor );
		bool isArmorHasKeyword	= ArmorHasKeywords( a_actor );
		bool isMagicHasKeyword	= ActiveEffectsHasKeywords( a_actor );
		bool isSourceHasKeyword	= ShooterHasKeyword( a_source );

		return isActorHasKeyword && isArmorHasKeyword && isMagicHasKeyword && isSourceHasKeyword;
	}
};

struct TargetFilter
{
	AmmoType						ammoType;
	std::regex						editorID;
	RE::SEX							sex;
	std::vector<StringFilterList>	keywordInclude;
	std::vector<StringFilterList>	keywordExclude;
	std::vector<std::regex>			raceInclude;
	std::vector<std::regex>			raceExclude;

	// Editor ID map must be provided to filter by form editor ID
	bool IsVaild( RE::Actor* a_target, RE::Projectile* a_source, std::unordered_map<RE::FormID,std::string>* a_editorIDMap = NULL )
	{
		// Default to true if there is no filter.
		bool isVaild = keywordInclude.size() == 0;

		// Check if the actor actually has a keyword
		for( auto& filter : keywordInclude )
		{
			if( filter.Evaluate( a_target, a_source ) )
			{
				isVaild = true;
				break;
			}
		}

		// Check for exclusion filter
		if( isVaild && keywordExclude.size() > 0 )
		{
			for( auto& filter : keywordExclude )
			{
				if( filter.Evaluate( a_target, a_source ) )
				{
					isVaild = false;
					break;
				}
			}
		}

		// Check for race
		if( isVaild && raceInclude.size() > 0 )
		{
			isVaild = false;
			auto race = a_target->GetRace();
			for( auto& filter : raceInclude )
			{
				isVaild = std::regex_match( race->GetFullName(), filter );

				if( isVaild )
					break;
			}
		}

		if( isVaild && raceExclude.size() > 0 )
		{
			auto race = a_target->GetRace();
			for( auto& filter : raceExclude )
			{
				isVaild = std::regex_match( race->GetFullName(), filter );

				if( !isVaild )
					break;
			}
		}

		// Check for sex
		if( isVaild && sex != RE::SEX::kNone )
		{
			auto targetSex = a_target->GetActorBase()->GetSex();
			if( targetSex != RE::SEX::kNone )
				isVaild = targetSex == sex;
		}

		// Editor ID test
		if( isVaild && !editorID._Empty() )
		{
			if( a_editorIDMap )
			{
				auto base = a_target->GetActorBase();
				if( base )
				{
					auto baseRoot = base->GetRootFaceNPC();
					auto& baseEditorID = (*a_editorIDMap)[ baseRoot->GetFormID() ];
					isVaild = std::regex_match( baseEditorID, editorID );
				}
			}
			else
				isVaild = false;
		}

		// Ammo type test
		if( isVaild && ammoType != AmmoType::Both && a_source->ammoSource )
		{
			if( (a_source->ammoSource->IsBolt() && ammoType == AmmoType::Bolt) ||
				(!a_source->ammoSource->IsBolt() && ammoType == AmmoType::Arrow) )
				isVaild = true;
			else
				isVaild = false;
		}

		return isVaild;
	}
};

static std::vector<std::string> split( const char* a_input, const char* a_regex ) 
{
	// passing -1 as the submatch index parameter performs splitting
	std::string in(a_input);
	std::regex re(a_regex);
	std::sregex_token_iterator
		first{in.begin(), in.end(), re, -1},
		last;
	return {first, last};
}

static std::regex CreateRegex( const char* a_str )
{
	if( a_str == NULL || a_str[ 0 ] == NULL )
		return std::regex();

	try
	{
		return std::regex( a_str );
	}
	catch( std::regex_error e )
	{
		stl::report_and_fail( fmt::format( "Regular expression error: {} is not vaild.\n{}", a_str, e.what() ) );
	}
}

extern std::regex g_sExcludeRegexp;
extern std::regex g_PlayerNodes;
static RE::NiNode* FindClosestHitNode( RE::NiNode* a_root, RE::NiPoint3* a_pos, float& a_dist, bool a_isPlayer, bool a_ignoreHitboxCheck = false )
{
	float childMinDist = 1000000;
	RE::NiNode* childNode = NULL;
	if( a_root->children.size() > 0 )
	{
		for( auto iter = a_root->children.begin(); iter != a_root->children.end(); iter++ )
		{
			float childDist;
			auto avNode = iter->get();
			if( avNode )
			{
				auto node = avNode->AsNode();
				if( node )
				{
					auto childHit = FindClosestHitNode( node, a_pos, childDist, a_isPlayer );

					if( childDist < childMinDist )
					{
						childMinDist = childDist;
						childNode = childHit;
					}
				}
			}
		}
	}

	// Only check against node with collision object
	// Or if player is in first person mode then all of the nodes will not having any collision object
	if( a_ignoreHitboxCheck || a_isPlayer || a_root->collisionObject )
	{
		// Do not check excluded node
		if( !std::regex_match( a_root->name.c_str(), g_sExcludeRegexp ) )
		{
			if( !a_isPlayer || std::regex_match( a_root->name.c_str(), g_PlayerNodes ) )
			{
				auto* translation = &a_root->world.translate;
				float dx = translation->x - a_pos->x;
				float dy = translation->y - a_pos->y;
				float dz = translation->z - a_pos->z;
				a_dist = dx * dx + dy * dy + dz * dz;

				if( childMinDist < a_dist )
				{
					a_dist = childMinDist;
					return childNode;
				}

				return a_root->AsNode();
			}
		}
	}

	if( childNode )
	{
		a_dist = childMinDist;
		return childNode;
	}

	a_dist = 1000000;
	return NULL;
}

static float CalculateShotDifficulty( RE::Projectile* a_projectile, RE::Actor* a_target, float a_flightTimeFactor, float a_distanceFactor, float a_moveFactor )
{
	// First 0.1 second of flight time do not count as time bonus
	float timeBonus = max( a_projectile->lifeRemaining - 0.1f, 0 );
	float timeDifficulty = ( powf( 1 + timeBonus, 2 ) - 1 ) / 2;
	timeDifficulty *= a_flightTimeFactor;

	// Add moving target bonus
	RE::NiPoint3 targetVelocity;
	RE::NiPoint3 attackVector;
	a_projectile->GetLinearVelocity( attackVector );
	a_target->GetLinearVelocity( targetVelocity );

	float targetSpeed		= targetVelocity.Unitize();
	float projectileSpeed	= attackVector.Unitize();

	float sizeFactor = 1;
	auto targetController = a_target->GetCharController();
	RE::BSBound* targetBound = NULL;
	if( targetController )
	{
		targetBound = &targetController->collisionBound;
		if( targetBound )
			sizeFactor *= 65.0f / targetBound->extents.z; // Bonus for a short target like a rabbit or a penalty on tall target. (65 is normal sized NPC)
	}

	// First 0.1 second of travelled distance does not count
	float distBonus = max( a_projectile->distanceMoved - projectileSpeed * 0.1f, 0 );
	float distDifficulty = ( powf( 1 + distBonus / 6000.0f, 2 ) - 1 ) / 2;
	distDifficulty *= a_distanceFactor;

	// Calculate cross vector for shot difficulty
	// A target moving toward or away from the player is not that hard to shoot
	// But it becomes a lot harder when they're moving perpendicular to the player, especially when they're really far away

	RE::NiPoint3 movementCross = targetVelocity.Cross( attackVector );
	float crossFactor = 0;
	float movementFactor = 0;
	float bodyLengthSpeed = 0;
	float movementDifficulty = 0;
	if( targetSpeed != 0 )
	{
		crossFactor = movementCross.Length();
		// extents.y is the width of the side of an actor when it's moving forward
		if( targetBound && targetBound->extents.y != 0 )
		{
			bodyLengthSpeed = targetSpeed / targetBound->extents.y; // Speed vs size in body length per second
			movementFactor = bodyLengthSpeed / 2.5f * crossFactor;
		}

		movementDifficulty = powf( 2.0f, 1 + a_projectile->lifeRemaining * 2 ) - 2;
		movementDifficulty *= movementFactor * crossFactor * a_moveFactor;
	}

	// Multiply by size factor
	distDifficulty		*= sizeFactor;
	movementDifficulty	*= sizeFactor;

	float shotDifficulty = 0;
	shotDifficulty		+= timeDifficulty;
	shotDifficulty		+= distDifficulty;
	shotDifficulty		+= movementDifficulty;

	// Multiply difficulty by 2 if the target is flying (A flying dragon is very hard to hit)
	if( a_target->IsFlying() )
		shotDifficulty *= 2;

#ifdef _DEBUG
	RE::ConsoleLog::GetSingleton()->Print( "TDiff: %0.2f, DDiff: %0.2f, CFactor: %0.2f, Spd: %0.1f, BSpd: %0.2f, MFactor: %0.2f, SFactor: %0.2f",
		timeDifficulty, distDifficulty, crossFactor, targetSpeed, bodyLengthSpeed, movementFactor, sizeFactor );
#endif

	// Convert to multiplier
	return shotDifficulty + 1;
}

#pragma warning(pop)
