#pragma once

enum RelocationID
{
	DamageCorrection,
	DamageHook,
	ProjectileUpdate,
	ProjectileImpact,
	ProjectileVsActorGetImpactData,
	WorldToCameraMatrix,
	HandleProjectileAttack,
};

namespace RelocationHelper
{
	static REL::Relocation<uintptr_t> Get( RelocationID a_id, REL::Version a_ver )
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
		case WorldToCameraMatrix:
			return a_ver <= SKSE::RUNTIME_1_5_97 ?
				REL::Relocation<uintptr_t>( REL::ID(519579) ):
				REL::Relocation<uintptr_t>( REL::ID(406126) );
		case HandleProjectileAttack:
			return a_ver <= SKSE::RUNTIME_1_5_97 ?
				REL::Relocation<uintptr_t>( REL::ID(36016), 0x926 ): // SkyrimSE.exe+0x5C6EE0+0x926
				REL::Relocation<uintptr_t>( REL::ID(36991), 0xA3D ); // SkyrimSE.exe+0x5EBBE0+0xA3D
		}

		return REL::Relocation<uintptr_t>();
	}
}
