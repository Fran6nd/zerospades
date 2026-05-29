/*
 Copyright (c) 2026 ZeroSpades developers

 This file is part of ZeroSpades.

 ZeroSpades is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 ZeroSpades is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with ZeroSpades.  If not, see <http://www.gnu.org/licenses/>.

 */

#include "ScriptManager.h"
#include <Gui/ModsScreenHelper.h>

namespace spades {
	static gui::ModsScreenHelper* CreateModsScreenHelper() { return new gui::ModsScreenHelper(); }

	class ModsScreenHelperRegistrar : public ScriptObjectRegistrar {
	public:
		ModsScreenHelperRegistrar() : ScriptObjectRegistrar("ModsScreenHelper") {}

		void Register(ScriptManager* manager, Phase phase) override {
			asIScriptEngine* eng = manager->GetEngine();
			int r;
			eng->SetDefaultNamespace("spades");
			switch (phase) {
				case PhaseObjectType:
					r = eng->RegisterObjectType("ModsScreenHelper", 0, asOBJ_REF);
					manager->CheckError(r);
					break;
				case PhaseObjectMember:
					r = eng->RegisterObjectBehaviour(
					  "ModsScreenHelper", asBEHAVE_FACTORY, "ModsScreenHelper@ f()",
					  asFUNCTION(CreateModsScreenHelper), asCALL_CDECL);
					manager->CheckError(r);
					r = eng->RegisterObjectBehaviour(
					  "ModsScreenHelper", asBEHAVE_ADDREF, "void f()",
					  asMETHOD(gui::ModsScreenHelper, AddRef), asCALL_THISCALL);
					manager->CheckError(r);
					r = eng->RegisterObjectBehaviour(
					  "ModsScreenHelper", asBEHAVE_RELEASE, "void f()",
					  asMETHOD(gui::ModsScreenHelper, Release), asCALL_THISCALL);
					manager->CheckError(r);
					r = eng->RegisterObjectMethod(
					  "ModsScreenHelper", "void StartRefresh()",
					  asMETHOD(gui::ModsScreenHelper, StartRefresh), asCALL_THISCALL);
					manager->CheckError(r);
					r = eng->RegisterObjectMethod(
					  "ModsScreenHelper", "bool PollRefreshState()",
					  asMETHOD(gui::ModsScreenHelper, PollRefreshState), asCALL_THISCALL);
					manager->CheckError(r);
					r = eng->RegisterObjectMethod(
					  "ModsScreenHelper", "string GetRefreshMessage()",
					  asMETHOD(gui::ModsScreenHelper, GetRefreshMessage), asCALL_THISCALL);
					manager->CheckError(r);
					r = eng->RegisterObjectMethod(
					  "ModsScreenHelper", "int GetRefreshTotal()",
					  asMETHOD(gui::ModsScreenHelper, GetRefreshTotal), asCALL_THISCALL);
					manager->CheckError(r);
					r = eng->RegisterObjectMethod(
					  "ModsScreenHelper", "int GetRefreshDone()",
					  asMETHOD(gui::ModsScreenHelper, GetRefreshDone), asCALL_THISCALL);
					manager->CheckError(r);
					r = eng->RegisterObjectMethod(
					  "ModsScreenHelper", "string GetRefreshCurrentItem()",
					  asMETHOD(gui::ModsScreenHelper, GetRefreshCurrentItem), asCALL_THISCALL);
					manager->CheckError(r);
					r = eng->RegisterObjectMethod(
					  "ModsScreenHelper", "array<string>@ GetModNames()",
					  asMETHOD(gui::ModsScreenHelper, GetModNames), asCALL_THISCALL);
					manager->CheckError(r);
					r = eng->RegisterObjectMethod(
					  "ModsScreenHelper", "int GetModPakCount(string)",
					  asMETHOD(gui::ModsScreenHelper, GetModPakCount), asCALL_THISCALL);
					manager->CheckError(r);
					r = eng->RegisterObjectMethod(
					  "ModsScreenHelper", "int64 GetModTotalSize(string)",
					  asMETHOD(gui::ModsScreenHelper, GetModTotalSize), asCALL_THISCALL);
					manager->CheckError(r);
					r = eng->RegisterObjectMethod(
					  "ModsScreenHelper", "array<string>@ GetModContents(string)",
					  asMETHOD(gui::ModsScreenHelper, GetModContents), asCALL_THISCALL);
					manager->CheckError(r);
					r = eng->RegisterObjectMethod(
					  "ModsScreenHelper", "string MergeMod(string)",
					  asMETHOD(gui::ModsScreenHelper, MergeMod), asCALL_THISCALL);
					manager->CheckError(r);
					r = eng->RegisterObjectMethod(
					  "ModsScreenHelper", "string ResetUserMods()",
					  asMETHOD(gui::ModsScreenHelper, ResetUserMods), asCALL_THISCALL);
					manager->CheckError(r);
					r = eng->RegisterObjectMethod(
					  "ModsScreenHelper", "string GetModsIndexUrl()",
					  asMETHOD(gui::ModsScreenHelper, GetModsIndexUrl), asCALL_THISCALL);
					manager->CheckError(r);
					r = eng->RegisterObjectMethod(
					  "ModsScreenHelper", "string GetModsDirAbs()",
					  asMETHOD(gui::ModsScreenHelper, GetModsDirAbs), asCALL_THISCALL);
					manager->CheckError(r);
					break;
				default: break;
			}
		}
	};

	static ModsScreenHelperRegistrar registrar;
} // namespace spades
