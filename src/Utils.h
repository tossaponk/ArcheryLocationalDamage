#pragma once
#pragma warning(push)
#pragma warning(disable: 4505)

struct StringFilter
{
	enum class Type
	{
		kNone = -1,
		kActorKeyword,
		kArmorKeyword,
		kMagicKeyword,

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
		kActorKeyword = 1 << (uint32_t)StringFilter::Type::kActorKeyword,
		kArmorKeyword = 1 << (uint32_t)StringFilter::Type::kArmorKeyword,
		kMagicKeyword = 1 << (uint32_t)StringFilter::Type::kMagicKeyword,
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
		if( !HasFilterType( StringFilter::Type::kArmorKeyword ) )
			return true;

		std::vector<StringFilter*> lookupFilter;
		for( auto& filter : data )
		{
			if( filter.type == StringFilter::Type::kArmorKeyword )
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
					// Effect must active to count for keyword matching
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

	bool Evaluate( RE::TESForm* a_form )
	{
		if( a_form->Is( RE::FormType::ActorCharacter ) )
		{
			auto actor = static_cast<RE::Actor*>( a_form );
			bool isActorHasKeyword	= ActorHasKeywords( actor );
			bool isArmorHasKeyword	= ArmorHasKeywords( actor );
			bool isMagicHasKeyword	= ActiveEffectsHasKeywords( actor );

			return isActorHasKeyword && isArmorHasKeyword && isMagicHasKeyword;
		}

		return false;
	}
};

struct ActorFilter
{
	std::regex						editorID;
	RE::SEX							sex;
	std::vector<StringFilterList>	keywordInclude;
	std::vector<StringFilterList>	keywordExclude;
	std::vector<std::regex>			raceInclude;
	std::vector<std::regex>			raceExclude;

	// Editor ID map must be provided to filter by form editor ID
	bool IsActorVaild( RE::Actor* a_actor, std::unordered_map<RE::FormID,std::string>* a_editorIDMap = NULL )
	{
		// Default to true if there is no filter.
		bool isVaild = keywordInclude.size() == 0;

		// Check if the actor actually has a keyword
		for( auto& filter : keywordInclude )
		{
			if( filter.Evaluate( a_actor ) )
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
				if( filter.Evaluate( a_actor ) )
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
			auto race = a_actor->GetRace();
			for( auto& filter : raceInclude )
			{
				isVaild = std::regex_match( race->GetFullName(), filter );

				if( isVaild )
					break;
			}
		}

		if( isVaild && raceExclude.size() > 0 )
		{
			auto race = a_actor->GetRace();
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
			auto targetSex = a_actor->GetActorBase()->GetSex();
			if( targetSex != RE::SEX::kNone )
				isVaild = targetSex == sex;
		}

		// Editor ID test
		if( isVaild && !editorID._Empty() )
		{
			if( a_editorIDMap )
			{
				auto base = a_actor->GetActorBase();
				if( base )
				{
					auto& baseEditorID = (*a_editorIDMap)[ base->GetFormID() ];
					isVaild = std::regex_match( baseEditorID, editorID );
				}
			}
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

static float CalculateShotDifficulty( RE::Projectile* a_projectile, RE::Actor* a_target, float a_flightTimeFactor, float a_moveFactor )
{
	float shotDifficulty = a_projectile->lifeRemaining * a_flightTimeFactor;

	// Add moving target bonus
	RE::NiPoint3 targetVelocity;
	RE::NiPoint3 attackVector;
	a_projectile->GetLinearVelocity( attackVector );
	a_target->GetLinearVelocity( targetVelocity );

	float sizeFactor = 1;
	auto targetController = a_target->GetCharController();
	RE::BSBound* targetBound = NULL;
	if( targetController )
	{
		targetBound = &targetController->collisionBound;
		if( targetBound )
			sizeFactor *= 65.0f / targetBound->extents.z; // Bonus for a short target like a rabbit or a penalty on tall target. (65 is normal sized NPC)
	}

	// Calculate cross vector for shot difficulty
	// A target moving toward or away from the player is not that hard to shoot
	// But it becomes a lot harder when they're moving perpendicular to the player, especially when they're really far away
	attackVector.Unitize();
	auto targetSpeed = targetVelocity.Unitize();
	RE::NiPoint3 movementCross = targetVelocity.Cross( attackVector );
	if( targetSpeed != 0 )
	{
		float crossFactor = movementCross.Length();
		float movementBonus = targetSpeed / 500.0f;
		float movementDifficulty = pow( 3.0f, 1 + a_projectile->lifeRemaining * 2 ) - 3;
		movementDifficulty *= ( movementBonus + crossFactor ) * a_moveFactor;

		// extents.y is the width of the side of an actor when it's moving forward
		if( targetBound && targetBound->extents.y != 0 )
		{
			float bodyLengthSpeed = targetSpeed / targetBound->extents.y; // Speed vs size in body length per second
			float bodySpeedFactor = 1 + bodyLengthSpeed / 5.0f * crossFactor;
			movementDifficulty *= bodySpeedFactor;
		}
			
		shotDifficulty += movementDifficulty;
	}

	// Convert to multiplier
	return shotDifficulty + 1;
}

#pragma warning(pop)
