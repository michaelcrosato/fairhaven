#pragma once

#include "CoreMinimal.h"

/** The next occurrence of a whole clock hour, shared by the panel and commit. */
struct FUEGT2RestPreview
{
	int32 StartDayIndex = 0;
	float StartHour = 0.0f;
	int32 WakeDayIndex = 0;
	int32 WakeHour = 0;
	float DurationHours = 0.0f;
};
