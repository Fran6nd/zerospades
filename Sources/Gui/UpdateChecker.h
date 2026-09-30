/*
 Copyright (c) 2026 Fran6nd, ZeroSpades developers.

 This file is part of ZeroSpades, a fork of OpenSpades.

 ZeroSpades is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 ZeroSpades is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with ZeroSpades.	 If not, see <http://www.gnu.org/licenses/>.

 */

#pragma once

#include <array>
#include <mutex>
#include <optional>
#include <string>

namespace spades {
	namespace gui {
		/** A `major.minor.patch` release number, as tagged on the releases page. */
		struct ReleaseVersion {
			// Not named fields: glibc defines `major` and `minor` as macros.
			std::array<int, 3> components{}; // major, minor, patch

			/** The version this build was compiled as. */
			static ReleaseVersion Current();

			/**
			 * Parses a release tag such as `v0.1.0`, `0.1` or `v0.1.0-rc1`. A leading
			 * `v` and any suffix after the numeric part are ignored; missing trailing
			 * components are zero.
			 */
			static std::optional<ReleaseVersion> Parse(const std::string& tag);

			std::string ToString() const;

			bool operator<(const ReleaseVersion& o) const { return components < o.components; }
		};

		/**
		 * Asks the releases page, once per launch and in the background, whether a
		 * newer ZeroSpades has been published. It only reports; it never downloads
		 * or installs anything.
		 *
		 * The check is process-wide: the main screen and the in-game menu read the
		 * same result, so a notice seen in one is consistent with the other.
		 */
		class UpdateChecker {
		public:
			enum class State {
				/** The check has not been started (or is disabled). */
				Idle,
				Checking,
				UpToDate,
				UpdateAvailable,
				/** The request or its response failed; nothing is shown. */
				Failed,
			};

			struct Result {
				State state = State::Idle;
				/** The latest published version; set when `state` is `UpdateAvailable`. */
				ReleaseVersion latestVersion;
				/** The release page to send the user to. */
				std::string releaseUrl;
			};

			static UpdateChecker& Get();

			/**
			 * Starts the check unless it already ran in this process or the user
			 * turned it off (`cl_zsCheckForUpdates`). Call on the main thread.
			 */
			void Start();

			Result GetResult();

			/**
			 * Returns true exactly once per process, the first time it is called
			 * after an update was found, so the startup prompt is shown only once
			 * even though the main screen is rebuilt (resize, return from a game).
			 */
			bool ClaimStartupPrompt();

		private:
			class Query;
			friend class Query;

			std::mutex mutex;
			Result result;
			bool started = false;
			bool startupPromptClaimed = false;

			UpdateChecker() = default;
			UpdateChecker(const UpdateChecker&) = delete;
			UpdateChecker& operator=(const UpdateChecker&) = delete;

			void Finish(Result&& r);
		};
	} // namespace gui
} // namespace spades
