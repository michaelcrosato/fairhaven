// Fairhaven - transient reminder hysteresis; it never changes the shared ledger.
#pragma once

#include "CoreMinimal.h"
#include "NPC/UEGT2NPCTypes.h"

namespace UEGT2NeedReminders
{
	enum : uint8 { Energy = 1, Fed = 2, Relief = 4, Company = 8, All = 15 };
	constexpr float Low = 0.34f;
	constexpr float Recovered = 0.50f;
	constexpr double GraceSeconds = 5.0;
	constexpr double DisplaySeconds = 5.0;
	constexpr double CooldownSeconds = 30.0;

	/** At most one pending bit per need, re-armed only after substantial recovery. */
	struct UEGT2_API FState
	{
		void Reset(double Now);
		bool Observe(const FUEGT2NPCNeeds& Needs);
		uint8 Ready(double Now) const;
		void Delivered(uint8 Mask, double Now);
	private:
		uint8 Armed = All;
		uint8 Pending = 0;
		double GraceUntil = 0.0;
		double NextDelivery = 0.0;
	};
}
