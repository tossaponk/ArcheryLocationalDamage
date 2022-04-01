#pragma once

struct HitDataOverride
{
	RE::TESObjectREFR*		aggressor = 0;
	RE::TESObjectREFR*		target = 0;
	RE::NiPoint3			location;
	float					damageMult = 1.0f;
	RE::BGSImpactData*		impactData = nullptr;
	unsigned long long		expireTimestamp;
};

struct LocationalDamage
{
	typedef void(*MagicCaster_CastPtr)( RE::MagicItem* a_spell, bool unk1, RE::TESObjectREFR* a_target, float a_magOverride, bool unk2, float unk3, void* unk4 );
	static MagicCaster_CastPtr fnCastMagic;

	static void ApplyLocationalDamage( RE::Projectile* a_projectile, RE::TESObjectREFR* a_target, RE::NiPoint3* a_location );

	static bool RandomPercent( int percent )
	{
		return rand() % 100 <= percent;
	}

	// Amplify newly created active effects casted by an aggressor
	static void AmplifyActiveEffect( RE::Actor* a_actor, RE::Actor* a_aggressor, float a_magnitudeMult )
	{
		if( !a_actor )
			return;

		auto activeEffectList = a_actor->GetActiveEffectList();
		for( auto& activeEffect : *activeEffectList )
		{
			auto caster = activeEffect->caster ? activeEffect->caster.get() : NULL;
			auto casterActor = caster ? caster->As<RE::Actor>() : nullptr;
			if( activeEffect->elapsedSeconds == 0 && (casterActor == a_aggressor || a_aggressor == NULL) )
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

	static bool Install( REL::Version a_ver );
};
