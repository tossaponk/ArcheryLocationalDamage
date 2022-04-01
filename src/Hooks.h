#pragma once

#include "Offsets.h"
#include "LocationalDamage.h"

extern float g_fLastHitDamage;
extern float g_fDamageMult;

extern RE::BGSImpactData* g_ImpactOverride;
extern std::vector<HitDataOverride> g_HitDataOverride;
extern std::mutex g_handleProjectileAttackMutex;

namespace Hooks
{
	static RE::BGSImpactData* GetSparkImpact()
	{
		auto impact = RE::TESForm::LookupByEditorID( "WPNArrowVsMetalImpact" );
		return impact->As<RE::BGSImpactData>();
	}

	struct ProjectileUpdateHook
	{
		static uint64_t thunk( RE::Projectile* a_projectile, float a_delta )
		{
			return func( a_projectile, a_delta );
		}

		static inline REL::Relocation<decltype(thunk)> func;
		static const size_t size = 0xB5;
	};

	// For testing
	struct GetImpactDataHook
	{
		static RE::BGSImpactData* thunk( RE::BGSImpactDataSet* a_dataset, RE::BGSMaterialType* a_material )
		{
			_CRT_UNUSED(a_dataset);
			_CRT_UNUSED(a_material);

			return GetSparkImpact();
			//return func( a_dataset, a_material );
		}
		static inline REL::Relocation<decltype(thunk)> func;

		// SkyrimSE.exe+078199C - E8 1F4AB5FF           - call SkyrimSE.exe+2D63C0 => ProjectileVsStatic?
		// SkyrimSE.exe+07806C2 - E8 F95CB5FF           - call SkyrimSE.exe+2D63C0 => ActorVsProjectile?

		// 44203+0x402 SkyrimSE.exe+0x7802C0+0x402
		// 36210+0x1F2 SkyrimSE.exe+0x5C4350+0x1F2
		// 44213+0xBC SkyrimSE.exe+0x7818E0+0xBC
		static void Install( REL::Version a_ver )
		{
			_CRT_UNUSED(a_ver);

			stl::write_thunk_call<GetImpactDataHook>( REL::Offset( 0x7806C2 ).address() );
			stl::write_thunk_call<GetImpactDataHook>( REL::Offset( 0x5C4542 ).address() );
			stl::write_thunk_call<GetImpactDataHook>( REL::Offset( 0x78199C ).address() );
		}
	};

	struct HandleProjectileAttackHook
	{
		// 36991+0xA3D SkyrimSE.exe+0x5EBBE0+0xA3D
		static void* thunk( RE::Character* a_character, RE::HitData* a_hitData )
		{
			auto aggressorPtr	= a_hitData->aggressor ? a_hitData->aggressor.get() : nullptr;
			auto targetPtr		= a_hitData->target ? a_hitData->target.get() : nullptr;

			auto aggressorRef	= aggressorPtr ? aggressorPtr->AsReference() : nullptr;
			auto targetRef		= targetPtr ? targetPtr->AsReference() : nullptr;

			std::lock_guard<std::mutex> lock( g_handleProjectileAttackMutex );

			// Find matching hit data
			std::vector<HitDataOverride>::iterator iter;
			for( iter = g_HitDataOverride.begin(); iter != g_HitDataOverride.end(); ++iter )
			{
				if( iter->aggressor == aggressorRef &&
					iter->target == targetRef &&
					iter->location == a_hitData->unk00 )
				{
					g_ImpactOverride = iter->impactData;
					a_hitData->totalDamage *= iter->damageMult;

					g_HitDataOverride.erase( iter );
					break;
				}
			}

			// Impact override is consumed in this function
			auto result = func( a_character, a_hitData );

			// Reset
			g_ImpactOverride = NULL;

			unsigned long long currentTimestamp;
			QueryPerformanceCounter( (LARGE_INTEGER*)&currentTimestamp );

			// Cleanup hit data that does not get processed in time
			for( iter = g_HitDataOverride.begin(); iter != g_HitDataOverride.end(); )
			{
				if( iter->expireTimestamp <= currentTimestamp )
					iter = g_HitDataOverride.erase( iter );
				else
					++iter;
			}

			return result;
		}
		static inline REL::Relocation<decltype(thunk)> func;

		static bool Check( REL::Version a_ver )
		{
			auto hook = RelocationHelper::Get( RelocationID::HandleProjectileAttack, a_ver );
			if( *(uint8_t*)hook.address() != 0xE8 )
			{
				logger::critical( "Opcode at injection site not matched. Aborting..." );
				return false;
			}

			return true;
		}

		static void Install( REL::Version a_ver )
		{
			auto hook = RelocationHelper::Get( RelocationID::HandleProjectileAttack, a_ver );
			stl::write_thunk_call<HandleProjectileAttackHook>( hook.address() );
		}
	};

	struct ProjectileGetImpackHookActor
	{
		// GetImpactData( a_dataset, a_material )
		// There's appear to be some limit to how many impacts that can play on an actor at once
		// This function does sometime got skipped on projectile hit
		static RE::BGSImpactData* thunk( RE::BGSImpactDataSet* a_dataset, RE::BGSMaterialType* a_material )
		{
			if( g_ImpactOverride )
				return g_ImpactOverride;

			return func( a_dataset, a_material );
		}
		static inline REL::Relocation<decltype(thunk)> func;

		static bool Check( REL::Version a_ver )
		{
			auto hook = RelocationHelper::Get( RelocationID::ProjectileVsActorGetImpactData, a_ver );
			if( *(uint8_t*)hook.address() != 0xE8 )
			{
				logger::critical( "Opcode at injection site not matched. Aborting..." );
				return false;
			}

			return true;
		}

		// SkyrimSE.exe+062071F -> 37690+0x3AF
		// SkyrimSE.exe+062071F - E8 9C5CCBFF           - call SkyrimSE.exe+2D63C0
		static void Install( REL::Version a_ver )
		{
			auto hook = RelocationHelper::Get( RelocationID::ProjectileVsActorGetImpactData, a_ver );
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
						LocationalDamage::ApplyLocationalDamage( a_projectile, targetPtr->AsReference(), impactLocation );
				}
			}

			return a_projectile->Unk_B8();
		}

		static inline REL::Relocation<decltype(thunk)> func;

		static bool Check( REL::Version a_ver )
		{
			auto hook = RelocationHelper::Get( RelocationID::ProjectileImpact, a_ver );
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
			auto hook = RelocationHelper::Get( RelocationID::ProjectileImpact, a_ver );
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
			auto hook = RelocationHelper::Get( RelocationID::DamageHook, a_ver );
			if( *(uint8_t*)hook.address() != 0x4C )
			{
				logger::critical( "Opcode at injection site not matched. Aborting..." );
				return false;
			}

			return true;
		}

		static void Install( REL::Version a_ver )
		{
			auto hook = RelocationHelper::Get( RelocationID::DamageHook, a_ver );
			Patch patch( hook.address() + 8, a_ver );
			REL::safe_fill( hook.address(), 0x90, 8 );
			stl::write_patch_branch<DamageHook>( hook.address(), patch );
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

	bool Install( REL::Version a_ver );
}
