#pragma once

#include "CoreMinimal.h"

/** Cached presentation only. Reading this never starts a disk operation. */
struct FUEGT2AutosaveStatus
{
	bool bAvailable = false;
	bool bBusy = false;
	/** Monotonic for this game instance; availability reads never increment it. */
	uint64 SuccessfulWrites = 0;
	FText Text;
};
