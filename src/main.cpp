namespace
{
	void InitializeLog()
	{
#ifndef NDEBUG
		auto sink = std::make_shared<spdlog::sinks::msvc_sink_mt>();
#else
		auto path = logger::log_directory();
		if (!path) {
			util::report_and_fail("Failed to find standard logging directory"sv);
		}

		*path /= fmt::format("{}.log"sv, Plugin::NAME);
		auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true);
#endif

#ifndef NDEBUG
		const auto level = spdlog::level::trace;
#else
		const auto level = spdlog::level::info;
#endif

		auto log = std::make_shared<spdlog::logger>("global log"s, std::move(sink));
		log->set_level(level);
		log->flush_on(level);

		spdlog::set_default_logger(std::move(log));
		spdlog::set_pattern("%g(%#): [%^%l%$] %v"s);
	}
}

#ifdef SKYRIM_SUPPORT_AE
extern "C" DLLEXPORT constinit auto SKSEPlugin_Version = []() 
{
	SKSE::PluginVersionData v;

	v.PluginVersion(Plugin::VERSION);
	v.PluginName(Plugin::NAME);

	v.UsesAddressLibrary(true);
	//v.CompatibleVersions({ SKSE::RUNTIME_LATEST });

	return v;
}();
#endif

extern "C" DLLEXPORT bool SKSEAPI SKSEPlugin_Query(const SKSE::QueryInterface* a_skse, SKSE::PluginInfo* a_info)
{
	(void)a_info;

	InitializeLog();

	logger::info( "{} v{}"sv, Plugin::NAME, Plugin::VERSION.string() );

	if (a_skse->IsEditor()) 
	{
		logger::critical( "Not supported on editor."sv );
		return false;
	}

	const auto ver = a_skse->RuntimeVersion();
	if( ver < SKSE::RUNTIME_1_5_97 )
	{
		logger::critical( "Not supported on older version than 1.5.97."sv );
		return false;
	}

	a_info->name = Plugin::NAME.data();
	a_info->version = 1;

	return true;
}

struct RelocationHelper
{
	enum ID
	{
		DamageCorrection,
		DamageHook,
		ProjectileUpdate,
		ProjectileImpact,
		ProjectileVsActorGetImpactData
	};

	static REL::Relocation<uintptr_t> GetRelocationByID( ID a_id, REL::Version a_ver )
	{
		switch( a_id )
		{
		case DamageCorrection:
			return a_ver <= SKSE::RUNTIME_1_5_97 ? 
				REL::Relocation<uintptr_t>( REL::ID(37633), 0x14D ):
				REL::Relocation<uintptr_t>( REL::ID(38586), 0x189 );
		case DamageHook:
			return a_ver <= SKSE::RUNTIME_1_5_97 ? 
				REL::Relocation<uintptr_t>( REL::ID(42842), 0x649 ): // SkyrimSE.exe+0x743510
				REL::Relocation<uintptr_t>( REL::ID(44014), 0x62D ); // SkyrimSE.exe+0x770880
																	 //REL::Relocation<uintptr_t>( REL::ID(42833), 0x13E ):
																	 //REL::Relocation<uintptr_t>( REL::ID(44002), 0x13E );
		case ProjectileUpdate:
			return a_ver <= SKSE::RUNTIME_1_5_97 ? 
				REL::Relocation<uintptr_t>( REL::ID(42943), 0x410 ):
				REL::Relocation<uintptr_t>( REL::ID(44123), 0x407 );
		case ProjectileImpact:
			return a_ver <= SKSE::RUNTIME_1_5_97 ?
				REL::Relocation<uintptr_t>( REL::ID(43013), 0x3E3 ): // SkyrimSE.exe+0x7521f0+0x3E3
				REL::Relocation<uintptr_t>( REL::ID(44204), 0x3D4 ); // SkyrimSE.exe+0x780870+0x3D4
		case ProjectileVsActorGetImpactData:
			return a_ver <= SKSE::RUNTIME_1_5_97 ?
				REL::Relocation<uintptr_t>( REL::ID(36682), 0x354 ): // SkyrimSE.exe+0x5f87f0+0x354
				REL::Relocation<uintptr_t>( REL::ID(37690), 0x3AF ); // SkyrimSE.exe+0x620370+0x3AF
		}

		return REL::Relocation<uintptr_t>();
	}
};

#define MAXIMUM_LOCATION_COUNT 32
struct LocationalDamageSetting
{
	struct Effect
	{
		std::string effectID = "";
		int			effectChance = 0;
	};

	bool									enable = false;
	float									damageMult = 1.0f;
	bool									deflectProjectile = false;
	int										successChance = 100;
	float									successHPFactor = 0;
	bool									successHPFactorCap = true;
	std::string								message;
	std::string								sound;
	std::string								impactData;
	std::regex								regexp;
	std::vector<Effect>						effects;
	std::vector<std::vector<std::string>>	keywordInclude;
	std::vector<std::vector<std::string>>	keywordExclude;

	LocationalDamageSetting()
	{
		effects.resize( 5 );
	}
};

static std::vector<LocationalDamageSetting> g_LocationalDamageSettings;

static bool g_bDebugNotification = true;
static bool g_bPlayerNotification = true;
static bool g_bEffectChanceCap = true;
static bool g_bAmplifyEnchantment = true;
static float g_fNormalMult = 1.0f;
static float g_fDamageMult = 1.0f;
static float g_fHPFactor = 0.5f;
static float g_fLastHitDamage = 0;
static std::regex g_sExcludeRegexp;
static std::regex g_PlayerNodes;

static RE::BGSImpactData* g_ImpactOverride = NULL;

std::vector<std::string> split( const char* a_input, const char* a_regex ) 
{
	// passing -1 as the submatch index parameter performs splitting
	std::string in(a_input);
	std::regex re(a_regex);
	std::sregex_token_iterator
		first{in.begin(), in.end(), re, -1},
		last;
	return {first, last};
}

RE::NiNode* FindClosestHitNode( RE::NiNode* a_root, RE::NiPoint3* a_pos, float& a_dist, bool a_isPlayer )
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

std::regex g_armorKeywordRegex( "Armor.*" );
bool ActorHasKeywords( RE::Actor* a_actor, std::vector<std::string>& a_keywords )
{
	bool actorHasKeyword	= true;
	bool armorHasKeyword	= true;
	bool includeArmor		= false;

	for( auto& keyword : a_keywords )
	{
		if( !std::regex_match( keyword, g_armorKeywordRegex ) )
			actorHasKeyword = actorHasKeyword && a_actor->HasKeywordString( keyword );
		else
			includeArmor = true;
	}

	if( includeArmor )
	{
		armorHasKeyword = false;

		const auto inv = a_actor->GetInventory([](RE::TESBoundObject& a_object) {
			return a_object.IsArmor();
			});

		for( const auto& [item, invData] : inv ) 
		{
			const auto& [count, entry] = invData;
			if( count > 0 && entry->IsWorn() ) 
			{
				const auto armor = item->As<RE::TESObjectARMO>();
				if( armor ) 
				{
					bool pieceHasKeyword = true;
					for( auto& keyword : a_keywords )
					{
						if( std::regex_match( keyword, g_armorKeywordRegex ) )
						{
							pieceHasKeyword = pieceHasKeyword && armor->HasKeywordString( keyword );

							if( !pieceHasKeyword ) break;
						}
					}

					armorHasKeyword = armorHasKeyword || pieceHasKeyword;

					if( armorHasKeyword ) break;
				}
			}
		}
	}

	return actorHasKeyword && armorHasKeyword;
}

namespace LocationalDamage
{
	typedef void(*MagicCaster_CastPtr)( RE::MagicItem* a_spell, bool unk1, RE::TESObjectREFR* a_target, float a_magOverride, bool unk2, float unk3, void* unk4 );
	static MagicCaster_CastPtr fnCastMagic;

	bool RandomPercent( int percent )
	{
		return rand() % 100 <= percent;
	}

	// Amplify newly created active effects casted by an aggressor
	static void AmplifyActiveEffect( RE::Actor* a_actor, RE::ActorPtr a_aggressor, float a_magnitudeMult )
	{
		if( !a_actor )
			return;

		auto activeEffectList = a_actor->GetActiveEffectList();
		for( auto& activeEffect : *activeEffectList )
		{
			auto caster = activeEffect->caster ? activeEffect->caster.get() : NULL;
			if( activeEffect->elapsedSeconds == 0 && (caster == a_aggressor || a_aggressor == NULL) )
			{
				// Amplify magnitude if applicable
				if( activeEffect->effect->baseEffect->data.flags & RE::EffectSetting::EffectSettingData::Flag::kPowerAffectsMagnitude &&
					activeEffect->magnitude != 0 )
					activeEffect->magnitude *= a_magnitudeMult;
				else
					activeEffect->duration *= a_magnitudeMult;
			}
		}
	}

	static RE::EnchantmentItem* GetEquippedObjectEnchantment( RE::Actor* a_actor, bool a_leftHand )
	{
		if( a_actor->currentProcess && a_actor->currentProcess->middleHigh )
		{
			RE::InventoryEntryData* data = a_leftHand ?
				a_actor->currentProcess->middleHigh->leftHand :
				a_actor->currentProcess->middleHigh->rightHand;

			if( data )
			{
				auto object = data->object;
				if( object )
				{
					auto ench = object->As<RE::TESEnchantableForm>();
					if( ench && ench->formEnchanting )
						return ench->formEnchanting;
				}

				auto extraList = data->extraLists;
				if( extraList )
				{
					for( const auto& xList : *extraList )
					{
						const auto xEnch = xList->GetByType<RE::ExtraEnchantment>();
						if( xEnch && xEnch->enchantment )
							return xEnch->enchantment;
					}
				}
			}
		}

		return nullptr;
	}

	static float GetHPFactor( RE::Actor* a_actor, float a_factor, float a_damage, bool a_isCap )
	{
		auto targetMaxHP = a_actor->GetBaseActorValue( RE::ActorValue::kHealth );

		// Prevent divide by zero
		if( targetMaxHP == 0 )
			return 1;

		float factor = 1;
		if( a_factor > 0 )
			factor = a_damage / targetMaxHP / a_factor;
		else if( a_factor < 0 )
			factor = 1 / (a_damage / targetMaxHP / -a_factor);

		return a_isCap ? min( factor, 1 ) : factor;
	}

	struct ProjectileHitHook
	{
		static void ApplyLocationalDamage( RE::Projectile* a_projectile, RE::TESObjectREFR* a_target, RE::NiPoint3* a_location )
		{
			if( a_projectile && a_target && !a_target->IsDead() &&
				a_projectile->formType == RE::FormType::ProjectileArrow &&
				a_target->formType == RE::FormType::ActorCharacter )
			{
				RE::NiNode* hitPart = nullptr;
				RE::Projectile::ImpactData* impactData = nullptr;

				// Use impact result directly
				if( !a_projectile->impacts.empty() )
				{
					impactData = *a_projectile->impacts.begin();
					hitPart = impactData->damageRootNode;
				}

				// Search manually if no impact data
				if( !hitPart )
				{
					float hitDist;
					hitPart = FindClosestHitNode( a_target->Get3D()->AsNode(), a_location, hitDist, a_target->IsPlayerRef() );
				}

				if( hitPart )
				{
					RE::ActorPtr shooterActor = NULL;
					auto shooter = a_projectile->GetActorCause();
					if( shooter )
						shooterActor = shooter->actor.get();

					auto targetActor	= (RE::Actor*)a_target;

					bool locationHit = false;

					// Shield node has a strange name, need to check parent
					if( hitPart->parent && hitPart->parent->name == "SHIELD" )
						hitPart = hitPart->parent;

					for( auto& locationalSetting : g_LocationalDamageSettings )
					{
						if( locationalSetting.enable && 
							std::regex_match( hitPart->name.c_str(), locationalSetting.regexp ) )
						{
							// Default to true if no keyword include specified
							bool shouldApplyLocationalDamage = locationalSetting.keywordInclude.size() == 0;

							// Check if the actor actually has a keyword
							for( auto& keywords : locationalSetting.keywordInclude )
							{
								shouldApplyLocationalDamage = ActorHasKeywords( targetActor, keywords );
								if( shouldApplyLocationalDamage ) break;
							}

							// Check for keyword exclusion
							for( auto& keywords : locationalSetting.keywordExclude )
							{
								if( !shouldApplyLocationalDamage ) break;
								shouldApplyLocationalDamage = !ActorHasKeywords( targetActor, keywords );
							}

							// Final success chance check
							float successHPFactor = GetHPFactor( targetActor, locationalSetting.successHPFactor, g_fLastHitDamage, locationalSetting.successHPFactorCap );
							int finalSuccessChance = (int)(locationalSetting.successChance * successHPFactor);
							if( shouldApplyLocationalDamage && RandomPercent( finalSuccessChance ) )
							{
								locationHit = true;
								g_fDamageMult = locationalSetting.damageMult;
								g_fLastHitDamage *= g_fDamageMult;

								auto missileProjectile = a_projectile->As<RE::MissileProjectile>();
								if( missileProjectile && locationalSetting.deflectProjectile )
								{
									if( missileProjectile->impactResult == RE::ImpactResult::kStick )
										missileProjectile->impactResult = RE::ImpactResult::kBounce;
								}

								if( locationalSetting.impactData.size() > 0 )
								{
									auto impactOverride = RE::TESForm::LookupByEditorID( locationalSetting.impactData );
									if( impactOverride )
										g_ImpactOverride = impactOverride->As<RE::BGSImpactData>();
								}

								auto& message = locationalSetting.message;

								if( shooterActor )
								{
									// Amplify the power of any effects applied by the impact.(Enchantments and Perks)
									if( g_bAmplifyEnchantment )
										AmplifyActiveEffect( targetActor, shooterActor, locationalSetting.damageMult );

									// Player against enemy
									if( shooterActor->IsPlayerRef() )
									{
										if( message.size() > 0 )
											RE::DebugNotification( message.c_str(), NULL, false );

										if( locationalSetting.sound.size() > 0 )
											RE::PlaySound( locationalSetting.sound.c_str() );
									}

									// Enemy against player
									if( a_target->IsPlayerRef() && message.size() > 0 )
									{
										RE::DebugNotification( fmt::format( "{} by {}"sv, message.c_str(), shooterActor->GetDisplayFullName() ).c_str() );
									}
								}

								for( auto& effect : locationalSetting.effects )
								{
									auto hpFactor		= GetHPFactor( targetActor, g_fHPFactor, g_fLastHitDamage, g_bEffectChanceCap );
									int finalChance		= (int)(effect.effectChance * hpFactor);

									if( effect.effectID.length() > 0 && RandomPercent( finalChance ) )
									{
										auto magicItem = RE::TESForm::LookupByEditorID<RE::MagicItem>( effect.effectID );
										if( magicItem && (
											magicItem->formType == RE::FormType::Spell ||
											magicItem->formType == RE::FormType::Enchantment ||
											magicItem->formType == RE::FormType::AlchemyItem ) )
										{
											auto castingSource = RE::MagicSystem::CastingSource::kInstant;

											// Cast from target if all effects are PVM type to prevent PVM stacking bug from multiple sources
											bool shouldCastAtTarget = true;
											for( auto iter = magicItem->effects.begin(); iter != magicItem->effects.end(); ++iter )
											{
												if( (*iter)->baseEffect->GetArchetype() != RE::EffectSetting::Archetype::kPeakValueModifier )
												{
													shouldCastAtTarget = false;
													break;
												}
											}

											// Cast at target when it should or when the firing actor does not exist.(Fired from an activator)
											if( shouldCastAtTarget || !shooterActor )
												a_target->GetMagicCaster( castingSource )->CastSpellImmediate( magicItem, false, a_target, 1.0f, false, 0, NULL );
											else
												shooterActor->GetMagicCaster( castingSource )->CastSpellImmediate( magicItem, false, a_target, 1.0f, false, 0, NULL );
										}
									}
								}

								break;
							}
						}
					}

					if( g_bDebugNotification )
					{
						if( shooterActor && shooterActor->IsPlayerRef() )
							RE::DebugNotification( fmt::format( "Arrow hit {}"sv, hitPart->name.c_str() ).c_str() );

						if( a_target->IsPlayerRef() )
							RE::ConsoleLog::GetSingleton()->Print( "Enemy hit your '%s'", hitPart->name.c_str() );
					}
				}
			}
		}

		static uint64_t thunk( RE::Projectile* a_projectile, RE::TESObjectREFR* a_target, RE::NiPoint3* a_location, RE::NiPoint3* unk1, uint32_t unk2, uint8_t unk3 )
		{
			//ApplyLocationalDamage( a_projectile, a_target, a_location );

			return func( a_projectile, a_target, a_location, unk1, unk2, unk3 );
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct ProjectileUpdateHook
	{
		static uint64_t thunk( RE::Projectile* a_projectile, float a_delta )
		{
			return func( a_projectile, a_delta );
		}

		static inline REL::Relocation<decltype(thunk)> func;
		static const size_t size = 0xB5;
	};

	struct ProjectileGetImpackHookActor
	{
		// GetImpactData( a_dataset, a_material )
		static RE::BGSImpactData* thunk( RE::BGSImpactDataSet* a_dataset, RE::BGSMaterialType* a_material )
		{
			auto impact = g_ImpactOverride;
			g_ImpactOverride = NULL;

			if( impact )
				return impact;

			return func( a_dataset, a_material );
		}
		static inline REL::Relocation<decltype(thunk)> func;

		static bool Check( REL::Version a_ver )
		{
			auto hook = RelocationHelper::GetRelocationByID( RelocationHelper::ID::ProjectileVsActorGetImpactData, a_ver );
			if( *(uint8_t*)hook.address() != 0xE8 )
			{
				logger::critical( "Opcode at injection site not matched. Aborting..." );
				return false;
			}

			return true;
		}

		// SkyrimSE.exe+062071F -> 37690+0x3AF
		static void Install( REL::Version a_ver )
		{
			auto hook = RelocationHelper::GetRelocationByID( RelocationHelper::ID::ProjectileVsActorGetImpactData, a_ver );
			stl::write_thunk_call<ProjectileGetImpackHookActor>( hook.address() );
		}
	};

	struct ProjectileImpactHook
	{
		static bool thunk( RE::Projectile* a_projectile )
		{
			// Use impact result directly
			if( !a_projectile->impacts.empty() )
			{
				auto impactData = *a_projectile->impacts.begin();
				if( impactData->collidee )
				{
					auto targetPtr = impactData->collidee.get();
					auto impactLocation = &impactData->desiredTargetLoc;

					if( targetPtr )
						ProjectileHitHook::ApplyLocationalDamage( a_projectile, targetPtr->AsReference(), impactLocation );
				}
			}

			return a_projectile->Unk_B8();
		}
		static inline REL::Relocation<decltype(thunk)> func;

		static bool Check( REL::Version a_ver )
		{
			auto hook = RelocationHelper::GetRelocationByID( RelocationHelper::ID::ProjectileImpact, a_ver );
			if( *(uint16_t*)hook.address() != 0x90FF )
			{
				logger::critical( "Opcode at injection site not matched. Aborting..." );
				return false;
			}

			return true;
		}

		static void Install( REL::Version a_ver )
		{
			// 44204+0x3EC 0x780870
			auto hook = RelocationHelper::GetRelocationByID( RelocationHelper::ID::ProjectileImpact, a_ver );
			REL::safe_fill( hook.address(), 0x90, 6 );
			stl::write_thunk_call<ProjectileImpactHook>( hook.address() );
		}
	};

	struct DamageHook
	{
		// lea r11,[rsp+000000C0]
		struct Patch : Xbyak::CodeGenerator
		{
			Patch( size_t a_retAddr, REL::Version a_ver )
			{
				Xbyak::Label callLbl;
				Xbyak::Label retLbl;

				// HitData is at register r13
				mov( rcx, r13 );
				call( ptr[rip + callLbl] );

				lea( r11, ptr[rsp+(a_ver <= SKSE::RUNTIME_1_5_97 ? 0xD0 : 0xC0)] );
				jmp( ptr[rip + retLbl] );

				L( callLbl );
				dq( (size_t)&thunk );

				L( retLbl );
				dq( a_retAddr );

				ready();
			}
		};

		static void thunk( RE::HitData* a_hitData )
		{
			// Save damage for effect chance calculation
			g_fLastHitDamage = a_hitData->totalDamage;
		}
		static inline REL::Relocation<decltype(thunk)> func;

		static bool Check( REL::Version a_ver )
		{
			auto hook = RelocationHelper::GetRelocationByID( RelocationHelper::ID::DamageHook, a_ver );
			if( *(uint8_t*)hook.address() != 0x4C )
			{
				logger::critical( "Opcode at injection site not matched. Aborting..." );
				return false;
			}

			return true;
		}

		static void Install( REL::Version a_ver )
		{
			auto hook = RelocationHelper::GetRelocationByID( RelocationHelper::ID::DamageHook, a_ver );
			Patch patch( hook.address() + 8, a_ver );
			REL::safe_fill( hook.address(), 0x90, 8 );
			stl::write_patch_branch<DamageHook>( hook.address(), patch );
		}
	};

	struct DamageCorrection
	{
		struct DamageCalculationPatch : public Xbyak::CodeGenerator
		{
			DamageCalculationPatch( size_t a_retAddr, REL::Version a_ver )
			{
				Xbyak::Label callLbl;
				Xbyak::Label retLbl;

				// HitData is at register r13, forward it(r15 in SE)
				mov( rcx, a_ver <= SKSE::RUNTIME_1_5_97 ? r15 : r13 );
				call( ptr[rip + callLbl] );

				jmp( ptr[rip + retLbl] );

				L( callLbl );
				dq( (size_t)&thunk );

				L( retLbl );
				dq( a_retAddr );

				ready();
			}
		};

		// Hook an unknown function for damage calculation
		// Should be safe as it's not using any argument
		static void* thunk( RE::HitData* a_hitData )
		{
			a_hitData->totalDamage *= g_fDamageMult;
			g_fDamageMult = 1.0f;

			return func();
		}
		static inline REL::Relocation<void*()> func;

		static bool Check( REL::Version a_ver )
		{
			auto hook = RelocationHelper::GetRelocationByID( RelocationHelper::ID::DamageCorrection, a_ver );
			if( *(uint8_t*)hook.address() != 0xE8 )
			{
				logger::critical( "Opcode at injection site not matched. Aborting..." );
				return false;
			}

			return true;
		}

		static void Install( REL::Version a_ver )
		{
			auto hook = RelocationHelper::GetRelocationByID( RelocationHelper::ID::DamageCorrection, a_ver );
			DamageCalculationPatch patch( hook.address() + 5, a_ver );
			stl::write_patch_branch<DamageCorrection>( hook.address(), patch );
		}
	};

	struct ApplySpellHook
	{
		static bool thunk( RE::MagicCaster* a_caster, float a_magOverride, uint32_t* unk2, uint32_t unk3, bool unk4_bool1, bool unk5_bool2 )
		{
			return func( a_caster, a_magOverride, unk2, unk3, unk4_bool1, unk5_bool2 );
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct CastSpellHook
	{
		static uint64_t thunk( RE::MagicCaster* a_caster, RE::MagicItem* a_spell, bool unk1_bool1, RE::TESObjectREFR* a_target, float a_magOverride, bool unk2_bool2, float unk3, RE::TESObjectREFR* a_blameTarget )
		{
			if( a_target && a_target->IsPlayerRef() )
			{
				auto name = a_spell->GetName();
				auto formID = a_spell->formID;
				RE::ConsoleLog::GetSingleton()->Print( fmt::format( "Spell {} ({:08X}) casted on player"sv, name, formID ).c_str() );
			}
			auto ret = func( a_caster, a_spell, unk1_bool1, a_target, a_magOverride, unk2_bool2, unk3, a_blameTarget );
			return ret;
		}
		static inline REL::Relocation<decltype(thunk)> func;
	}; 

	struct CheckResistanceHook
	{
		static void thunk( RE::MagicItem* a_magicItem, RE::EffectItem* a_effect, RE::TESBoundObject* a_object )
		{
			return func( a_magicItem, a_effect, a_object );
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	inline bool Install( REL::Version a_ver )
	{
		//REL::Relocation<uintptr_t> projectileUpdateHookLoc{ REL::ID(44028), 0x79 };
		//stl::write_thunk_call<ProjectileUpdateHook>( projectileUpdateHookLoc.address() );

		if( !DamageHook::Check( a_ver ) )					return false;
		if( !DamageCorrection::Check( a_ver ) )				return false;
		if( !ProjectileGetImpackHookActor::Check( a_ver ) ) return false;
		if( !ProjectileImpactHook::Check( a_ver ) )			return false;

		DamageHook::Install( a_ver );
		DamageCorrection::Install( a_ver );
		ProjectileGetImpackHookActor::Install( a_ver );
		ProjectileImpactHook::Install( a_ver );

		//ProjectileImpactHook::ProjectileImpactPatch patchImpact( projectileImpactHookLoc.address() + 6 );
		//stl::write_patch_branch<ProjectileImpactHook>( projectileImpactHookLoc.address(), patchImpact );
		//stl::write_vfunc<RE::Projectile,ProjectileUpdateHook>();
		//ProjectileUpdateHook::func = projectileVtbl.address() + sizeof(uintptr_t)*0xB6;
		//projectileVtbl.write_vfunc( 0xB6, ProjectileUpdateHook::thunk );

		// MagicCaster::Unk01() = Cast(...)
		// MagicCaster::Unk09() = ApplyEffect()?
		//REL::Relocation<uintptr_t> spellHook{ REL::ID(34404), 0xAF };
		//stl::write_thunk_call<ApplySpellHook>( spellHook.address() );

		// For debugging spell cast
		/*REL::Relocation<uintptr_t> castMagic{ REL::ID(34404) };
		REL::Relocation<uintptr_t> casterVtbl{ RE::VTABLE_MagicCaster[0] };
		REL::Relocation<uintptr_t> actorCasterVtbl{ RE::VTABLE_ActorMagicCaster[0] };
		REL::Relocation<uintptr_t> nactorCasterVtbl{ RE::VTABLE_ExtraMagicCaster[1] };
		casterVtbl.write_vfunc( 1, CastSpellHook::thunk );
		actorCasterVtbl.write_vfunc( 1, CastSpellHook::thunk );
		nactorCasterVtbl.write_vfunc( 1, CastSpellHook::thunk );
		CastSpellHook::func = castMagic.address();*/

		logger::info( "Archery Locational Damage loaded." );

		return true;
	}
};

void SetLocationEffect( LocationalDamageSetting& a_setting, int a_index, std::string a_effect )
{
	if( a_setting.effects.size() < a_index + 1 )
		a_setting.effects.resize( a_index + 1 );

	a_setting.effects[ a_index ].effectID = a_effect;
}

void SetLocationChance( LocationalDamageSetting& a_setting, int a_index, int a_chance )
{
	if( a_setting.effects.size() < a_index + 1 )
		a_setting.effects.resize( a_index + 1 );

	a_setting.effects[ a_index ].effectChance = a_chance;
}

extern "C" DLLEXPORT bool SKSEAPI SKSEPlugin_Load( const SKSE::LoadInterface* a_skse )
{
	InitializeLog();

	SKSE::Init( a_skse );

	CSimpleIniA iniFile;
	iniFile.SetUnicode();
	iniFile.SetMultiKey();
	iniFile.LoadFile( L"Data/SKSE/Plugins/ArcheryLocationalDamage.ini" );

	g_bDebugNotification	= iniFile.GetBoolValue( "Settings", "DebugNotification", false );
	g_bPlayerNotification	= iniFile.GetBoolValue( "Settings", "PlayerHitNotification", false );
	g_sExcludeRegexp		= iniFile.GetValue( "Settings", "LocationExclude", "" );
	g_PlayerNodes			= iniFile.GetValue( "Settings", "PlayerNodeInclude", ".*" );
	g_fHPFactor				= (float)iniFile.GetDoubleValue( "Settings", "HPFactor", 50 ) / 100.0f;
	g_bEffectChanceCap		= iniFile.GetBoolValue( "Settings", "HPFactorCap", true );
	g_bAmplifyEnchantment	= iniFile.GetBoolValue( "Settings", "AmplifyEnchantment", true );

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
			setting.deflectProjectile	= iniFile.GetBoolValue( sectionIter.pItem, "Deflect", false );
			setting.impactData			= iniFile.GetValue( sectionIter.pItem, "ImpactData", "" );
			setting.message				= iniFile.GetValue( sectionIter.pItem, "Message", "" );
			setting.sound				= iniFile.GetValue( sectionIter.pItem, "HitSound", "" );
			std::string regexp			= iniFile.GetValue( sectionIter.pItem, "Regexp", "" );

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
				{
					auto keywords = split( iter->second, "[\\s,]+" );
					setting.keywordInclude.push_back( keywords );
				}
				else if( currentKey == "KeywordExclude" && *iter->second != 0 )
				{
					auto keywords = split( iter->second, "[\\s,]+" );
					setting.keywordExclude.push_back( keywords );
				}
			}

			g_LocationalDamageSettings.push_back( setting );
		}
	}

	return LocationalDamage::Install( a_skse->RuntimeVersion() );
}

/*
* v 1.6.353
skyrimse.exe+0x772930 => ProjectileUpdate 44028 
skyrimse.exe+0x77a830 => ProjectileUpdate 44123					-> +0x407 : Projectile::Unk_B5 virtual function
skyrimse.exe+0x7818e0 => ProjectileHit 44213
skyrimse.exe+0xc7ff30 => AttachChild 70289
SkyrimSE.exe+0x647410 => Restore/Damage AV
SkyrimSE.exe+0x64c850 => PhysicalDamage Load 38586 (HitData@rdx)-> +0x189
SkyrimSE.exe+0x770880 => Calculate Physical Damage 44014
SkyrimSE.exe+0x76fee0 => For hook calculate damage call 44002	-> +0x13E
SkyrimSE.exe+0x5676d0 => Apply Spell Effect 34410 

SkyrimSE.exe+6FAA20 - 48 89 5C 24 08        - mov [rsp+08],rbx  => Actor::AttachArrow
SkyrimSE.exe+64E5E0 - 48 89 5C 24 08        - mov [rsp+08],rbx  => Actor::HandleHealthDamage
SkyrimSE.exe+C7FF30 - 48 85 D2              - test rdx,rdx		=> NiNode::Attach
SkyrimSE.exe+64CCF7 - E9 123393FF           - jmp 13FF7000E		=> HandleDamage
SkyrimSE.exe+64C850 - 48 8B C4              - mov rax,rsp		=> CalculateDamage
SkyrimSE.exe+2FF480 - 48 8B C4              - mov rax,rsp		=> ConsoleScriptCompile
SkyrimSE.exe+2FB5D0 - 4C 89 44 24 18        - mov [rsp+18],r8	=> ExecuteConsoleCommand

SkyrimSE.exe+64CAE0 - F3 45 0F10 55 50      - movss xmm10,[r13+50]	=> r13 = HitData*

SkyrimSE.exe+770EAD - 4C 8D 9C 24 C0000000  - lea r11,[rsp+000000C0] => Hook r13 for final damage after defense

SkyrimSE.exe+2FF610 - 4C 89 4C 24 20        - mov [rsp+20],r9	=> Cast Spell Console
SkyrimSE.exe+88A420 - 4C 89 44 24 18        - mov [rsp+18],r8	=> ConsoleLog

SkyrimSE.exe+5676D0 - 48 8B C4              - mov rax,rsp		=> CastSpell or ApplyMagicEffect?
SkyrimSE.exe+566F30 - 48 89 5C 24 10        - mov [rsp+10],rbx	=> CastSpell
SkyrimSE.exe+56c800												=> ApplyMagicEffect?
SkyrimSE.exe+566FDF - E8 EC060000           - call SkyrimSE.exe+5676D0

SkyrimSE.exe+64C850 - 48 8B C4              - mov rax,rsp			=> Apply Physical damage?

SkyrimSE.exe+772930 
SkyrimSE.exe+7729A9 													Call projectile update
SkyrimSE.exe+774FB3 - 89 8B D8010000        - mov [rbx+000001D8],ecx	Write impact result 1
SkyrimSE.exe+75E63C - 89 86 D8010000        - mov [rsi+000001D8],eax	Material impact result before stick or bounce
SkyrimSE.exe+780BAB - 48 8B 36              - mov r13, rax				Impact result return address

SkyrimSE.exe+780834 - 48 89 BE 98000000     - mov [rsi+00000098],rdi	Impact created
SkyrimSE.exe+780343 - E8 187A4A00           - call SkyrimSE.exe+C27D60	Impact test?
SkyrimSE.exe+780676 - E8 E5E9FFFF           - call SkyrimSE.exe+77F060	Do Impact?(return BGSMaterialType*)
SkyrimSE.exe+77F107 - E8 24B3B5FF           - call SkyrimSE.exe+2DA430	Do Impact?(return BGSMaterialType*) : Non Actor Only

SkyrimSE.exe+780BA4 - 41 FF 92 E8050000     - call qword ptr [r10+000005E8] Actor Get Impact Material?
SkyrimSE.exe+77FC3A - FF 90 F0050000        - call qword ptr [rax+000005F0]
SkyrimSE.exe+75E411 - E8 1AC0B7FF           - call SkyrimSE.exe+2DA430	

SkyrimSE.exe+2DA430 - 48 89 5C 24 08        - mov [rsp+08],rbx			Get Object material

SkyrimSE.exe+62071F - E8 9C5CCBFF           - call SkyrimSE.exe+2D63C0	Actor call get impact data

SkyrimSE.exe+620DA7 - FF 50 20              - call qword ptr [rax+20]	rax+0x20 = BGSImpactData->GetModel()
SkyrimSE.exe+620DB9 - FF 50 20              - call qword ptr [rax+20]
SkyrimSE.exe+620E79 - FF 50 20              - call qword ptr [rax+20]

v 1.5.97
SkyrimSE.exe+0x626400 => PhysicalDamage Load 37633				-> +0x14D
SkyrimSE.exe+0x742C00 => For hook calculate damage call 42833	-> +0x13E
skyrimse.exe+0x74C710 => ProjectileUpdate 42943					-> +0x410
*/
