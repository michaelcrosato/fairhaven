#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"
#include "Settings/UEGT2GameUserSettings.h"
#include "Sound/SoundClass.h"
#include "UObject/UnrealType.h"
#include "World/UEGT2ScatterField.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEGT2SettingsDefaultsTest,
	"UEGT2.Settings.RestoreDefaults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEGT2SettingsDefaultsTest::RunTest(const FString& Parameters)
{
	// Use a separate settings object so a reset test cannot alter the editor's
	// active settings or persist changes to the player's file.
	UUEGT2GameUserSettings* Settings = NewObject<UUEGT2GameUserSettings>();
	Settings->SetFieldOfView(110.0f);
	Settings->SetMotionBlurEnabled(true);
	Settings->SetBloomEnabled(false);
	Settings->SetResolutionScalePercent(70.0f);
	Settings->SetBrightness(1.5f);
	Settings->SetFoliageDrawDistanceLevel(0);
	Settings->SetMouseSensitivity(2.0f);
	Settings->SetInvertLookY(true);
	Settings->SetHeadBobScale(0.0f);
	Settings->SetHudSizeLevel(2);
	Settings->SetToggleSprint(true);
	Settings->SetAutoWalkEnabled(true);
	Settings->SetShowCrosshair(false);
	Settings->SetShowInteractPrompts(false);
	Settings->SetShowSpeechBubbles(false);
	Settings->SetShowAlmanac(false);
	Settings->SetShowNeeds(false);
	Settings->SetSaveProgressEnabled(false);
	Settings->SetAutosaveEnabled(true);
	Settings->SetSurveyJournalEnabled(false);
	Settings->SetNearbyServicesEnabled(false);
	Settings->SetTownSurveyContractEnabled(false);
	Settings->SetSleepUntilEnabled(false);
	Settings->SetUseFahrenheit(true);
	Settings->SetCrowdDensity(0.2f);
	Settings->SetKeyOverride(TEXT("Jump"), EKeys::J);
	for (int32 Index = 0; Index < (int32)EUEGT2AudioBus::Count; ++Index)
	{
		Settings->SetAudioVolume((EUEGT2AudioBus)Index, 0.1f);
	}

	Settings->SetToDefaults();
	TestEqual(TEXT("field of view"), Settings->GetFieldOfView(), 90.0f);
	TestFalse(TEXT("motion blur"), Settings->GetMotionBlurEnabled());
	TestTrue(TEXT("bloom"), Settings->GetBloomEnabled());
	TestEqual(TEXT("resolution scale"), Settings->GetResolutionScalePercent(), 100.0f);
	TestEqual(TEXT("brightness"), Settings->GetBrightness(), 1.0f);
	TestEqual(TEXT("foliage distance"), Settings->GetFoliageDrawDistanceLevel(), 2);
	TestEqual(TEXT("mouse sensitivity"), Settings->GetMouseSensitivity(), 1.0f);
	TestFalse(TEXT("inverted look"), Settings->GetInvertLookY());
	TestEqual(TEXT("head bob"), Settings->GetHeadBobScale(), 1.0f);
	TestEqual(TEXT("normal HUD size"), Settings->GetHudSizeLevel(), 0);
	TestFalse(TEXT("toggle sprint"), Settings->GetToggleSprint());
	TestFalse(TEXT("auto-walk requires opt-in"), Settings->GetAutoWalkEnabled());
	TestTrue(TEXT("crosshair"), Settings->GetShowCrosshair());
	TestTrue(TEXT("interaction prompts"), Settings->GetShowInteractPrompts());
	TestTrue(TEXT("speech bubbles"), Settings->GetShowSpeechBubbles());
	TestTrue(TEXT("almanac"), Settings->GetShowAlmanac());
	TestTrue(TEXT("needs"), Settings->GetShowNeeds());
	TestTrue(TEXT("progress saving"), Settings->GetSaveProgressEnabled());
	TestFalse(TEXT("autosave requires opt-in"), Settings->GetAutosaveEnabled());
	TestTrue(TEXT("survey journal"), Settings->GetSurveyJournalEnabled());
	TestTrue(TEXT("nearby services guide"), Settings->GetNearbyServicesEnabled());
	TestTrue(TEXT("town survey contract"), Settings->GetTownSurveyContractEnabled());
	TestTrue(TEXT("chosen wake times"), Settings->GetSleepUntilEnabled());
	TestFalse(TEXT("Fahrenheit"), Settings->GetUseFahrenheit());
	TestEqual(TEXT("crowd density"), Settings->GetCrowdDensity(), 1.0f);
	TestFalse(TEXT("key override removed"), Settings->GetKeyOverride(TEXT("Jump")).IsValid());
	for (int32 Index = 0; Index < (int32)EUEGT2AudioBus::Count; ++Index)
	{
		const EUEGT2AudioBus Bus = (EUEGT2AudioBus)Index;
		TestEqual(UUEGT2GameUserSettings::GetAudioBusDisplayName(Bus).ToString(),
			Settings->GetAudioVolume(Bus), Bus == EUEGT2AudioBus::Music ? 0.6f : 1.0f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEGT2HudSizeSettingsTest,
	"UEGT2.Settings.HudSize",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEGT2HudSizeSettingsTest::RunTest(const FString& Parameters)
{
	UUEGT2GameUserSettings* Settings = NewObject<UUEGT2GameUserSettings>();
	Settings->SetToDefaults();
	TestEqual(TEXT("normal scale by default"), Settings->GetHudScale(), 1.0f);
	Settings->SetHudSizeLevel(1);
	TestEqual(TEXT("large scale"), Settings->GetHudScale(), 1.25f);
	Settings->SetHudSizeLevel(2);
	TestEqual(TEXT("larger scale"), Settings->GetHudScale(), 1.5f);
	Settings->SetHudSizeLevel(MIN_int32);
	TestEqual(TEXT("low setting clamps"), Settings->GetHudSizeLevel(), 0);
	Settings->SetHudSizeLevel(MAX_int32);
	TestEqual(TEXT("high setting clamps"), Settings->GetHudSizeLevel(), 2);
	// Config loading bypasses setters. Exercise that path without writing the
	// active settings file or exposing the backing property to production code.
	FIntProperty* Property = FindFProperty<FIntProperty>(Settings->GetClass(), TEXT("HudSizeLevel"));
	if (!TestNotNull(TEXT("persisted HUD level"), Property)) { return false; }
	Property->SetPropertyValue_InContainer(Settings, MIN_int32);
	TestEqual(TEXT("low edited config is safe"), Settings->GetHudScale(), 1.0f);
	Property->SetPropertyValue_InContainer(Settings, MAX_int32);
	TestEqual(TEXT("high edited config is safe"), Settings->GetHudScale(), 1.5f);
	Settings->SetToDefaults();
	TestEqual(TEXT("reset clears retained size"), Settings->GetHudSizeLevel(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEGT2AudioSettingsTest,
	"UEGT2.Settings.AudioVolumes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEGT2AudioSettingsTest::RunTest(const FString& Parameters)
{
	UUEGT2GameUserSettings* Settings = UUEGT2GameUserSettings::Get();
	if (!TestNotNull(TEXT("project game settings"), Settings)) { return false; }

	const TCHAR* Paths[] = {
		TEXT("/Game/Fairhaven/Audio/SC_Master"),
		TEXT("/Game/Fairhaven/Audio/SC_Effects"),
		TEXT("/Game/Fairhaven/Audio/SC_Ambience"),
		TEXT("/Game/Fairhaven/Audio/SC_Music"),
		TEXT("/Game/Fairhaven/Audio/SC_UI"),
	};
	TArray<USoundClass*> Classes;
	TArray<float> Before;
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Paths); ++Index)
	{
		USoundClass* Class = LoadObject<USoundClass>(nullptr, Paths[Index]);
		if (!TestNotNull(Paths[Index], Class)) { return false; }
		Classes.Add(Class);
		Before.Add(Settings->GetAudioVolume((EUEGT2AudioBus)Index));
	}
	ON_SCOPE_EXIT
	{
		for (int32 Index = 0; Index < Before.Num(); ++Index)
		{
			Settings->SetAudioVolume((EUEGT2AudioBus)Index, Before[Index]);
		}
		Settings->ApplyNonResolutionSettings();
	};

	const float BusVolumes[] = { 0.5f, 0.8f, 0.6f, 0.4f, 0.2f };
	for (int32 Index = 0; Index < Classes.Num(); ++Index)
	{
		Settings->SetAudioVolume((EUEGT2AudioBus)Index, BusVolumes[Index]);
	}
	Settings->ApplyNonResolutionSettings();
	TestEqual(TEXT("master receives its own slider"), Classes[0]->Properties.Volume, 0.5f);
	for (int32 Index = 1; Index < Classes.Num(); ++Index)
	{
		// Exercise the generated asset hierarchy as well as the menu settings.
		// FAudioDevice propagates Master into each of these children, so their
		// own property must contain only their bus volume.
		TestTrue(FString::Printf(TEXT("%s is below Master"), Paths[Index]),
			Classes[0]->ChildClasses.Contains(Classes[Index]));
		TestTrue(FString::Printf(TEXT("%s retains its Master parent after loading"), Paths[Index]),
			Classes[Index]->ParentClass == Classes[0]);
		TestEqual(FString::Printf(TEXT("%s receives only its own slider"), Paths[Index]),
			Classes[Index]->Properties.Volume, BusVolumes[Index]);
	}
	TestEqual(TEXT("half master and 80 percent effects produce 40 percent gain"),
		Classes[0]->Properties.Volume * Classes[1]->Properties.Volume, 0.4f);

	Settings->SetAudioVolume(EUEGT2AudioBus::Master, 0.0f);
	Settings->ApplyNonResolutionSettings();
	TestEqual(TEXT("master mute silences the hierarchy"), Classes[0]->Properties.Volume, 0.0f);
	for (int32 Index = 1; Index < Classes.Num(); ++Index)
	{
		TestEqual(FString::Printf(TEXT("master mute preserves %s's bus gain"), Paths[Index]),
			Classes[Index]->Properties.Volume, BusVolumes[Index]);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEGT2FoliageDistanceTest,
	"UEGT2.Settings.FoliageDrawDistance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEGT2FoliageDistanceTest::RunTest(const FString& Parameters)
{
	UUEGT2GameUserSettings* Settings = UUEGT2GameUserSettings::Get();
	UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (!TestNotNull(TEXT("settings"), Settings) || !TestNotNull(TEXT("test mesh"), Mesh)) { return false; }
	const int32 PreviousLevel = Settings->GetFoliageDrawDistanceLevel();
	UWorld* World = UWorld::CreateWorld(EWorldType::EditorPreview, false);
	if (!TestNotNull(TEXT("test world"), World)) { return false; }
	ON_SCOPE_EXIT
	{
		World->DestroyWorld(false);
		Settings->SetFoliageDrawDistanceLevel(PreviousLevel);
		Settings->ApplyNonResolutionSettings();
	};
	AUEGT2ScatterField* Nature = World->SpawnActor<AUEGT2ScatterField>();
	AUEGT2ScatterField* Fences = World->SpawnActor<AUEGT2ScatterField>();
	if (!TestNotNull(TEXT("nature"), Nature) || !TestNotNull(TEXT("fences"), Fences)) { return false; }
	Nature->bUseFoliageDrawDistance = true;
	UHierarchicalInstancedStaticMeshComponent* Grass = Nature->AddLayer(Mesh, TEXT("Grass"), 7000, 9000);
	UHierarchicalInstancedStaticMeshComponent* Unlimited = Nature->AddLayer(Mesh, TEXT("Unlimited"), 0, 0);
	UHierarchicalInstancedStaticMeshComponent* Fence = Fences->AddLayer(Mesh, TEXT("Fence"), 12000, 16000);
	if (!TestNotNull(TEXT("grass layer"), Grass) || !TestNotNull(TEXT("unlimited layer"), Unlimited)
		|| !TestNotNull(TEXT("fence layer"), Fence)) { return false; }
	Settings->SetFoliageDrawDistanceLevel(0);
	Nature->DispatchBeginPlay();
	Fences->DispatchBeginPlay();
	TestEqual(TEXT("startup applies the selected fade distance"), Grass->InstanceStartCullDistance, 3500);
	TestEqual(TEXT("startup applies the selected end distance"), Grass->InstanceEndCullDistance, 4500);
	const int32 Levels[] = { 0, 3, 3, 1, 2, 0, 2 };
	const int32 ExpectedEnds[] = { 4500, 13500, 13500, 6750, 9000, 4500, 9000 };
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Levels); ++Index)
	{
		Settings->SetFoliageDrawDistanceLevel(Levels[Index]);
		Settings->ApplyNonResolutionSettings();
		TestEqual(TEXT("repeated settings always scale authored distances"), Grass->InstanceEndCullDistance, ExpectedEnds[Index]);
		TestEqual(TEXT("unlimited layers stay unlimited"), Unlimited->InstanceEndCullDistance, 0);
		TestEqual(TEXT("fences do not follow foliage distance"), Fence->InstanceEndCullDistance, 16000);
	}
	Settings->SetFoliageDrawDistanceLevel(0);
	Settings->ApplyNonResolutionSettings();
	UHierarchicalInstancedStaticMeshComponent* Later = Nature->AddLayer(Mesh, TEXT("Later"), 10000, 20000);
	if (!TestNotNull(TEXT("runtime layer"), Later)) { return false; }
	TestEqual(TEXT("runtime additions use the current setting"), Later->InstanceEndCullDistance, 10000);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
