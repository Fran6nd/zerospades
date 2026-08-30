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

		ScriptManager();
		~ScriptManager();

	public:
		/**
		 * Returns the script engine, building it on first use. Building compiles
		 * every script reachable from `Scripts/Main.as` against the resources
		 * that are mounted right now, so the caller decides which set of mods
		 * the scripts come from by mounting before the first call.
		 */
		static ScriptManager* GetInstance();

		/**
		 * Releases the script engine and every context it owns. The next
		 * `GetInstance` call builds a fresh engine and recompiles the scripts,
		 * which is how a changed mod set is picked up without restarting.
		 *
		 * Every script object must already be destroyed - the engine owns their
		 * types, so a surviving object would dangle. In practice that means no
		 * `client::Client` may be alive.
		 */
		static void Shutdown();

		/** Whether the engine is currently built. */
		static bool IsLoaded();

		static void CheckError(int);

		asIScriptEngine* GetEngine() const { return engine; }

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

		/**
		 * Marks every phase of every registrar as not yet done, so the next
		 * `RegisterAll` runs them again. Registrars are process-wide statics
		 * while the registrations they perform (and any type handle they cache)
		 * belong to a single engine, so this must run before registering
		 * against a newly created engine.
		 */
		static void ResetAllPhases();

	private:
		bool phaseDone[PhaseCount];
		std::string name;
	};

} // namespace spades