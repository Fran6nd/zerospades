/*
 Copyright (c) 2013 yvt

 This file is part of OpenSpades.

 OpenSpades is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 OpenSpades is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with OpenSpades.  If not, see <http://www.gnu.org/licenses/>.

 */

#pragma once

// Configure AngelScript string addon to use property accessors
// This is required for compatibility with ZeroSpades scripts that access string.length as a property
#define AS_USE_ACCESSORS 1

#include <AngelScript/include/angelscript.h>
#include <AngelScript/addons/scriptany.h>
#include <AngelScript/addons/scriptarray.h>
#include <AngelScript/addons/scriptbuilder.h>
#include <AngelScript/addons/scriptdictionary.h>
#include <AngelScript/addons/scripthandle.h>
#include <AngelScript/addons/scripthelper.h>
#include <AngelScript/addons/scriptmath.h>
#include <AngelScript/addons/scriptmathcomplex.h>
#include <AngelScript/addons/scriptstdstring.h>
#include <AngelScript/addons/weakref.h>
#include <cstdint>
#include <list>
#include <mutex>

namespace spades {

	class ScriptContextHandle;

	class ScriptManager {
		friend class ScriptContextHandle;
		struct Context {
			asIScriptContext* obj;
			int refCount;
		};
		std::recursive_mutex contextMutex;
		std::list<Context*> contextFreeList;

		asIScriptEngine* engine;
		const std::uint64_t generation;

		ScriptManager();
		~ScriptManager();

	public:
		/** Builds the engine on first use, from the resources mounted then. */
		static ScriptManager* GetInstance();

		/**
		 * Releases the engine; the next `GetInstance` builds a new one. Every
		 * script object must be gone, i.e. no `client::Client` may be alive.
		 */
		static void Shutdown();

		static void CheckError(int);

		asIScriptEngine* GetEngine() const { return engine; }

		/** Unique per engine built. Caches compare this, not the pointer: a
		 * new engine can reuse a released one's address. */
		std::uint64_t GetGeneration() const { return generation; }

		ScriptContextHandle GetContext();
	};

	class ScriptContextUtils {
		asIScriptContext* context;

		void appendLocation(std::stringstream& ss, asIScriptFunction* func, const char* secName,
		                    int line, int column);

	public:
		ScriptContextUtils();
		ScriptContextUtils(asIScriptContext*);
		void ExecuteChecked();
		void SetNativeException(const std::exception&);
	};

	class ScriptContextHandle {
		ScriptManager* manager;
		ScriptManager::Context* obj;

		void Release();

	public:
		ScriptContextHandle();
		ScriptContextHandle(ScriptManager::Context*, ScriptManager* manager);
		ScriptContextHandle(const ScriptContextHandle&);
		~ScriptContextHandle();
		void operator=(const ScriptContextHandle&);
		asIScriptContext* GetContext() const;
		asIScriptContext* operator->() const;

		ScriptManager* GetManager() const { return manager; }

		void ExecuteChecked();
	};

	class ScriptObjectRegistrar {
	public:
		enum Phase { PhaseObjectType, PhaseGlobalFunction, PhaseObjectMember, PhaseCount };
		ScriptObjectRegistrar(const std::string& name);
		virtual void Register(ScriptManager* manager, Phase) = 0;

		static void RegisterOne(const std::string& name, ScriptManager* manager, Phase);
		static void RegisterAll(ScriptManager* manager, Phase);

		/** Marks every phase not done, before registering with a new engine. */
		static void ResetAllPhases();

	private:
		bool phaseDone[PhaseCount];
		std::string name;
	};

} // namespace spades