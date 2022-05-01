#include "LocationalDamage.h"
#include "Utils.h"
#include "Hooks.h"
#include "Settings.h"
#include "FloatingDamage.h"

extern std::vector<Settings::Location> g_LocationalDamageSettings;

extern bool g_bDebugNotification;
extern bool g_bPlayerNotification;
extern bool g_bPlayerHitSound;
extern bool g_bEffectChanceCap;
extern bool g_bAmplifyEnchantment;
extern bool g_bEnableFloatingText;
extern bool g_bHitEffectNotification;
extern bool g_bNPCFloatingNotification;
extern bool g_bIgnoreHitboxCheck;
extern bool g_bEnableDifficultyBonus;
extern bool g_bEnableLocationMultiplier;
extern float g_fShotDifficultyTimeFactor;
extern float g_fShotDifficultyDistFactor;
extern float g_fShotDifficultyMoveFactor;
extern float g_fShotDifficultyMax;
extern float g_fNormalMult;
extern float g_fDamageMult;
extern float g_fHPFactor;
extern float g_fLastHitDamage;
extern float g_fFloatingOffsetX;
extern float g_fFloatingOffsetY;
extern long g_nNotificationMode;
extern long g_nEXPNotificationMode;
extern std::regex g_sExcludeRegexp;
extern std::regex g_PlayerNodes;

float g_fLastHitDamage = 0;
float g_fDamageMult = 1.0f;
std::vector<HitDataOverride> g_HitDataOverride;
RE::BGSImpactData* g_ImpactOverride = NULL;
unsigned long long g_PerformanceFrequency = 0;

std::unordered_map<RE::FormID,std::string> formEditorIDMap;
void LocationalDamage::InitFormEditorIDMap()
{
	auto start = std::chrono::system_clock::now();
	const auto& [map, lock] = RE::TESForm::GetAllFormsByEditorID();
	const RE::BSReadWriteLock l{ lock };
	if( map ) 
	{
		for( auto& [editorID, form] : *map )
			formEditorIDMap.emplace( form->GetFormID(), editorID );
	}
	auto end = std::chrono::system_clock::now();
	std::chrono::duration<float> duration = end - start;
	logger::info( "Form editor ID loaded in {:0.2f} seconds", duration.count() );
}

static std::mutex mutex;
void LocationalDamage::ApplyLocationalDamage( RE::Projectile* a_projectile, RE::TESObjectREFR* a_target, RE::NiPoint3* a_location )
{
	if( a_projectile && a_target && !a_target->IsDead() &&
		a_projectile->formType == RE::FormType::ProjectileArrow &&
		a_target->formType == RE::FormType::ActorCharacter )
	{
		// Skip if projectile has no life remaining (eg. during VATS hitscan)
		// Not really accurate because this member variable seems to be the projectile's lifetime instead of remaining life.
		// lifeRemaining can be zero when shot at point-blank range.
		/*if( a_projectile->lifeRemaining == 0 )
			return;*/

		// VATS hitscan set bit 17 to 1
		if( a_projectile->flags & (1 << 17) )
			return;

		RE::Actor* shooterActor = nullptr;
		auto shooter = a_projectile->shooter.get();
		if( shooter )
			shooterActor = shooter->As<RE::Actor>();

		// Skip if the target is not vaild
		if( shooterActor && !shooterActor->CheckValidTarget( *a_target ) )
			return;

		RE::NiNode* hitPart = nullptr;
		RE::Projectile::ImpactData* impactData = nullptr;

		// Use impact result directly
		if( !a_projectile->impacts.empty() )
		{
			impactData = *a_projectile->impacts.begin();
			hitPart = impactData->damageRootNode;
		}

		// Search manually if no impact data
		if( !hitPart || g_bIgnoreHitboxCheck )
		{
			float hitDist;
			hitPart = FindClosestHitNode( a_target->Get3D()->AsNode(), a_location, hitDist, a_target->IsPlayerRef(), g_bIgnoreHitboxCheck );
		}
		
		if( hitPart )
		{
			// Floating damage UI invocation cannot be done simutaneously from multiple threads.
			std::lock_guard<std::mutex> lock( mutex );

			HitDataOverride hitDataOverride;
			auto targetActor		= (RE::Actor*)a_target;
			bool locationHit		= false;
			bool shooterIsPlayer	= shooterActor && shooterActor->IsPlayerRef();
			bool targetIsPlayer		= targetActor->IsPlayerRef();
			float difficulty		= 1;

			// Shield node has a strange name, need to check parent
			if( hitPart->parent && hitPart->parent->name == "SHIELD" )
				hitPart = hitPart->parent;

			for( auto& locationalSetting : g_LocationalDamageSettings )
			{
				if( locationalSetting.enable &&
					std::regex_match( hitPart->name.c_str(), locationalSetting.regexp ) )
				{
					// Success chance check
					int finalSuccessChance = 100;

					// Compute HP factor
					if( locationalSetting.successHPFactor != 0 )
					{
						float successHPFactor = GetHPFactor( targetActor, locationalSetting.successHPFactor, g_fLastHitDamage, locationalSetting.successHPFactorCap );
						finalSuccessChance = (int)(locationalSetting.successChance * successHPFactor);
					}
					
					if( RandomPercent( finalSuccessChance ) &&
						locationalSetting.filter.IsActorVaild( targetActor, &formEditorIDMap ) )
					{
						hitDataOverride.aggressor	= shooterActor;
						hitDataOverride.target		= a_target;
						hitDataOverride.location	= *a_location;
						hitDataOverride.damageMult	*= locationalSetting.damageMult;

						difficulty = max( difficulty, locationalSetting.difficulty );

						// Set expiration time to prevent build up of unprocessed hits (1/10 sec)
						QueryPerformanceCounter( (LARGE_INTEGER*)&hitDataOverride.expireTimestamp );
						hitDataOverride.expireTimestamp += g_PerformanceFrequency / 10;

						locationHit = true;
						g_fDamageMult = locationalSetting.damageMult;
						g_fLastHitDamage *= g_fDamageMult;

						auto missileProjectile = a_projectile->As<RE::MissileProjectile>();
						if( missileProjectile && locationalSetting.deflectProjectile )
						{
							if( missileProjectile->impactResult == RE::ImpactResult::kStick )
							{
								missileProjectile->impactResult = RE::ImpactResult::kBounce;

								if( impactData )
									impactData->impactResult = RE::ImpactResult::kBounce;
							}
						}

						if( locationalSetting.impactData.size() > 0 )
						{
							auto impactOverride = RE::TESForm::LookupByEditorID( locationalSetting.impactData );
							if( impactOverride )
							{
								g_ImpactOverride = impactOverride->As<RE::BGSImpactData>();
								hitDataOverride.impactData = g_ImpactOverride;
							}
						}

						auto message			= &locationalSetting.message;
						auto messageFloating	= &locationalSetting.messageFloating;

						if( shooterActor )
						{
							// Amplify the power of any effects applied by the impact.(Enchantments and Perks)
							if( g_bAmplifyEnchantment )
								AmplifyActiveEffect( targetActor, shooterActor, locationalSetting.damageMult );

							// Only player sound when the player is involved
							if( ( shooterIsPlayer || targetIsPlayer ) && locationalSetting.sound.size() > 0 )
							{
								if( shooterIsPlayer || g_bPlayerHitSound )
									RE::PlaySound( locationalSetting.sound.c_str() );
							}

							// Notification display
							if( shooterIsPlayer || targetIsPlayer || g_bNPCFloatingNotification )
							{
								if( message->size() > 0 || messageFloating->size() > 0 )
								{
									// Use normal message for floating text if floating message is not defined
									if( messageFloating->size() == 0 )
										messageFloating = message;

									bool shouldShowNotification = g_nNotificationMode == NotificationMode::Both || g_nNotificationMode == NotificationMode::Screen;

									if( g_nNotificationMode == NotificationMode::Both || g_nNotificationMode == NotificationMode::Floating )
									{
										if( ( targetIsPlayer && g_bPlayerNotification ) ||								// Floating notification for player
											( !targetIsPlayer && !shooterIsPlayer && g_bNPCFloatingNotification ) ||	// Floating notification for NPC
											shooterIsPlayer )
										{
											// Switch to screen notification if failed
											if( !FloatingDamage::CreateFloatingText( 
												messageFloating->c_str(), 
												targetIsPlayer ? locationalSetting.floatingColorSelf : locationalSetting.floatingColorEnemy, 
												locationalSetting.floatingSize ) )
												shouldShowNotification = true;
										}
									}

									// On screen notification is reserved for player only
									if( shouldShowNotification && message->size() > 0 )
									{
										if( targetIsPlayer && g_bPlayerNotification )
											RE::DebugNotification( fmt::format( "{} by {}"sv, message->c_str(), shooterActor->GetDisplayFullName() ).c_str() );
										else if( shooterIsPlayer )
											RE::DebugNotification( message->c_str(), NULL, false );
									}
								}
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

									if( g_bHitEffectNotification )
									{
										if( (targetIsPlayer || shooterIsPlayer) ||
											g_bNPCFloatingNotification )
											FloatingDamage::CreateFloatingText( 
												magicItem->GetName(), 
												targetIsPlayer ? locationalSetting.floatingColorSelf : locationalSetting.floatingColorEnemy, 
												locationalSetting.floatingSize );
									}
								}
							}
						}

						// Stop processing further locations if not required to do so.
						if( !locationalSetting.shouldContinue )
							break;
					}
				}
			}

			float expMult = 1;
			if( hitDataOverride.aggressor )
			{
				g_HitDataOverride.push_back( hitDataOverride );

				if( shooterIsPlayer )
				{
					// Reward additional EXP if enabled
					if( g_bEnableLocationMultiplier && difficulty > 1 )
						expMult += difficulty - 1;
				}
			}

			if( shooterIsPlayer )
			{
				// Reward shot difficulty EXP if enabled
				if( g_bEnableDifficultyBonus )
				{
					float shotDifficulty = CalculateShotDifficulty( a_projectile, targetActor, g_fShotDifficultyTimeFactor, g_fShotDifficultyDistFactor, g_fShotDifficultyMoveFactor );
					expMult *= shotDifficulty;
				}

				if( g_nEXPNotificationMode != NotificationMode::None && ( g_bEnableDifficultyBonus || g_bEnableLocationMultiplier ) )
				{
					auto reportStr = fmt::format( "Shot difficulty: {:0.1f}", expMult );
					bool shouldShowNotification = 
						g_nEXPNotificationMode == NotificationMode::Both || g_nEXPNotificationMode == NotificationMode::Screen;
					if( g_nEXPNotificationMode == NotificationMode::Both || g_nEXPNotificationMode == NotificationMode::Floating )
					{
						if( !FloatingDamage::CreateFloatingText( reportStr.c_str(), 0xFF8000, 24) )
							shouldShowNotification = true;
					}

					if( shouldShowNotification )
						RE::DebugNotification( reportStr.c_str(), NULL, false );
				}

				// Clamp to limit
				expMult = min( g_fShotDifficultyMax, expMult );

				if( expMult > 1 )
				{
					auto player = static_cast<RE::PlayerCharacter*>( shooterActor );
					auto weapon = a_projectile->weaponSource;
					if( weapon )
					{
						// Skyrim give EXP equals to weapon base attack damage by default (1x)
						auto baseEXP = weapon->GetAttackDamage();
						// Only give the amount of EXP over 1x since the game is already rewarded the player at this point
						player->AddSkillExperience( weapon->weaponData.skill.get(), baseEXP * (expMult - 1) );
					}
				}
			}

			bool isFPS = false;
			auto camera = RE::PlayerCamera::GetSingleton();
			if( camera && targetIsPlayer )
				isFPS = camera->currentState->id == RE::CameraState::kFirstPerson;

			// Flush floating text buffer
			float alpha = (shooterIsPlayer || targetIsPlayer) ? 100.0f : 50.0f;
			FloatingDamage::Flush( a_target, isFPS ? NULL : a_location, g_fFloatingOffsetX, g_fFloatingOffsetY, alpha, shooterIsPlayer );

			if( g_bDebugNotification )
			{
				if( shooterIsPlayer )
					RE::DebugNotification( fmt::format( "Arrow hits {}"sv, hitPart->name.c_str() ).c_str() );

				if( a_target->IsPlayerRef() )
					RE::ConsoleLog::GetSingleton()->Print( "Enemy hits your '%s'", hitPart->name.c_str() );
			}
		}
	}
}

bool LocationalDamage::Install( REL::Version a_ver )
{
#ifdef _DEBUG
	//while( !IsDebuggerPresent() );
#endif

	Settings::Load();
	FloatingDamage::Initialize( a_ver );

	QueryPerformanceFrequency( (LARGE_INTEGER*)&g_PerformanceFrequency );

	return Hooks::Install( a_ver );
}
