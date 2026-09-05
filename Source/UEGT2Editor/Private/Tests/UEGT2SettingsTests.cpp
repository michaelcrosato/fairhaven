#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Misc/ScopeExit.h"
#include "Settings/UEGT2GameUserSettings.h"
#include "Sound/SoundClass.h"

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

#endif // WITH_AUTOMATION_TESTS
