// Fairhaven (UEGT2) - all persisted player settings in one place.
//
// Graphics, audio, gameplay and control rebinds live here so there is exactly
// one save file (Saved/Config/.../GameUserSettings.ini) and one place to look.
// Adding a setting: add a UPROPERTY(Config), expose a getter/setter pair, and
// apply it in ApplyNonResolutionSettings().
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameUserSettings.h"
#include "UEGT2GameUserSettings.generated.h"

/** Broadcast after any settings apply, so UI and gameplay can refresh. */
DECLARE_MULTICAST_DELEGATE(FUEGT2SettingsApplied);

UENUM(BlueprintType)
enum class EUEGT2AudioBus : uint8
{
	Master,
	Effects,
	Ambience,
	Music,
	UI,
	Count UMETA(Hidden)
};

UCLASS(Config = GameUserSettings, ClassGroup = "UEGT2")
class UEGT2_API UUEGT2GameUserSettings : public UGameUserSettings
{
	GENERATED_BODY()

public:
	UUEGT2GameUserSettings();

	/** Never null in a running game. */
	UFUNCTION(BlueprintPure, Category = "UEGT2|Settings")
	static UUEGT2GameUserSettings* Get();

	/** Fires after ApplySettings / ApplyNonResolutionSettings. */
	static FUEGT2SettingsApplied OnSettingsApplied;

	virtual void ApplyNonResolutionSettings() override;
	virtual void SetToDefaults() override;

	// ---- Graphics ---------------------------------------------------------
	UFUNCTION(BlueprintPure, Category = "UEGT2|Graphics") float GetFieldOfView() const { return FieldOfView; }
	UFUNCTION(BlueprintCallable, Category = "UEGT2|Graphics") void SetFieldOfView(float Value);

	UFUNCTION(BlueprintPure, Category = "UEGT2|Graphics") bool GetMotionBlurEnabled() const { return bMotionBlur; }
	UFUNCTION(BlueprintCallable, Category = "UEGT2|Graphics") void SetMotionBlurEnabled(bool bValue);

	UFUNCTION(BlueprintPure, Category = "UEGT2|Graphics") bool GetBloomEnabled() const { return bBloom; }
	UFUNCTION(BlueprintCallable, Category = "UEGT2|Graphics") void SetBloomEnabled(bool bValue);

	UFUNCTION(BlueprintPure, Category = "UEGT2|Graphics") float GetResolutionScalePercent() const { return ResolutionScalePercent; }
	UFUNCTION(BlueprintCallable, Category = "UEGT2|Graphics") void SetResolutionScalePercent(float Value);

	UFUNCTION(BlueprintPure, Category = "UEGT2|Graphics") float GetBrightness() const { return Brightness; }
	UFUNCTION(BlueprintCallable, Category = "UEGT2|Graphics") void SetBrightness(float Value);

	UFUNCTION(BlueprintPure, Category = "UEGT2|Graphics") int32 GetFoliageDrawDistanceLevel() const { return FoliageDrawDistanceLevel; }
	UFUNCTION(BlueprintCallable, Category = "UEGT2|Graphics") void SetFoliageDrawDistanceLevel(int32 Value);
	/** Multiplier for authored nature-layer fade and cull distances. */
	float GetFoliageDrawDistanceScale() const;

	// ---- Audio ------------------------------------------------------------
	UFUNCTION(BlueprintPure, Category = "UEGT2|Audio")
	float GetAudioVolume(EUEGT2AudioBus Bus) const;

	UFUNCTION(BlueprintCallable, Category = "UEGT2|Audio")
	void SetAudioVolume(EUEGT2AudioBus Bus, float Value);

	static FText GetAudioBusDisplayName(EUEGT2AudioBus Bus);

	// ---- Gameplay ---------------------------------------------------------
	UFUNCTION(BlueprintPure, Category = "UEGT2|Gameplay") float GetMouseSensitivity() const { return MouseSensitivity; }
	UFUNCTION(BlueprintCallable, Category = "UEGT2|Gameplay") void SetMouseSensitivity(float Value);

	UFUNCTION(BlueprintPure, Category = "UEGT2|Gameplay") bool GetInvertLookY() const { return bInvertLookY; }
	UFUNCTION(BlueprintCallable, Category = "UEGT2|Gameplay") void SetInvertLookY(bool bValue);

	UFUNCTION(BlueprintPure, Category = "UEGT2|Gameplay") float GetHeadBobScale() const { return HeadBobScale; }
	UFUNCTION(BlueprintCallable, Category = "UEGT2|Gameplay") void SetHeadBobScale(float Value);

	UFUNCTION(BlueprintPure, Category = "UEGT2|Gameplay") bool GetToggleSprint() const { return bToggleSprint; }
	UFUNCTION(BlueprintCallable, Category = "UEGT2|Gameplay") void SetToggleSprint(bool bValue);

	UFUNCTION(BlueprintPure, Category = "UEGT2|Gameplay") bool GetShowCrosshair() const { return bShowCrosshair; }
	UFUNCTION(BlueprintCallable, Category = "UEGT2|Gameplay") void SetShowCrosshair(bool bValue);

	UFUNCTION(BlueprintPure, Category = "UEGT2|Gameplay") bool GetShowInteractPrompts() const { return bShowInteractPrompts; }
	UFUNCTION(BlueprintCallable, Category = "UEGT2|Gameplay") void SetShowInteractPrompts(bool bValue);

	/** The date, clock and temperature panel in the top-left. */
	UFUNCTION(BlueprintPure, Category = "UEGT2|Gameplay") bool GetShowAlmanac() const { return bShowAlmanac; }
	UFUNCTION(BlueprintCallable, Category = "UEGT2|Gameplay") void SetShowAlmanac(bool bValue);

	/** Temperature in Fahrenheit rather than Celsius. */
	UFUNCTION(BlueprintPure, Category = "UEGT2|Gameplay") bool GetUseFahrenheit() const { return bUseFahrenheit; }
	UFUNCTION(BlueprintCallable, Category = "UEGT2|Gameplay") void SetUseFahrenheit(bool bValue);

	/** The player's own needs, trade and purse, bottom left. */
	UFUNCTION(BlueprintPure, Category = "UEGT2|Gameplay") bool GetShowNeeds() const { return bShowNeeds; }
	UFUNCTION(BlueprintCallable, Category = "UEGT2|Gameplay") void SetShowNeeds(bool bValue);

	/** The text-message bubbles NPCs put over their heads. */
	UFUNCTION(BlueprintPure, Category = "UEGT2|Gameplay") bool GetShowSpeechBubbles() const { return bShowSpeechBubbles; }
	UFUNCTION(BlueprintCallable, Category = "UEGT2|Gameplay") void SetShowSpeechBubbles(bool bValue);

	/**
	 * How much of the town's population is present, 0.1 to 1.
	 *
	 * A performance dial, not a taste one: the map ships with enough people to
	 * fill it on the target GPU, and this is what a weaker machine turns down.
	 * The same inhabitants are hidden at the same setting every run.
	 */
	UFUNCTION(BlueprintPure, Category = "UEGT2|Gameplay") float GetCrowdDensity() const { return CrowdDensity; }
	UFUNCTION(BlueprintCallable, Category = "UEGT2|Gameplay") void SetCrowdDensity(float Value);

	// ---- Control rebinds --------------------------------------------------
	/** Key override for an input action, or an invalid key when unbound. */
	UFUNCTION(BlueprintPure, Category = "UEGT2|Controls")
	FKey GetKeyOverride(FName ActionName) const;

	UFUNCTION(BlueprintCallable, Category = "UEGT2|Controls")
	void SetKeyOverride(FName ActionName, FKey Key);

	UFUNCTION(BlueprintCallable, Category = "UEGT2|Controls")
	void ClearKeyOverrides();

	/** Sensible defaults for an RTX 3060-class GPU at 1080p. */
	UFUNCTION(BlueprintCallable, Category = "UEGT2|Settings")
	void ApplyRecommendedDefaults();

private:
	void ApplyConsoleVariables() const;
	void ApplyAudioSettings() const;

	// Graphics
	UPROPERTY(Config) float FieldOfView = 90.0f;
	UPROPERTY(Config) bool bMotionBlur = false;
	UPROPERTY(Config) bool bBloom = true;
	UPROPERTY(Config) float ResolutionScalePercent = 100.0f;
	UPROPERTY(Config) float Brightness = 1.0f;
	UPROPERTY(Config) int32 FoliageDrawDistanceLevel = 2;

	// Audio, indexed by EUEGT2AudioBus
	UPROPERTY(Config) TArray<float> AudioVolumes;

	// Gameplay
	UPROPERTY(Config) float MouseSensitivity = 1.0f;
	UPROPERTY(Config) bool bInvertLookY = false;
	UPROPERTY(Config) float HeadBobScale = 1.0f;
	UPROPERTY(Config) bool bToggleSprint = false;
	UPROPERTY(Config) bool bShowCrosshair = true;
	UPROPERTY(Config) bool bShowInteractPrompts = true;
	UPROPERTY(Config) bool bShowSpeechBubbles = true;
	UPROPERTY(Config) bool bShowAlmanac = true;
	UPROPERTY(Config) bool bShowNeeds = true;
	UPROPERTY(Config) bool bUseFahrenheit = false;
	UPROPERTY(Config) float CrowdDensity = 1.0f;

	// Controls
	UPROPERTY(Config) TMap<FName, FKey> KeyOverrides;
};
