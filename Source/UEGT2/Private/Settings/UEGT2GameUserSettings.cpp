#include "Settings/UEGT2GameUserSettings.h"

#include "Engine/Engine.h"
#include "HAL/IConsoleManager.h"
#include "Sound/SoundClass.h"
#include "UEGT2LogChannels.h"

FUEGT2SettingsApplied UUEGT2GameUserSettings::OnSettingsApplied;

namespace UEGT2SettingsLocal
{
	/** Sound class assets created by the content build. Missing assets are tolerated. */
	const TCHAR* AudioBusSoundClassPaths[] = {
		TEXT("/Game/Fairhaven/Audio/SC_Master"),
		TEXT("/Game/Fairhaven/Audio/SC_Effects"),
		TEXT("/Game/Fairhaven/Audio/SC_Ambience"),
		TEXT("/Game/Fairhaven/Audio/SC_Music"),
		TEXT("/Game/Fairhaven/Audio/SC_UI"),
	};

	void SetCVar(const TCHAR* Name, float Value)
	{
		if (IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Name))
		{
			Variable->Set(Value, ECVF_SetByGameSetting);
		}
	}
}

UUEGT2GameUserSettings::UUEGT2GameUserSettings()
{
	AudioVolumes.Init(1.0f, static_cast<int32>(EUEGT2AudioBus::Count));
	AudioVolumes[static_cast<int32>(EUEGT2AudioBus::Music)] = 0.6f;
}

UUEGT2GameUserSettings* UUEGT2GameUserSettings::Get()
{
	return GEngine ? Cast<UUEGT2GameUserSettings>(GEngine->GetGameUserSettings()) : nullptr;
}

void UUEGT2GameUserSettings::SetToDefaults()
{
	const bool bPersistenceChanged = !bSaveProgressEnabled || bAutosaveEnabled;
	Super::SetToDefaults();

	FieldOfView = 90.0f;
	bMotionBlur = false;
	bBloom = true;
	ResolutionScalePercent = 100.0f;
	Brightness = 1.0f;
	FoliageDrawDistanceLevel = 2;

	AudioVolumes.Init(1.0f, static_cast<int32>(EUEGT2AudioBus::Count));
	AudioVolumes[static_cast<int32>(EUEGT2AudioBus::Music)] = 0.6f;

	MouseSensitivity = 1.0f;
	bInvertLookY = false;
	HeadBobScale = 1.0f;
	HudSizeLevel = 0;
	bToggleSprint = false;
	bAutoWalkEnabled = false;
	bShowCrosshair = true;
	bShowInteractPrompts = true;
	bShowSpeechBubbles = true;
	bShowAlmanac = true;
	bShowNeeds = true;
	bSaveProgressEnabled = true;
	bAutosaveEnabled = false;
	bSurveyJournalEnabled = true;
	bSleepUntilEnabled = true;
	bUseFahrenheit = false;
	CrowdDensity = 1.0f;
	KeyOverrides.Empty();
	if (bPersistenceChanged) { ++PersistenceRevision; }
}

void UUEGT2GameUserSettings::ApplyNonResolutionSettings()
{
	Super::ApplyNonResolutionSettings();

	if (AudioVolumes.Num() < static_cast<int32>(EUEGT2AudioBus::Count))
	{
		AudioVolumes.SetNumZeroed(static_cast<int32>(EUEGT2AudioBus::Count));
	}

	ApplyConsoleVariables();
	ApplyAudioSettings();

	UE_LOG(LogUEGT2Settings, Log,
		TEXT("Settings applied: fov=%.0f resScale=%.0f%% quality(view=%d shadow=%d gi=%d refl=%d pp=%d tex=%d fx=%d foliage=%d) master=%.2f progress=%s journal=%s sleepUntil=%s autosave=%s hud=%.0f%% autoWalk=%s"),
		FieldOfView, ResolutionScalePercent,
		GetViewDistanceQuality(), GetShadowQuality(), GetGlobalIlluminationQuality(),
		GetReflectionQuality(), GetPostProcessingQuality(), GetTextureQuality(),
		GetVisualEffectQuality(), GetFoliageQuality(),
		GetAudioVolume(EUEGT2AudioBus::Master), bSaveProgressEnabled ? TEXT("on") : TEXT("off"),
		bSurveyJournalEnabled ? TEXT("on") : TEXT("off"), bSleepUntilEnabled ? TEXT("on") : TEXT("off"),
		bAutosaveEnabled ? TEXT("on") : TEXT("off"), GetHudScale() * 100.0f, bAutoWalkEnabled ? TEXT("on") : TEXT("off"));

	OnSettingsApplied.Broadcast();
}

void UUEGT2GameUserSettings::ApplyConsoleVariables() const
{
	using UEGT2SettingsLocal::SetCVar;
	SetCVar(TEXT("r.MotionBlurQuality"), bMotionBlur ? 3.0f : 0.0f);
	SetCVar(TEXT("r.BloomQuality"), bBloom ? 5.0f : 0.0f);
	SetCVar(TEXT("r.ScreenPercentage"), FMath::Clamp(ResolutionScalePercent, 50.0f, 100.0f));

	// Brightness rides on the post-process exposure bias.
	SetCVar(TEXT("r.EyeAdaptation.ExposureOffset"), FMath::Loge(FMath::Max(Brightness, 0.1f)) / FMath::Loge(2.0f));
}

void UUEGT2GameUserSettings::ApplyAudioSettings() const
{
	for (int32 Index = 0; Index < static_cast<int32>(EUEGT2AudioBus::Count); ++Index)
	{
		const EUEGT2AudioBus Bus = static_cast<EUEGT2AudioBus>(Index);
		USoundClass* SoundClass = LoadObject<USoundClass>(nullptr, UEGT2SettingsLocal::AudioBusSoundClassPaths[Index]);
		if (!SoundClass)
		{
			continue;
		}
		// The generated classes are children of Master. Unreal propagates its
		// volume down that tree; multiplying it here too would square the slider.
		SoundClass->Properties.Volume = FMath::Clamp(GetAudioVolume(Bus), 0.0f, 1.0f);
	}
}

// ---- Graphics --------------------------------------------------------------
void UUEGT2GameUserSettings::SetFieldOfView(float Value) { FieldOfView = FMath::Clamp(Value, 60.0f, 120.0f); }
void UUEGT2GameUserSettings::SetMotionBlurEnabled(bool bValue) { bMotionBlur = bValue; }
void UUEGT2GameUserSettings::SetBloomEnabled(bool bValue) { bBloom = bValue; }
void UUEGT2GameUserSettings::SetResolutionScalePercent(float Value) { ResolutionScalePercent = FMath::Clamp(Value, 50.0f, 100.0f); }
void UUEGT2GameUserSettings::SetBrightness(float Value) { Brightness = FMath::Clamp(Value, 0.5f, 2.0f); }
void UUEGT2GameUserSettings::SetFoliageDrawDistanceLevel(int32 Value) { FoliageDrawDistanceLevel = FMath::Clamp(Value, 0, 3); }

float UUEGT2GameUserSettings::GetFoliageDrawDistanceScale() const
{
	static const float Scales[] = { 0.5f, 0.75f, 1.0f, 1.5f };
	return Scales[FMath::Clamp(FoliageDrawDistanceLevel, 0, UE_ARRAY_COUNT(Scales) - 1)];
}

// ---- Audio -----------------------------------------------------------------
float UUEGT2GameUserSettings::GetAudioVolume(EUEGT2AudioBus Bus) const
{
	const int32 Index = static_cast<int32>(Bus);
	return AudioVolumes.IsValidIndex(Index) ? AudioVolumes[Index] : 1.0f;
}

void UUEGT2GameUserSettings::SetAudioVolume(EUEGT2AudioBus Bus, float Value)
{
	const int32 Index = static_cast<int32>(Bus);
	if (AudioVolumes.Num() < static_cast<int32>(EUEGT2AudioBus::Count))
	{
		AudioVolumes.SetNumZeroed(static_cast<int32>(EUEGT2AudioBus::Count));
	}
	if (AudioVolumes.IsValidIndex(Index))
	{
		AudioVolumes[Index] = FMath::Clamp(Value, 0.0f, 1.0f);
	}
}

FText UUEGT2GameUserSettings::GetAudioBusDisplayName(EUEGT2AudioBus Bus)
{
	switch (Bus)
	{
	case EUEGT2AudioBus::Master:   return NSLOCTEXT("UEGT2", "AudioMaster", "Master");
	case EUEGT2AudioBus::Effects:  return NSLOCTEXT("UEGT2", "AudioEffects", "Effects");
	case EUEGT2AudioBus::Ambience: return NSLOCTEXT("UEGT2", "AudioAmbience", "Ambience");
	case EUEGT2AudioBus::Music:    return NSLOCTEXT("UEGT2", "AudioMusic", "Music");
	case EUEGT2AudioBus::UI:       return NSLOCTEXT("UEGT2", "AudioUI", "Interface");
	default:                       return FText::GetEmpty();
	}
}

// ---- Gameplay --------------------------------------------------------------
void UUEGT2GameUserSettings::SetMouseSensitivity(float Value) { MouseSensitivity = FMath::Clamp(Value, 0.1f, 4.0f); }
void UUEGT2GameUserSettings::SetInvertLookY(bool bValue) { bInvertLookY = bValue; }
void UUEGT2GameUserSettings::SetHeadBobScale(float Value) { HeadBobScale = FMath::Clamp(Value, 0.0f, 2.0f); }
void UUEGT2GameUserSettings::SetHudSizeLevel(int32 Value) { HudSizeLevel = FMath::Clamp(Value, 0, 2); }

float UUEGT2GameUserSettings::GetHudScale() const
{
	static const float Scales[] = { 1.0f, 1.25f, 1.5f };
	return Scales[GetHudSizeLevel()];
}

void UUEGT2GameUserSettings::SetToggleSprint(bool bValue) { bToggleSprint = bValue; }
void UUEGT2GameUserSettings::SetAutoWalkEnabled(bool bValue) { bAutoWalkEnabled = bValue; }
void UUEGT2GameUserSettings::SetShowCrosshair(bool bValue) { bShowCrosshair = bValue; }
void UUEGT2GameUserSettings::SetShowInteractPrompts(bool bValue) { bShowInteractPrompts = bValue; }
void UUEGT2GameUserSettings::SetShowSpeechBubbles(bool bValue) { bShowSpeechBubbles = bValue; }
void UUEGT2GameUserSettings::SetShowNeeds(bool bValue) { bShowNeeds = bValue; }
void UUEGT2GameUserSettings::SetSaveProgressEnabled(bool bValue)
{
	if (bSaveProgressEnabled != bValue) { bSaveProgressEnabled = bValue; ++PersistenceRevision; }
}
void UUEGT2GameUserSettings::SetAutosaveEnabled(bool bValue)
{
	if (bAutosaveEnabled != bValue) { bAutosaveEnabled = bValue; ++PersistenceRevision; }
}
void UUEGT2GameUserSettings::SetSurveyJournalEnabled(bool bValue) { bSurveyJournalEnabled = bValue; }
void UUEGT2GameUserSettings::SetSleepUntilEnabled(bool bValue) { bSleepUntilEnabled = bValue; }

void UUEGT2GameUserSettings::SetShowAlmanac(bool bValue)
{
	bShowAlmanac = bValue;
}

void UUEGT2GameUserSettings::SetUseFahrenheit(bool bValue)
{
	bUseFahrenheit = bValue;
}
void UUEGT2GameUserSettings::SetCrowdDensity(float Value) { CrowdDensity = FMath::Clamp(Value, 0.1f, 1.0f); }

// ---- Controls --------------------------------------------------------------
FKey UUEGT2GameUserSettings::GetKeyOverride(FName ActionName) const
{
	if (const FKey* Found = KeyOverrides.Find(ActionName))
	{
		return *Found;
	}
	return FKey();
}

void UUEGT2GameUserSettings::SetKeyOverride(FName ActionName, FKey Key)
{
	if (Key.IsValid())
	{
		KeyOverrides.Add(ActionName, Key);
	}
	else
	{
		KeyOverrides.Remove(ActionName);
	}
	UE_LOG(LogUEGT2Settings, Log, TEXT("Rebound '%s' to '%s'."),
		*ActionName.ToString(), *Key.ToString());
}

void UUEGT2GameUserSettings::ClearKeyOverrides()
{
	KeyOverrides.Empty();
	UE_LOG(LogUEGT2Settings, Log, TEXT("Control rebinds reset to defaults."));
}

void UUEGT2GameUserSettings::ApplyRecommendedDefaults()
{
	// Tuned for an RTX 3060-class GPU at 1920x1080 with Lumen and VSM on.
	SetViewDistanceQuality(2);
	SetShadowQuality(2);
	SetGlobalIlluminationQuality(2);
	SetReflectionQuality(2);
	SetPostProcessingQuality(2);
	SetTextureQuality(3);
	SetVisualEffectQuality(2);
	SetFoliageQuality(2);
	SetShadingQuality(2);
	SetAntiAliasingQuality(2);
	SetResolutionScalePercent(100.0f);
	SetFrameRateLimit(0.0f);
	SetVSyncEnabled(false);
	FoliageDrawDistanceLevel = 2;

	UE_LOG(LogUEGT2Settings, Log, TEXT("Recommended defaults applied (RTX 3060-class, 1080p)."));
}
