// Fairhaven (UEGT2) - dev mode: the free camera and world controls.
//
// This subsystem is the single owner of dev state. The menu and the console
// commands both go through it rather than poking the pawn or the sky directly,
// which is what keeps them showing the same thing. The pawn knows how to fly;
// this knows whether it should be.
//
// A world subsystem rather than an actor on purpose: nothing here needs to be
// placed in the map, so a content rebuild cannot destroy it.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "World/UEGT2Weather.h"
#include "UEGT2DevModeSubsystem.generated.h"

class AUEGT2Character;
class AUEGT2PlayerController;
class AUEGT2SkyController;

/** Engine view mode exposed on the Display tab. */
UENUM(BlueprintType)
enum class EUEGT2ViewMode : uint8
{
	Lit,
	Unlit,
	Wireframe,
	Count UMETA(Hidden)
};

UCLASS()
class UEGT2_API UUEGT2DevModeSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

	static UUEGT2DevModeSubsystem* Get(const UWorld* World);

	// ---- Master -----------------------------------------------------------
	/** Turning it off restores walking, collision, jumps, speed and game speed. */
	UFUNCTION(BlueprintCallable, Category = "UEGT2|Dev") void SetDevModeEnabled(bool bEnabled);
	UFUNCTION(BlueprintPure, Category = "UEGT2|Dev") bool IsDevModeEnabled() const { return bDevModeEnabled; }

	// ---- Player -----------------------------------------------------------
	UFUNCTION(BlueprintCallable, Category = "UEGT2|Dev") void SetGodMode(bool bEnabled);
	UFUNCTION(BlueprintPure, Category = "UEGT2|Dev") bool IsGodMode() const;

	UFUNCTION(BlueprintCallable, Category = "UEGT2|Dev") void SetFlyEnabled(bool bEnabled);
	UFUNCTION(BlueprintPure, Category = "UEGT2|Dev") bool IsFlyEnabled() const;

	UFUNCTION(BlueprintCallable, Category = "UEGT2|Dev") void SetNoclipEnabled(bool bEnabled);
	UFUNCTION(BlueprintPure, Category = "UEGT2|Dev") bool IsNoclipEnabled() const;

	/** Clamped to 1-50. */
	UFUNCTION(BlueprintCallable, Category = "UEGT2|Dev") void SetSpeedMultiplier(float Multiplier);
	UFUNCTION(BlueprintPure, Category = "UEGT2|Dev") float GetSpeedMultiplier() const;

	// ---- World ------------------------------------------------------------
	UFUNCTION(BlueprintCallable, Category = "UEGT2|Dev") void SetTimeOfDay(float Hours);
	UFUNCTION(BlueprintPure, Category = "UEGT2|Dev") float GetTimeOfDay() const;

	UFUNCTION(BlueprintCallable, Category = "UEGT2|Dev") void SetDayNightCycleEnabled(bool bEnabled);
	UFUNCTION(BlueprintPure, Category = "UEGT2|Dev") bool IsDayNightCycleEnabled() const;

	UFUNCTION(BlueprintCallable, Category = "UEGT2|Dev") void SetDayLengthMinutes(float Minutes);
	UFUNCTION(BlueprintPure, Category = "UEGT2|Dev") float GetDayLengthMinutes() const;

	UFUNCTION(BlueprintCallable, Category = "UEGT2|Dev") void SetWeather(EUEGT2Weather NewWeather);
	UFUNCTION(BlueprintPure, Category = "UEGT2|Dev") EUEGT2Weather GetWeather() const;

	UFUNCTION(BlueprintCallable, Category = "UEGT2|Dev") void SetFogDensity(float Density);
	UFUNCTION(BlueprintPure, Category = "UEGT2|Dev") float GetFogDensity() const;

	/** Global time dilation, 0.1-5. */
	UFUNCTION(BlueprintCallable, Category = "UEGT2|Dev") void SetGameSpeed(float Scale);
	UFUNCTION(BlueprintPure, Category = "UEGT2|Dev") float GetGameSpeed() const { return GameSpeed; }

	// ---- Display ----------------------------------------------------------
	UFUNCTION(BlueprintCallable, Category = "UEGT2|Dev") void SetViewMode(EUEGT2ViewMode Mode);
	UFUNCTION(BlueprintPure, Category = "UEGT2|Dev") EUEGT2ViewMode GetViewMode() const { return ViewMode; }

	UFUNCTION(BlueprintCallable, Category = "UEGT2|Dev") void SetDiagnosticsVisible(bool bVisible);
	UFUNCTION(BlueprintPure, Category = "UEGT2|Dev") bool IsDiagnosticsVisible() const;

	UFUNCTION(BlueprintCallable, Category = "UEGT2|Dev") void SetShowCollision(bool bVisible);
	UFUNCTION(BlueprintPure, Category = "UEGT2|Dev") bool IsShowCollision() const { return bShowCollision; }

	UFUNCTION(BlueprintCallable, Category = "UEGT2|Dev") void SetStatFps(bool bVisible);
	UFUNCTION(BlueprintPure, Category = "UEGT2|Dev") bool IsStatFps() const { return bStatFps; }

	UFUNCTION(BlueprintCallable, Category = "UEGT2|Dev") void SetStatUnit(bool bVisible);
	UFUNCTION(BlueprintPure, Category = "UEGT2|Dev") bool IsStatUnit() const { return bStatUnit; }

	UFUNCTION(BlueprintCallable, Category = "UEGT2|Dev") void SetDrawInteractionProbe(bool bVisible);
	UFUNCTION(BlueprintPure, Category = "UEGT2|Dev") bool IsDrawInteractionProbe() const { return bDrawInteractionProbe; }

	// ---- Teleport ---------------------------------------------------------
	/** Teleport to a tour viewpoint by index into UUEGT2CaptureSubsystem::GetTour(). */
	UFUNCTION(BlueprintCallable, Category = "UEGT2|Dev") bool TeleportToViewpoint(int32 Index);

	/** Teleport to a tour viewpoint by name, e.g. "Vista". */
	UFUNCTION(BlueprintCallable, Category = "UEGT2|Dev") bool TeleportToViewpointNamed(const FString& Name);

	UFUNCTION(BlueprintCallable, Category = "UEGT2|Dev") void SavePosition();
	UFUNCTION(BlueprintCallable, Category = "UEGT2|Dev") bool RestorePosition();
	UFUNCTION(BlueprintPure, Category = "UEGT2|Dev") bool HasSavedPosition() const { return bHasSavedPosition; }

	/** Player location in centimetres, or zero when there is no pawn. */
	UFUNCTION(BlueprintPure, Category = "UEGT2|Dev") FVector GetPlayerLocation() const;

	/** True when there is a pawn to act on; the menu greys out player rows without one. */
	UFUNCTION(BlueprintPure, Category = "UEGT2|Dev") bool HasPlayer() const;

private:
	AUEGT2Character* GetCharacter() const;
	AUEGT2PlayerController* GetPC() const;
	AUEGT2SkyController* GetSky() const;
	void RunConsole(const TCHAR* Command) const;
	void RegisterConsoleCommands();
	/** Drop to the ground under a world XY and put the player HeightMetres above it. */
	bool TeleportToGround(const FVector2D& WorldXY, float HeightMetres, float Yaw, float Pitch);
	void Notify(const FString& Message) const;

	bool bDevModeEnabled = false;
	float GameSpeed = 1.0f;
	EUEGT2ViewMode ViewMode = EUEGT2ViewMode::Lit;
	bool bShowCollision = false;
	bool bStatFps = false;
	bool bStatUnit = false;
	bool bDrawInteractionProbe = false;

	FVector SavedLocation = FVector::ZeroVector;
	FRotator SavedRotation = FRotator::ZeroRotator;
	bool bHasSavedPosition = false;
};
