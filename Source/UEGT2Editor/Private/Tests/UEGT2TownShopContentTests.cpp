// Fairhaven - town services cannot be replaced by Newhaven's global counts.
#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Components/StaticMeshComponent.h"
#include "Editor.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Interaction/UEGT2Amenity.h"
#include "Tests/AutomationEditorCommon.h"

namespace UEGT2TownShopContentTests
{
	struct FShop
	{
		const TCHAR* Trade;
		const TCHAR* Mesh;
		const TCHAR* Venue;
		bool bFood;
	};
	const FShop Required[] = {
		{ TEXT("grocer"), TEXT("SM_House_C"), TEXT("the Fairhaven Grocery"), true },
		{ TEXT("baker"), TEXT("SM_House_A"), TEXT("The Bakehouse"), true },
		{ TEXT("butcher_hardware"), TEXT("SM_House_C"), TEXT("the Ironmonger and Hardware"), true },
		{ TEXT("clothier"), TEXT("SM_House_A"), TEXT("the Draper and Tailor"), false },
		{ TEXT("barber"), TEXT("SM_House_C"), TEXT("the Barber"), false },
		{ TEXT("doctor"), TEXT("SM_House_A"), TEXT("the Physician"), false },
		{ TEXT("dentist"), TEXT("SM_House_C"), TEXT("the Dentist"), false },
		{ TEXT("optician"), TEXT("SM_House_A"), TEXT("the Spectacle Maker"), false },
		{ TEXT("lawyer"), TEXT("SM_House_C"), TEXT("the Solicitor"), false },
		{ TEXT("bookshop"), TEXT("SM_House_A"), TEXT("the Bookseller"), false },
		{ TEXT("post"), TEXT("SM_House_C"), TEXT("the Post Office"), false },
		{ TEXT("bank"), TEXT("SM_House_A"), TEXT("the Bank"), false }
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEGT2TownShopContentTest, "UEGT2.Content.TownShops",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEGT2TownShopContentTest::RunTest(const FString& Parameters)
{
	using namespace UEGT2TownShopContentTests;
	FAutomationEditorCommonUtils::LoadMap(TEXT("/Game/Maps/L_Fairhaven"));
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("generated editor world exists"), World)) { return false; }
	TArray<AActor*> Shops;
	TArray<AUEGT2Amenity*> Amenities;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (It->GetActorLabel().StartsWith(TEXT("Town Shop "))) { Shops.Add(*It); }
		if (AUEGT2Amenity* Amenity = Cast<AUEGT2Amenity>(*It)) { Amenities.Add(Amenity); }
	}
	TestEqual(TEXT("Fairhaven retains exactly twelve shop shells"), Shops.Num(), 12);
	TSet<AUEGT2Amenity*> Matched;
	int32 Food = 0, Work = 0;
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Required); ++Index)
	{
		const FShop& Expected = Required[Index];
		const FString Label = FString::Printf(TEXT("Town Shop %s %d"), Expected.Trade, Index);
		int32 Shells = 0; AStaticMeshActor* Shop = nullptr;
		for (AActor* Actor : Shops)
		{
			if (Actor->GetActorLabel() == Label) { ++Shells; Shop = Cast<AStaticMeshActor>(Actor); }
		}
		if (!TestEqual(Label + TEXT(" exists exactly once"), Shells, 1)
			|| !TestNotNull(Label + TEXT(" is an actual static mesh shell"), Shop)) { continue; }
		const UStaticMeshComponent* Component = Shop->GetStaticMeshComponent();
		const UStaticMesh* Mesh = Component ? Component->GetStaticMesh() : nullptr;
		if (!TestNotNull(Label + TEXT(" has generated shell geometry"), Mesh)) { continue; }
		TestEqual(Label + TEXT(" uses the authored service shell"), Mesh->GetName(), FString(Expected.Mesh));
		TestTrue(Label + TEXT(" shell has triangles"), Mesh->GetNumTriangles(0) > 0);
		TestTrue(Label + TEXT(" has a finite upright unscaled transform"), Shop->GetActorTransform().IsValid()
			&& Shop->GetActorScale3D().Equals(FVector::OneVector, 0.0001)
			&& Shop->GetActorUpVector().Equals(FVector::UpVector, 0.0001));
		TestEqual(Label + TEXT(" shell is static"), Component->GetMobility(), EComponentMobility::Static);
		TestEqual(Label + TEXT(" shell remains WorldStatic"), Component->GetCollisionObjectType(), ECC_WorldStatic);
		TestTrue(Label + TEXT(" shell has query collision"), Component->IsQueryCollisionEnabled());
		TestEqual(Label + TEXT(" shell retains ordinary player collision"), Component->GetCollisionResponseToChannel(ECC_Pawn), ECR_Block);

		// npc._Survey uses _front_of(location, yaw, 560, 190) for every town
		// shop, preserving its actor Z. Match the native serialized placement,
		// not a city venue with the same role/name or an approximate XY cluster.
		const FVector Front = Shop->GetActorLocation() - Shop->GetActorRightVector() * 470.0;
		AUEGT2Amenity* Venue = nullptr; int32 Venues = 0;
		for (AUEGT2Amenity* Candidate : Amenities)
		{
			if (Candidate->GetActorLocation().Equals(Front, 2.0)) { Venue = Candidate; ++Venues; }
		}
		if (!TestEqual(Label + TEXT(" has exactly one native amenity at its full XYZ doorstep"), Venues, 1)
			|| !TestNotNull(Label + TEXT(" doorstep amenity exists"), Venue)) { continue; }
		TestFalse(Label + TEXT(" does not share another shop's amenity"), Matched.Contains(Venue));
		Matched.Add(Venue);
		const EUEGT2AmenityKind Kind = Expected.bFood ? EUEGT2AmenityKind::Food : EUEGT2AmenityKind::Work;
		const EUEGT2NPCRole Role = Expected.bFood ? EUEGT2NPCRole::Villager : EUEGT2NPCRole::Shopkeeper;
		TestEqual(Label + TEXT(" supplies the authored amenity kind"), Venue->GetKind(), Kind);
		TestEqual(Label + TEXT(" supplies the authored job role"), Venue->GetJobRole(), Role);
		TestEqual(Label + TEXT(" preserves its venue name"), Venue->GetVenueName().ToString(), FString(Expected.Venue));
		Food += Venue->GetKind() == EUEGT2AmenityKind::Food ? 1 : 0;
		Work += Venue->GetKind() == EUEGT2AmenityKind::Work ? 1 : 0;
	}
	TestEqual(TEXT("twelve town shops own twelve distinct doorstep amenities"), Matched.Num(), 12);
	TestEqual(TEXT("the three town food shops remain usable venues"), Food, 3);
	TestEqual(TEXT("the nine town service shops retain their workplaces"), Work, 9);
	AddInfo(FString::Printf(TEXT("Fairhaven shops: %d shells, %d matching native amenities (%d food, %d work). Spatial matching excludes Newhaven substitutes; ordinary traversal remains a packaged check."),
		Shops.Num(), Matched.Num(), Food, Work));
	return true;
}

#endif
