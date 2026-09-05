#include "UI/UEGT2NeedReminders.h"

void UEGT2NeedReminders::FState::Reset(double Now)
{
	Armed = All;
	Pending = 0;
	GraceUntil = Now + GraceSeconds;
	NextDelivery = GraceUntil;
}

bool UEGT2NeedReminders::FState::Observe(const FUEGT2NPCNeeds& Needs)
{
	const float Values[] = { Needs.Energy, Needs.Fed, Needs.Relief, Needs.Company };
	for (float Value : Values)
	{
		if (!FMath::IsFinite(Value) || Value < 0.0f || Value > 1.0f) { return false; }
	}
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Values); ++Index)
	{
		const uint8 Bit = 1 << Index;
		if (Values[Index] >= Recovered) { Armed |= Bit; }
		if (Values[Index] >= Low) { Pending &= ~Bit; }
		else if (Armed & Bit) { Pending |= Bit; Armed &= ~Bit; }
	}
	return true;
}

uint8 UEGT2NeedReminders::FState::Ready(double Now) const
{
	return FMath::IsFinite(Now) && Now >= GraceUntil && Now >= NextDelivery ? Pending : 0;
}

void UEGT2NeedReminders::FState::Delivered(uint8 Mask, double Now)
{
	Pending &= ~Mask;
	NextDelivery = Now + CooldownSeconds;
}
