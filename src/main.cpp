#include "LocationalDamage.h"

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

static void SKSEMessageHandler(SKSE::MessagingInterface::Message* message) 
{
	if( message->type == SKSE::MessagingInterface::kDataLoaded )
	{
		LocationalDamage::InitFormEditorIDMap();
		LocationalDamage::InitPerkConditions();
	}
}

extern "C" DLLEXPORT bool SKSEAPI SKSEPlugin_Load( const SKSE::LoadInterface* a_skse )
{
	InitializeLog();

	SKSE::Init( a_skse );
	SKSE::GetMessagingInterface()->RegisterListener( "SKSE", SKSEMessageHandler );

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
