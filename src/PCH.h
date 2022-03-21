#pragma once

#pragma warning(push)
#include <RE/Skyrim.h>
#include <REL/Relocation.h>
#include <SKSE/SKSE.h>

#ifdef NDEBUG
#	include <spdlog/sinks/basic_file_sink.h>
#else
#	include <spdlog/sinks/msvc_sink.h>
#endif

#include <xbyak\xbyak.h>
#include <SimpleIni.h>
#pragma warning(pop)

using namespace std::literals;

namespace logger = SKSE::log;

namespace util
{
	using SKSE::stl::report_and_fail;
}

namespace stl
{
	using namespace SKSE::stl;

	template <class T>
	void write_thunk_call(std::uintptr_t a_src)
	{
		auto& trampoline = SKSE::GetTrampoline();
		SKSE::AllocTrampoline(14);

		T::func = trampoline.write_call<5>(a_src, T::thunk);
	}

	template <class T>
	void write_patch_call( std::uintptr_t a_src, Xbyak::CodeGenerator& a_code )
	{
		auto& trampoline = SKSE::GetTrampoline();
		SKSE::AllocTrampoline( 14 + a_code.getSize() );
		auto target = trampoline.allocate( a_code );

		T::func = trampoline.write_call<5>( a_src, target );
	}

	template <class T>
	void write_patch_branch( std::uintptr_t a_src, Xbyak::CodeGenerator& a_code )
	{
		auto& trampoline = SKSE::GetTrampoline();
		SKSE::AllocTrampoline( 14 + a_code.getSize() );
		auto target = trampoline.allocate( a_code );

		T::func = trampoline.write_branch<5>( a_src, target );
	}

	template <class F, size_t index, class T>
	void write_vfunc()
	{
		REL::Relocation<std::uintptr_t> vtbl{ F::VTABLE[index] };
		T::func = vtbl.write_vfunc(T::size, T::thunk);
	}

	template <class F, class T>
	void write_vfunc()
	{
		write_vfunc<F, 0, T>();
	}
}

#define DLLEXPORT __declspec(dllexport)

#include "Plugin.h"
