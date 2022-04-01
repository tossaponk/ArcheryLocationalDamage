#include "Hooks.h"
#include "LocationalDamage.h"

std::mutex g_handleProjectileAttackMutex;

bool Hooks::Install( REL::Version a_ver )
{
	//REL::Relocation<uintptr_t> projectileUpdateHookLoc{ REL::ID(44028), 0x79 };
	//stl::write_thunk_call<ProjectileUpdateHook>( projectileUpdateHookLoc.address() );

	if( !DamageHook::Check( a_ver ) )					return false;
	if( !ProjectileGetImpackHookActor::Check( a_ver ) ) return false;
	if( !ProjectileImpactHook::Check( a_ver ) )			return false;
	if( !HandleProjectileAttackHook::Check( a_ver ) )	return false;

	//GetImpactDataHook::Install( a_ver );
	DamageHook::Install( a_ver );
	ProjectileGetImpackHookActor::Install( a_ver );
	ProjectileImpactHook::Install( a_ver );
	HandleProjectileAttackHook::Install( a_ver );

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
