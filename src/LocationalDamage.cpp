#include "LocationalDamage.h"
#include "Utils.h"
#include "Hooks.h"
#include "Settings.h"
#include "FloatingDamage.h"

extern std::vector<LocationalDamageSetting> g_LocationalDamageSettings;

extern bool g_bDebugNotification;
extern bool g_bPlayerNotification;
extern bool g_bPlayerHitSound;
extern bool g_bEffectChanceCap;
extern bool g_bAmplifyEnchantment;
extern bool g_bEnableFloatingText;
extern bool g_bHitEffectNotification;
extern bool g_bNPCFloatingNotification;
extern float g_fNormalMult;
extern float g_fDamageMult;
extern float g_fHPFactor;
extern float g_fLastHitDamage;
extern float g_fFloatingOffsetX;
extern float g_fFloatingOffsetY;
extern long g_nNotificationMode;
extern std::regex g_sExcludeRegexp;
extern std::regex g_PlayerNodes;

float g_fLastHitDamage = 0;
float g_fDamageMult = 1.0f;
std::vector<HitDataOverride> g_HitDataOverride;
RE::BGSImpactData* g_ImpactOverride = NULL;
unsigned long long g_PerformanceFrequency = 0;

static std::mutex mutex;
void LocationalDamage::ApplyLocationalDamage( RE::Projectile* a_projectile, RE::TESObjectREFR* a_target, RE::NiPoint3* a_location )
{
	if( a_projectile && a_target && !a_target->IsDead() &&
		a_projectile->formType == RE::FormType::ProjectileArrow &&
		a_target->formType == RE::FormType::ActorCharacter )
	{
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
		if( !hitPart )
		{
			float hitDist;
			hitPart = FindClosestHitNode( a_target->Get3D()->AsNode(), a_location, hitDist, a_target->IsPlayerRef() );
		}
		
		if( hitPart )
		{
			auto targetActor		= (RE::Actor*)a_target;
			bool locationHit		= false;
			bool shooterIsPlayer	= shooterActor && shooterActor->IsPlayerRef();
			bool targetIsPlayer		= targetActor->IsPlayerRef();

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
						std::lock_guard<std::mutex> lock( mutex );

						HitDataOverride hitDataOverride;
						hitDataOverride.aggressor	= shooterActor;
						hitDataOverride.target		= a_target;
						hitDataOverride.location	= *a_location;
						hitDataOverride.damageMult	= locationalSetting.damageMult;

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

						bool isFPS = false;
						auto camera = RE::PlayerCamera::GetSingleton();
						if( camera && targetIsPlayer )
							isFPS = camera->currentState->id == RE::CameraState::kFirstPerson;

						// Flush floating text buffer
						float alpha = (shooterIsPlayer || targetIsPlayer) ? 100.0f : 50.0f;
						FloatingDamage::Flush( a_target, isFPS ? NULL : a_location, g_fFloatingOffsetX, g_fFloatingOffsetY, alpha );

						g_HitDataOverride.push_back( hitDataOverride );

						break;
					}
				}
			}

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
	Settings::Load();
	FloatingDamage::Initialize( a_ver );

	QueryPerformanceFrequency( (LARGE_INTEGER*)&g_PerformanceFrequency );

	return Hooks::Install( a_ver );
}