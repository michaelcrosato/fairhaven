#include "World/UEGT2SkyController.h"

#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/SkyLightComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/SkyLight.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "UEGT2LogChannels.h"

namespace
{
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
		const FLinearColor Night(0.05f, 0.07f, 0.12f);
		const FLinearColor Day(0.55f, 0.68f, 0.82f);
		return FMath::Lerp(Night, Day, T);
	}
}

AUEGT2SkyController::AUEGT2SkyController()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	bIsEditorOnlyActor = false;
}

void AUEGT2SkyController::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	RefreshSky();
}

void AUEGT2SkyController::BeginPlay()
{
	Super::BeginPlay();
	RefreshSky();

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
					It->DayLengthMinutes = FMath::Max(Minutes, 0.0f);
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
		for (TActorIterator<ADirectionalLight> It(World); It; ++It) { Sun = *It; break; }
	}
	if (!SkyLight)
	{
		for (TActorIterator<ASkyLight> It(World); It; ++It) { SkyLight = *It; break; }
	}
	if (!Fog)
	{
		for (TActorIterator<AExponentialHeightFog> It(World); It; ++It) { Fog = *It; break; }
	}
}

void AUEGT2SkyController::RefreshSky()
{
	CacheSkyActors();
	ApplyTimeOfDay();
}

void AUEGT2SkyController::SetTimeOfDay(float Hours)
{
	TimeOfDay = FMath::Fmod(FMath::Max(Hours, 0.0f), 24.0f);
	ApplyTimeOfDay();
	UE_LOG(LogUEGT2World, Log, TEXT("Time of day set to %.2f."), TimeOfDay);
}

void AUEGT2SkyController::ApplyTimeOfDay()
{
	// Sunrise at 06:00 in the east, sunset at 18:00 in the west, passing south.
	const float DayFraction = (TimeOfDay - 6.0f) / 12.0f;
	const float Elevation = FMath::Sin(DayFraction * PI) * MaxSunElevation;
	const float Azimuth = 90.0f + DayFraction * 180.0f;
	const float DayAlpha = FMath::Clamp(Elevation / 12.0f, 0.0f, 1.0f);

	if (Sun)
	{
		Sun->SetActorRotation(FRotator(-Elevation, Azimuth + 180.0f, 0.0f));
		// ADirectionalLight::GetComponent() is editor-only; go through ALight.
		if (UDirectionalLightComponent* Light = Cast<UDirectionalLightComponent>(Sun->GetLightComponent()))
		{
			Light->SetIntensity(NoonIntensity * FMath::Max(DayAlpha, 0.02f));
			Light->SetLightColor(SunColourForElevation(Elevation));
			Light->SetAtmosphereSunLight(true);
		}
	}

	if (SkyLight)
	{
		if (USkyLightComponent* Component = SkyLight->GetLightComponent())
		{
			Component->SetIntensity(FMath::Lerp(0.12f, 1.05f, DayAlpha));
		}
	}

	if (Fog)
	{
		if (UExponentialHeightFogComponent* Component =
				Fog->FindComponentByClass<UExponentialHeightFogComponent>())
		{
			Component->SetFogInscatteringColor(FogColourForElevation(Elevation));
		}
	}
}

void AUEGT2SkyController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (DayLengthMinutes <= KINDA_SMALL_NUMBER)
	{
		return;
	}
	const float HoursPerSecond = 24.0f / (DayLengthMinutes * 60.0f);
	TimeOfDay = FMath::Fmod(TimeOfDay + HoursPerSecond * DeltaSeconds, 24.0f);
	ApplyTimeOfDay();
}
