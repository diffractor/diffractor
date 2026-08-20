// This file is part of the Diffractor photo and video organizer
// Copyright 2026  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

// Purpose: The packed platform identity reported by the daily ping. Encoder and decoder live
// together so a round-trip test can cover every field at its extremes; the layout is described in
// docs/v-1.27.2.md. The store keeps daily aggregates, so a field misread is a day that cannot be
// re-read: nothing here may guess.

#pragma once

namespace df
{
	// Every enum below follows the same two rules, and they matter more than the widths: 0 is always
	// unknown, and the top value is always other. A rising "other" count is the early warning that a
	// field is running out of room. A meaning is fixed once assigned - retire a value by leaving it
	// unused, never by giving it to something else.
	enum class os_family : uint8_t
	{
		unknown = 0,
		windows = 1,
		linux_ = 2,
		mac = 3,
		other = 15,
	};

	enum class machine_arch : uint8_t
	{
		unknown = 0,
		x86 = 1,
		x64 = 2,
		arm32 = 3,
		arm64 = 4,
		riscv64 = 5,
		other = 15,
	};

	enum class package_kind : uint8_t
	{
		unknown = 0,
		installer = 1,
		microsoft_store = 2,
		portable = 3,
		deb = 4,
		rpm = 5,
		flatpak = 6,
		snap = 7,
		appimage = 8,
		other = 31,
	};

	// Coarse deliberately: Windows 10 against Windows 11 decides what to support, 23H2 against 24H2
	// does not. A family that has no meaningful version reports unknown rather than inventing one.
	enum class os_release : uint8_t
	{
		unknown = 0,
		windows_7 = 1,
		windows_8_1 = 2,
		windows_10 = 3,
		windows_11 = 4,
		windows_later = 5,
		other = 63,
	};

	// Identity only: the tuple every measurement is sliced *by*, never a measurement itself. Every
	// field in the key multiplies the daily rows; every separate counter only adds, so a bit is not
	// free here and nothing that is being measured belongs in it.
	struct environment_facts
	{
		os_family family = os_family::unknown;
		os_release release = os_release::unknown;
		machine_arch process = machine_arch::unknown;
		machine_arch machine = machine_arch::unknown;
		package_kind package = package_kind::unknown;

		friend bool operator==(const environment_facts&, const environment_facts&) = default;
	};

	// Read first, and fail closed on anything else. With daily aggregates a misdecode cannot be
	// undone, so an unrecognised layout is counted and left undecoded rather than guessed at.
	inline constexpr uint64_t environment_layout_version = 1;

	namespace environment_layout
	{
		struct field
		{
			int shift;
			int width;

			constexpr uint64_t mask() const { return ((1ull << width) - 1ull) << shift; }

			constexpr uint64_t pack(const uint64_t value) const { return (value & ((1ull << width) - 1ull)) << shift; }

			constexpr uint64_t unpack(const uint64_t packed) const
			{
				return (packed >> shift) & ((1ull << width) - 1ull);
			}
		};

		inline constexpr field version{0, 4};
		inline constexpr field family{4, 4};
		inline constexpr field release{8, 6};
		inline constexpr field process{14, 4};
		inline constexpr field machine{18, 4};
		inline constexpr field package{22, 5};

		// Everything above the last field. The reader masks it, or the first build that uses bit 27
		// splits every existing row in two.
		inline constexpr uint64_t reserved = ~(version.mask() | family.mask() | release.mask() |
			process.mask() | machine.mask() | package.mask());

		constexpr bool fits(const field f, const uint64_t value) { return value < (1ull << f.width); }

		// pack truncates, so a value that outgrew its field would be reported as unknown with nothing
		// to show for it. These fail the build instead, at the point the enum gains the value.
		static_assert(fits(family, static_cast<uint64_t>(os_family::other)));
		static_assert(fits(release, static_cast<uint64_t>(os_release::other)));
		static_assert(fits(process, static_cast<uint64_t>(machine_arch::other)));
		static_assert(fits(machine, static_cast<uint64_t>(machine_arch::other)));
		static_assert(fits(package, static_cast<uint64_t>(package_kind::other)));
		static_assert(fits(version, environment_layout_version));
	}

	inline uint64_t pack_environment(const environment_facts& facts)
	{
		return environment_layout::version.pack(environment_layout_version) |
			environment_layout::family.pack(static_cast<uint64_t>(facts.family)) |
			environment_layout::release.pack(static_cast<uint64_t>(facts.release)) |
			environment_layout::process.pack(static_cast<uint64_t>(facts.process)) |
			environment_layout::machine.pack(static_cast<uint64_t>(facts.machine)) |
			environment_layout::package.pack(static_cast<uint64_t>(facts.package));
	}

	// Answers nothing for a layout it does not recognise, and nothing for a value carrying reserved
	// bits: both mean the writer knew something this reader does not.
	inline std::optional<environment_facts> unpack_environment(const uint64_t packed)
	{
		if (environment_layout::version.unpack(packed) != environment_layout_version) return {};
		if ((packed & environment_layout::reserved) != 0) return {};

		environment_facts result;
		result.family = static_cast<os_family>(environment_layout::family.unpack(packed));
		result.release = static_cast<os_release>(environment_layout::release.unpack(packed));
		result.process = static_cast<machine_arch>(environment_layout::process.unpack(packed));
		result.machine = static_cast<machine_arch>(environment_layout::machine.unpack(packed));
		result.package = static_cast<package_kind>(environment_layout::package.unpack(packed));
		return result;
	}
}
