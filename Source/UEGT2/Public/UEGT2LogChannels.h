// Fairhaven (UEGT2) - log channel declarations.
//
// Every subsystem logs through one of these channels so automation, smoke tests
// and future agents can filter output precisely. Add a channel here rather than
// logging through LogTemp.
#pragma once

#include "CoreMinimal.h"
#include "Logging/LogMacros.h"

/** Boot, module lifetime, game mode and high level flow. */
UEGT2_API DECLARE_LOG_CATEGORY_EXTERN(LogUEGT2, Log, All);
/** Player pawn, movement and camera. */
UEGT2_API DECLARE_LOG_CATEGORY_EXTERN(LogUEGT2Player, Log, All);
/** Interaction tracing and interactable actors. */
UEGT2_API DECLARE_LOG_CATEGORY_EXTERN(LogUEGT2Interaction, Log, All);
/** Settings load/save/apply. */
UEGT2_API DECLARE_LOG_CATEGORY_EXTERN(LogUEGT2Settings, Log, All);
/** Menus, HUD and UI state. */
UEGT2_API DECLARE_LOG_CATEGORY_EXTERN(LogUEGT2UI, Log, All);
/** World composition, streaming, environment and time of day. */
UEGT2_API DECLARE_LOG_CATEGORY_EXTERN(LogUEGT2World, Log, All);
/** Performance counters and the diagnostics overlay. */
UEGT2_API DECLARE_LOG_CATEGORY_EXTERN(LogUEGT2Diag, Log, All);
/** Dev mode: free camera, world controls and teleports. */
UEGT2_API DECLARE_LOG_CATEGORY_EXTERN(LogUEGT2Dev, Log, All);
/** Inhabitants: schedules, routing, speech and the population director. */
UEGT2_API DECLARE_LOG_CATEGORY_EXTERN(LogUEGT2NPC, Log, All);
/** Explicit player checkpoints and their feature gate. */
UEGT2_API DECLARE_LOG_CATEGORY_EXTERN(LogUEGT2Progress, Log, All);
/** Survey journal tracking and its feature gate. */
UEGT2_API DECLARE_LOG_CATEGORY_EXTERN(LogUEGT2Survey, Log, All);
