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
		kFormEditorID,

		kTotal
	};

	struct FilterData
	{
		std::string	str;
		bool		isNegate = false;
	};

	std::vector<FilterData>	data;
	Type					type = Type::kFormEditorID;
};

class StringFilterList
{
	enum class Flag
	{
		kActorKeyword = 1 << (uint32_t)StringFilter::Type::kActorKeyword,
		kArmorKeyword = 1 << (uint32_t)StringFilter::Type::kArmorKeyword,
		kMagicKeyword = 1 << (uint32_t)StringFilter::Type::kMagicKeyword,
		kFormEditorID = 1 << (uint32_t)StringFilter::Type::kFormEditorID,
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

	bool FormEditorIDMatch( RE::TESForm* a_form )
	{
		if( data.size() == 0 )
			return true;

		for( auto& keywordList : data )
		{
			if( keywordList.type == StringFilter::Type::kFormEditorID )
			{
				for( auto& keyword : keywordList.data )
				{
					std::regex filter( keyword.str );
					if( !std::regex_match( a_form->GetFormEditorID(), filter ) )
						return false;
				}
			}
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

		// Search active effects if both actor and armor has none of the keyword
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
			bool isActorHasKeyword = ActorHasKeywords( actor );
			bool isArmorHasKeyword = ArmorHasKeywords( actor );
			bool isMagicHasKeyword = ActiveEffectsHasKeywords( actor );

			return isActorHasKeyword && isArmorHasKeyword && isMagicHasKeyword;
		}
		else if( a_form->Is( RE::FormType::Race ) )
		{
			return FormEditorIDMatch( a_form );
		}

		return false;
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

extern std::regex g_sExcludeRegexp;
extern std::regex g_PlayerNodes;
static RE::NiNode* FindClosestHitNode( RE::NiNode* a_root, RE::NiPoint3* a_pos, float& a_dist, bool a_isPlayer )
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
	if( a_root->collisionObject || a_isPlayer )
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

#pragma warning(pop)
