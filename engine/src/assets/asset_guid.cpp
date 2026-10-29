#include <pch.h>
#include "asset_guid.h"

#include <random>

namespace wiwa
{
	AssetGuid AssetGuid::generate()
	{
		static thread_local std::mt19937_64 generator{ std::random_device{}() };

		AssetGuid guid;
		do
		{
			guid.Hi = generator();
			guid.Lo = generator();
		} while (!guid.valid());

		return guid;
	}

	std::optional<AssetGuid> AssetGuid::from_string(std::string_view text)
	{
		if (text.size() != 32)
		{
			return std::nullopt;
		}

		u64 parts[2] = {};
		for (size_t i = 0; i < 32; i++)
		{
			const char c = text[i];
			u64 digit;
			if (c >= '0' && c <= '9')
			{
				digit = static_cast<u64>(c - '0');
			}
			else if (c >= 'a' && c <= 'f')
			{
				digit = static_cast<u64>(c - 'a' + 10);
			}
			else if (c >= 'A' && c <= 'F')
			{
				digit = static_cast<u64>(c - 'A' + 10);
			}
			else
			{
				return std::nullopt;
			}

			parts[i / 16] = (parts[i / 16] << 4) | digit;
		}

		return AssetGuid{ parts[0], parts[1] };
	}

	static u64 mix64(u64 x)
	{
		x += 0x9E3779B97F4A7C15ull;
		x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
		x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
		return x ^ (x >> 31);
	}

	AssetGuid AssetRef::resolved() const
	{
		if (LocalId == 0)
		{
			return Guid;
		}

		AssetGuid result{ mix64(Guid.Hi ^ mix64(LocalId)), mix64(Guid.Lo + LocalId) };
		if (!result.valid())
		{
			result.Lo = 1;
		}
		return result;
	}

	std::string AssetGuid::to_string() const
	{
		return fmt::format("{:016x}{:016x}", Hi, Lo);
	}
}
