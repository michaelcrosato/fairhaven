#include "World/UEGT2SkyController.h"

#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/VolumetricCloudComponent.h"
#include "Diagnostics/UEGT2CaptureSubsystem.h"
#include "Engine/DirectionalLight.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/SkyLight.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "UEGT2LogChannels.h"

// Named rather than anonymous: SUEGT2Menu.cpp leaks a file-scope using-directive
// into whatever a unity build concatenates after it, and file-local helpers at
// global scope are how that turns into an ambiguous-symbol error.
namespace UEGT2Sky
{
	/** Tag on the runtime-spawned moon, so CacheSkyActors never mistakes it for the sun. */
	const FName MoonTag(TEXT("UEGT2Moon"));

	/** Warm sunrise -> neutral noon -> warm sunset. */
	FLinearColor SunColourForElevation(float ElevationDegrees)
	{
		const float T = FMath::Clamp(ElevationDegrees / 35.0f, 0.0f, 1.0f);
		const FLinearColor Low(1.0f, 0.62f, 0.34f);
		const FLinearColor High(1.0f, 0.96f, 0.90f);
		return FMath::Lerp(Low, High, T);
	}

	FLinearColor FogColourForElevation(float ElevationDegrees)
	{
		const float T = FMath::Clamp((ElevationDegrees + 6.0f) / 34.0f, 0.0f, 1.0f);
		const FLinearColor Night(0.03f, 0.04f, 0.09f);
		const FLinearColor Day(0.55f, 0.68f, 0.82f);
		return FMath::Lerp(Night, Day, T);
	}

	/** Cool ambient once the sun has gone; keeps night blue rather than grey. */
	FLinearColor SkyColourForElevation(float ElevationDegrees)
	{
		const float T = FMath::Clamp((ElevationDegrees + 4.0f) / 20.0f, 0.0f, 1.0f);
		const FLinearColor Night(0.34f, 0.45f, 0.80f);
		const FLinearColor Day(1.0f, 1.0f, 1.0f);
		return FMath::Lerp(Night, Day, T);
	}

	/** Smoothstep between two edges; 0 below Lo, 1 above Hi. */
	float SmoothBand(float Value, float Lo, float Hi)
	{
		const float T = FMath::Clamp((Value - Lo) / FMath::Max(Hi - Lo, KINDA_SMALL_NUMBER), 0.0f, 1.0f);
		return T * T * (3.0f - 2.0f * T);
	}

	/** Pull Colour toward Tint by Strength. Strength 0 leaves it alone. */
	FLinearColor ApplyTint(const FLinearColor& Colour, const FLinearColor& Tint, float Strength)
	{
		return FMath::Lerp(Colour, Tint, FMath::Clamp(Strength, 0.0f, 1.0f));
	}
}

AUEGT2SkyController::AUEGT2SkyController()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	bIsEditorOnlyActor = false;
}

AUEGT2SkyController* AUEGT2SkyController::Get(const UWorld* World)
{
	if (!World)
	{
		return nullptr;
	}
	for (TActorIterator<AUEGT2SkyController> It(const_cast<UWorld*>(World)); It; ++It)
	{
		return *It;
	}
	return nullptr;
}

void AUEGT2SkyController::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	RefreshSky();
}

void AUEGT2SkyController::BeginPlay()
{
	Super::BeginPlay();

	// A moving sun would make Screenshot-Tour and Smoke-Packaged produce a
	// different image on every run, which is the whole reason the map ships
	// with the sun frozen. Captures keep the old behaviour exactly.
	const bool bCapturing = UUEGT2CaptureSubsystem::IsCaptureRequested()
		|| UUEGT2CaptureSubsystem::IsWalkSmokeRequested();

	if (bCapturing)
	{
		bDayNightCycleEnabled = false;
		DayLengthMinutes = 0.0f;
		UE_LOG(LogUEGT2World, Log, TEXT("Capture run: day/night cycle frozen at %.2f."), TimeOfDay);
	}
	else if (bDayNightCycleEnabled && DayLengthMinutes <= KINDA_SMALL_NUMBER)
	{
		// The placed actor serialises 0 from lighting.py, so a C++ default on
		// DayLengthMinutes would never be seen. Promote it here instead.
		DayLengthMinutes = DefaultDayLengthMinutes;
	}

	// -UEGT2Time= and -UEGT2Weather= let a headless capture render a specific
	// sky. Both freeze the cycle, so a night or storm shot is as reproducible
	// as the default one.
	float ForcedTime = 0.0f;
	if (FParse::Value(FCommandLine::Get(), TEXT("UEGT2Time="), ForcedTime))
	{
		TimeOfDay = FMath::Fmod(FMath::Fmod(ForcedTime, 24.0f) + 24.0f, 24.0f);
		bDayNightCycleEnabled = false;
		DayLengthMinutes = 0.0f;
		UE_LOG(LogUEGT2World, Log, TEXT("Time forced to %.2f from the command line."), TimeOfDay);
	}

	FString ForcedWeather;
	if (FParse::Value(FCommandLine::Get(), TEXT("UEGT2Weather="), ForcedWeather))
	{
		EUEGT2Weather Parsed = EUEGT2Weather::Clear;
		if (ParseWeatherName(ForcedWeather, Parsed))
		{
			Weather = Parsed;
			UE_LOG(LogUEGT2World, Log, TEXT("Weather forced to %s from the command line."),
				*GetWeatherDisplayName(Weather).ToString());
		}
		else
		{
			UE_LOG(LogUEGT2World, Warning, TEXT("Unknown -UEGT2Weather=%s."), *ForcedWeather);
		}
	}

	EnsureMoon();
	RefreshSky();

	UE_LOG(LogUEGT2World, Log, TEXT("Sky ready: time=%.2f cycle=%s dayLength=%.1fmin weather=%s"),
		TimeOfDay, IsDayNightCycleEnabled() ? TEXT("on") : TEXT("off"), DayLengthMinutes,
		*GetWeatherDisplayName(Weather).ToString());

	static FAutoConsoleCommandWithWorldAndArgs TimeCommand(
		TEXT("uegt2.Time"),
		TEXT("Set the time of day in hours, e.g. uegt2.Time 18.5"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
			[](const TArray<FString>& Args, UWorld* World)
			{
				if (Args.Num() < 1 || !World)
				{
					return;
				}
				const float Hours = FCString::Atof(*Args[0]);
				for (TActorIterator<AUEGT2SkyController> It(World); It; ++It)
				{
					It->SetTimeOfDay(Hours);
				}
			}));

	static FAutoConsoleCommandWithWorldAndArgs SpeedCommand(
		TEXT("uegt2.TimeSpeed"),
		TEXT("Real minutes per in-game day. 0 freezes the sun."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
			[](const TArray<FString>& Args, UWorld* World)
			{
				if (Args.Num() < 1 || !World)
				{
					return;
				}
				const float Minutes = FCString::Atof(*Args[0]);
				for (TActorIterator<AUEGT2SkyController> It(World); It; ++It)
				{
					It->SetDayLengthMinutes(Minutes);
				}
			}));

	static FAutoConsoleCommandWithWorldAndArgs WeatherCommand(
		TEXT("uegt2.Weather"),
		TEXT("Set the weather: clear, cloudy, overcast, foggy or storm."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
			[](const TArray<FString>& Args, UWorld* World)
			{
				if (Args.Num() < 1 || !World)
				{
					return;
				}
				EUEGT2Weather Parsed = EUEGT2Weather::Clear;
				if (!ParseWeatherName(Args[0], Parsed))
				{
					UE_LOG(LogUEGT2World, Warning,
						TEXT("Unknown weather '%s'. Try clear, cloudy, overcast, foggy or storm."), *Args[0]);
					return;
				}
				for (TActorIterator<AUEGT2SkyController> It(World); It; ++It)
				{
					It->SetWeather(Parsed);
				}
			}));
}

void AUEGT2SkyController::CacheSkyActors()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	if (!Sun)
	{
		for (TActorIterator<ADirectionalLight> It(World); It; ++It)
		{
			// Skip our own moon, or a re-cache would promote it to sun and the
			// world would be lit by a 0.55 lux light.
			if (It->ActorHasTag(UEGT2Sky::MoonTag))
			{
				continue;
			}
			Sun = *It;
			break;
		}
	}
	if (!Moon)
	{
		for (TActorIterator<ADirectionalLight> It(World); It; ++It)
		{
			if (It->ActorHasTag(UEGT2Sky::MoonTag)) { Moon = *It; break; }
		}
	}
	if (!SkyLight)
	{
		for (TActorIterator<ASkyLight> It(World); It; ++It) { SkyLight = *It; break; }
	}
	if (!Fog)
	{
		for (TActorIterator<AExponentialHeightFog> It(World); It; ++It) { Fog = *It; break; }
	}
	if (!Clouds)
	{
		for (TActorIterator<AVolumetricCloud> It(World); It; ++It) { Clouds = *It; break; }
	}
	if (!PostProcess)
	{
		for (TActorIterator<APostProcessVolume> It(World); It; ++It) { PostProcess = *It; break; }
	}
}

void AUEGT2SkyController::EnsureMoon()
{
	UWorld* World = GetWorld();
	if (!World || Moon)
	{
		return;
	}
	CacheSkyActors();
	if (Moon)
	{
		return;
	}

	// Spawned rather than placed by the content build: the map is a build
	// artifact, and a runtime actor cannot be destroyed by the next one.
	FActorSpawnParameters Params;
	Params.ObjectFlags |= RF_Transient;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	ADirectionalLight* NewMoon = World->SpawnActor<ADirectionalLight>(
		ADirectionalLight::StaticClass(), FTransform::Identity, Params);
	if (!NewMoon)
	{
		UE_LOG(LogUEGT2World, Warning, TEXT("Could not spawn the moon; nights will be unlit."));
		return;
	}

	NewMoon->Tags.Add(UEGT2Sky::MoonTag);
	NewMoon->SetMobility(EComponentMobility::Movable);
	if (UDirectionalLightComponent* Light = Cast<UDirectionalLightComponent>(NewMoon->GetLightComponent()))
	{
		Light->SetIntensity(0.0f);
		Light->SetLightColor(MoonColour);
		// Index 1 is the atmosphere's second light, which is what SkyAtmosphere
		// treats as the moon.
		Light->SetAtmosphereSunLight(true);
		Light->SetAtmosphereSunLightIndex(1);
	}
	Moon = NewMoon;
	UE_LOG(LogUEGT2World, Log, TEXT("Moon spawned for the day/night cycle."));
}

void AUEGT2SkyController::RefreshSky()
{
	CacheSkyActors();
	ApplySky();
}

void AUEGT2SkyController::SetTimeOfDay(float Hours)
{
	TimeOfDay = FMath::Fmod(FMath::Fmod(Hours, 24.0f) + 24.0f, 24.0f);
	ApplySky();
	UE_LOG(LogUEGT2World, Log, TEXT("Time of day set to %.2f."), TimeOfDay);
}

void AUEGT2SkyController::SetDayNightCycleEnabled(bool bEnabled)
{
	bDayNightCycleEnabled = bEnabled;
	if (bEnabled && DayLengthMinutes <= KINDA_SMALL_NUMBER)
	{
		DayLengthMinutes = DefaultDayLengthMinutes;
	}
	UE_LOG(LogUEGT2World, Log, TEXT("Day/night cycle %s (%.1f min/day)."),
		bEnabled ? TEXT("on") : TEXT("off"), DayLengthMinutes);
}

void AUEGT2SkyController::SetDayLengthMinutes(float Minutes)
{
	DayLengthMinutes = FMath::Max(Minutes, 0.0f);
	// uegt2.TimeSpeed 0 has always meant "freeze", so keep the master switch in
	// step rather than leaving a cycle that is enabled but never advances.
	bDayNightCycleEnabled = DayLengthMinutes > KINDA_SMALL_NUMBER;
}

void AUEGT2SkyController::SetWeather(EUEGT2Weather NewWeather)
{
	Weather = NewWeather;
	ApplySky();
	UE_LOG(LogUEGT2World, Log, TEXT("Weather set to %s."), *GetWeatherDisplayName(Weather).ToString());
}

void AUEGT2SkyController::SetFogDensityOverride(float Density)
{
	FogDensityOverride = Density;
	ApplySky();
}

float AUEGT2SkyController::GetEffectiveFogDensity() const
{
	return FogDensityOverride >= 0.0f ? FogDensityOverride : GetWeatherPreset(Weather).FogDensity;
}

float AUEGT2SkyController::SunElevationDegrees() const
{
	const float DayFraction = (TimeOfDay - 6.0f) / 12.0f;
	return FMath::Sin(DayFraction * PI) * MaxSunElevation;
}

bool AUEGT2SkyController::IsNight() const
{
	return SunElevationDegrees() < -1.0f;
}

void AUEGT2SkyController::ApplySky()
{
	// Sunrise at 06:00 in the east, sunset at 18:00 in the west, passing south.
	const float DayFraction = (TimeOfDay - 6.0f) / 12.0f;
	const float Elevation = SunElevationDegrees();
	const float Azimuth = 90.0f + DayFraction * 180.0f;

	// Smoothstepped rather than a linear clamp so dawn and dusk read as a
	// sunrise instead of a fade, and so the sun is genuinely off below the
	// horizon rather than sitting on an arbitrary 2% floor.
	const float SunAlpha = UEGT2Sky::SmoothBand(Elevation, -2.0f, 12.0f);
	const float AmbientAlpha = UEGT2Sky::SmoothBand(Elevation, -8.0f, 14.0f);
	const FUEGT2WeatherPreset& Preset = GetWeatherPreset(Weather);

	if (Sun)
	{
		Sun->SetActorRotation(FRotator(-Elevation, Azimuth + 180.0f, 0.0f));
		// ADirectionalLight::GetComponent() is editor-only; go through ALight.
		if (UDirectionalLightComponent* Light = Cast<UDirectionalLightComponent>(Sun->GetLightComponent()))
		{
			Light->SetIntensity(NoonIntensity * SunAlpha * Preset.SunIntensityScale);
			Light->SetLightColor(UEGT2Sky::ApplyTint(
				UEGT2Sky::SunColourForElevation(Elevation), Preset.SunTint, Preset.SunTintStrength));
			Light->SetAtmosphereSunLight(true);
			Light->SetAtmosphereSunLightIndex(0);
		}
	}

	if (Moon)
	{
		// Opposite side of the sky from the sun, fading in as the sun leaves.
		const float MoonElevation = -Elevation;
		const float MoonAlpha = UEGT2Sky::SmoothBand(-Elevation, -6.0f, 8.0f);
		Moon->SetActorRotation(FRotator(-MoonElevation, Azimuth, 0.0f));
		if (UDirectionalLightComponent* Light = Cast<UDirectionalLightComponent>(Moon->GetLightComponent()))
		{
			// Weather dims the moon too, or a storm at midnight would be brighter
			// than the same storm at noon.
			Light->SetIntensity(MoonIntensity * MoonAlpha * Preset.SunIntensityScale);
			Light->SetLightColor(MoonColour);
		}
	}

	if (SkyLight)
	{
		if (USkyLightComponent* Component = SkyLight->GetLightComponent())
		{
			Component->SetIntensity(
				FMath::Lerp(0.20f, 1.05f, AmbientAlpha) * Preset.SkyLightScale);
			Component->SetLightColor(UEGT2Sky::SkyColourForElevation(Elevation));
		}
	}

	if (Fog)
	{
		if (UExponentialHeightFogComponent* Component =
				Fog->FindComponentByClass<UExponentialHeightFogComponent>())
		{
			Component->SetFogInscatteringColor(UEGT2Sky::ApplyTint(
				UEGT2Sky::FogColourForElevation(Elevation), Preset.FogTint, Preset.FogTintStrength));
			Component->SetFogDensity(GetEffectiveFogDensity());
			Component->SetFogMaxOpacity(Preset.FogMaxOpacity);
		}
	}

	if (PostProcess)
	{
		// Slide the whole exposure window down as the sun goes, rather than
		// letting the map's daylight floor clamp the night to black.
		PostProcess->Settings.bOverride_AutoExposureMinBrightness = true;
		PostProcess->Settings.AutoExposureMinBrightness =
			FMath::Lerp(NightExposureMinEV, DayExposureMinEV, AmbientAlpha);
		PostProcess->Settings.bOverride_AutoExposureMaxBrightness = true;
		PostProcess->Settings.AutoExposureMaxBrightness =
			FMath::Lerp(NightExposureMaxEV, DayExposureMaxEV, AmbientAlpha);
	}

	if (Clouds)
	{
		if (UVolumetricCloudComponent* Component =
				Clouds->FindComponentByClass<UVolumetricCloudComponent>())
		{
			// There is no coverage parameter on the component - cloud density
			// lives in the cloud material - so a lower deck is what carries
			// "overcast" and "storm".
			Component->SetLayerBottomAltitude(Preset.CloudBottomAltitudeKm);
		}
	}
}

void AUEGT2SkyController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bDayNightCycleEnabled || DayLengthMinutes <= KINDA_SMALL_NUMBER)
	{
		return;
	}
	const float HoursPerSecond = 24.0f / (DayLengthMinutes * 60.0f);
	TimeOfDay = FMath::Fmod(TimeOfDay + HoursPerSecond * DeltaSeconds, 24.0f);
	ApplySky();
}
