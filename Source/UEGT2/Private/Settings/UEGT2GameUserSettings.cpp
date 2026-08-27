#include "Settings/UEGT2GameUserSettings.h"

#include "Engine/Engine.h"
#include "HAL/IConsoleManager.h"
#include "Sound/SoundClass.h"
#include "UEGT2LogChannels.h"
#include "UObject/ConstructorHelpers.h"

FUEGT2SettingsApplied UUEGT2GameUserSettings::OnSettingsApplied;

namespace
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
	bToggleSprint = false;
	bShowCrosshair = true;
	bShowInteractPrompts = true;
	KeyOverrides.Empty();
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
		TEXT("Settings applied: fov=%.0f resScale=%.0f%% quality(view=%d shadow=%d gi=%d refl=%d pp=%d tex=%d fx=%d foliage=%d) master=%.2f"),
		FieldOfView, ResolutionScalePercent,
		GetViewDistanceQuality(), GetShadowQuality(), GetGlobalIlluminationQuality(),
		GetReflectionQuality(), GetPostProcessingQuality(), GetTextureQuality(),
		GetVisualEffectQuality(), GetFoliageQuality(),
		GetAudioVolume(EUEGT2AudioBus::Master));

	OnSettingsApplied.Broadcast();
}

void UUEGT2GameUserSettings::ApplyConsoleVariables() const
{
	SetCVar(TEXT("r.MotionBlurQuality"), bMotionBlur ? 3.0f : 0.0f);
	SetCVar(TEXT("r.BloomQuality"), bBloom ? 5.0f : 0.0f);
	SetCVar(TEXT("r.ScreenPercentage"), FMath::Clamp(ResolutionScalePercent, 50.0f, 100.0f));

	// Foliage draw distance: scales the culling multiplier used by scatter fields.
	static const float FoliageScales[] = { 0.5f, 0.75f, 1.0f, 1.5f };
	const int32 Index = FMath::Clamp(FoliageDrawDistanceLevel, 0, UE_ARRAY_COUNT(FoliageScales) - 1);
	SetCVar(TEXT("foliage.LODDistanceScale"), FoliageScales[Index]);

	// Brightness rides on the post-process exposure bias.
	SetCVar(TEXT("r.EyeAdaptation.ExposureOffset"), FMath::Loge(FMath::Max(Brightness, 0.1f)) / FMath::Loge(2.0f));
}

void UUEGT2GameUserSettings::ApplyAudioSettings() const
{
	const float Master = GetAudioVolume(EUEGT2AudioBus::Master);
	for (int32 Index = 0; Index < static_cast<int32>(EUEGT2AudioBus::Count); ++Index)
	{
		const EUEGT2AudioBus Bus = static_cast<EUEGT2AudioBus>(Index);
		USoundClass* SoundClass = LoadObject<USoundClass>(nullptr, AudioBusSoundClassPaths[Index]);
		if (!SoundClass)
		{
			continue;
		}
		const float Volume = (Bus == EUEGT2AudioBus::Master)
			? Master
			: Master * GetAudioVolume(Bus);
		SoundClass->Properties.Volume = FMath::Clamp(Volume, 0.0f, 1.0f);
	}
}

// ---- Graphics --------------------------------------------------------------
void UUEGT2GameUserSettings::SetFieldOfView(float Value) { FieldOfView = FMath::Clamp(Value, 60.0f, 120.0f); }
void UUEGT2GameUserSettings::SetMotionBlurEnabled(bool bValue) { bMotionBlur = bValue; }
void UUEGT2GameUserSettings::SetBloomEnabled(bool bValue) { bBloom = bValue; }
void UUEGT2GameUserSettings::SetResolutionScalePercent(float Value) { ResolutionScalePercent = FMath::Clamp(Value, 50.0f, 100.0f); }
void UUEGT2GameUserSettings::SetBrightness(float Value) { Brightness = FMath::Clamp(Value, 0.5f, 2.0f); }
void UUEGT2GameUserSettings::SetFoliageDrawDistanceLevel(int32 Value) { FoliageDrawDistanceLevel = FMath::Clamp(Value, 0, 3); }

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
void UUEGT2GameUserSettings::SetToggleSprint(bool bValue) { bToggleSprint = bValue; }
void UUEGT2GameUserSettings::SetShowCrosshair(bool bValue) { bShowCrosshair = bValue; }
void UUEGT2GameUserSettings::SetShowInteractPrompts(bool bValue) { bShowInteractPrompts = bValue; }

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
