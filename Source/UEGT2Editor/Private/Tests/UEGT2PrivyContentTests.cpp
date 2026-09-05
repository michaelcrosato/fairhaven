// Fairhaven - square privies must have usable ground, entrances and amenities.
#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Editor.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Interaction/UEGT2Amenity.h"
#include "Interaction/UEGT2InteractionComponent.h"
#include "LandscapeProxy.h"
#include "PhysicsEngine/BodySetup.h"
#include "Player/UEGT2Character.h"
#include "Tests/AutomationEditorCommon.h"

namespace UEGT2PrivyContentTests
{
	constexpr double FloorLocalZ = 24.0; // The generated privy foundation's top.
	constexpr double FloorGap = 2.15; // Normal CharacterMovement floor spacing.
	FVector At(const AActor* Actor, double X, double Y, double Z = FloorLocalZ)
	{
		return Actor->GetActorTransform().TransformPosition(FVector(X, Y, Z));
	}
	bool TerrainAt(UWorld* World, const FVector& Expected, const AActor* Ignore, FHitResult& Hit)
	{
		return World->LineTraceSingleByObjectType(Hit, Expected + FVector(0, 0, 120), Expected - FVector(0, 0, 120),
			FCollisionObjectQueryParams(ECC_WorldStatic), FCollisionQueryParams(SCENE_QUERY_STAT(PrivyTerrain), false, Ignore))
			&& Cast<ALandscapeProxy>(Hit.GetActor());
	}
	bool CapsuleClear(UWorld* World, const FVector& From, const FVector& To,
		const UCapsuleComponent* Capsule, FHitResult& Hit)
	{
		return !World->SweepSingleByProfile(Hit, From, To, FQuat::Identity, Capsule->GetCollisionProfileName(),
			FCollisionShape::MakeCapsule(Capsule->GetUnscaledCapsuleRadius(), Capsule->GetUnscaledCapsuleHalfHeight()),
			FCollisionQueryParams(SCENE_QUERY_STAT(PrivyCorridor), false));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEGT2SquarePrivyContentTest,
	"UEGT2.Content.SquarePrivies", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEGT2SquarePrivyContentTest::RunTest(const FString& Parameters)
{
	using namespace UEGT2PrivyContentTests;
	FAutomationEditorCommonUtils::LoadMap(TEXT("/Game/Maps/L_Fairhaven"));
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("generated editor world exists"), World)) { return false; }
	const AUEGT2Character* Player = GetDefault<AUEGT2Character>();
	const UCapsuleComponent* Capsule = Player->GetCapsuleComponent();
	const UCharacterMovementComponent* Movement = Player->GetCharacterMovement();
	const float Radius = Capsule->GetUnscaledCapsuleRadius(), Half = Capsule->GetUnscaledCapsuleHalfHeight();
	const FCollisionShape Shape = FCollisionShape::MakeCapsule(Radius, Half);
	const FCollisionQueryParams Params(SCENE_QUERY_STAT(PrivyStanding), false);
	TArray<AUEGT2Amenity*> Washrooms;
	int32 SquareTags = 0;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		for (FName Tag : It->Tags) { SquareTags += Tag.ToString().StartsWith(TEXT("UEGT2.SquarePrivy.")) ? 1 : 0; }
	}
	if (!TestEqual(TEXT("generated world has exactly four square-privy tags"), SquareTags, 4)) { return false; }
	for (TActorIterator<AUEGT2Amenity> It(World); It; ++It)
	{
		if (It->GetKind() == EUEGT2AmenityKind::Washroom) { Washrooms.Add(*It); }
	}
	TSet<AUEGT2Amenity*> MatchedAmenities;
	int32 StandingSamples = 0, CorridorSweeps = 0;
	for (int32 Index = 0; Index < 4; ++Index)
	{
		const FString Label = FString::Printf(TEXT("Town Privy %d"), Index);
		const FName Tag(*FString::Printf(TEXT("UEGT2.SquarePrivy.%d"), Index));
		int32 Labels = 0, Tags = 0; AStaticMeshActor* Privy = nullptr; AActor* Tagged = nullptr;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (It->GetActorLabel() == Label) { ++Labels; Privy = Cast<AStaticMeshActor>(*It); }
			if (It->ActorHasTag(Tag)) { ++Tags; Tagged = *It; }
		}
		if (!TestEqual(Label + TEXT(" exists exactly once"), Labels, 1)
			|| !TestEqual(Label + TEXT(" cooked tag is unique"), Tags, 1)
			|| !TestNotNull(Label + TEXT(" is a static mesh actor"), Privy)
			|| !TestTrue(Label + TEXT(" owns its matching cooked identity"), Tagged == Privy)) { return false; }
		const UStaticMeshComponent* Component = Privy->GetStaticMeshComponent();
		const UStaticMesh* Mesh = Component ? Component->GetStaticMesh() : nullptr;
		if (!TestNotNull(Label + TEXT(" has its generated mesh"), Mesh)) { return false; }
		TestEqual(Label + TEXT(" uses the town privy geometry"), Mesh->GetName(), FString(TEXT("SM_Privy_A")));
		if (!TestTrue(Label + TEXT(" has a finite upright unscaled transform"), Privy->GetActorTransform().IsValid()
			&& Privy->GetActorScale3D().Equals(FVector::OneVector, 0.0001)
			&& Privy->GetActorUpVector().Equals(FVector::UpVector, 0.0001))) { return false; }
		TestEqual(Label + TEXT(" is static"), Component->GetMobility(), EComponentMobility::Static);
		TestEqual(Label + TEXT(" supports ordinary static ground queries"), Component->GetCollisionObjectType(), ECC_WorldStatic);
		TestTrue(Label + TEXT(" has query collision"), Component->IsQueryCollisionEnabled());
		TestEqual(Label + TEXT(" blocks the real pawn"), Component->GetCollisionResponseToChannel(ECC_Pawn), ECR_Block);
		const UBodySetup* Body = Mesh->GetBodySetup();
		if (!TestNotNull(Label + TEXT(" has collision geometry"), Body)) { return false; }
		TestTrue(Label + TEXT(" keeps its actual doorway instead of whole-bounds collision"), Body->CollisionTraceFlag == CTF_UseComplexAsSimple);

		const double FloorZ = At(Privy, 0, 0).Z;
		double Minimum = TNumericLimits<double>::Max(), Maximum = -TNumericLimits<double>::Max(), BodyMaximum = Maximum;
		// Ignore only this privy when measuring its underlying plot; other static
		// ground-level obstructions must not be mistaken for terrain.
		for (double X : { -135.0, -67.5, 0.0, 67.5, 135.0 })
		{
			for (double Y : { -131.0, -65.5, 0.0, 65.5, 131.0 })
			{
				FHitResult Ground;
				if (!TestTrue(Label + TEXT(" body footprint lies on Landscape"), TerrainAt(World, At(Privy, X, Y), Privy, Ground))) { return false; }
				Minimum = FMath::Min(Minimum, Ground.ImpactPoint.Z); Maximum = FMath::Max(Maximum, Ground.ImpactPoint.Z);
				BodyMaximum = FMath::Max(BodyMaximum, Ground.ImpactPoint.Z);
			}
		}
		for (double X : { -120.0, -60.0, 0.0, 60.0, 120.0 })
		{
			for (double Y : { -110.0, -150.0, -225.0, -285.0, -350.0, -410.0, -470.0 })
			{
				FHitResult Ground;
				if (!TestTrue(Label + TEXT(" front reservations lie on Landscape"), TerrainAt(World, At(Privy, X, Y), Privy, Ground))) { return false; }
				Minimum = FMath::Min(Minimum, Ground.ImpactPoint.Z); Maximum = FMath::Max(Maximum, Ground.ImpactPoint.Z);
			}
		}
		// Authoring samples bilinear heights; actual Landscape collision uses
		// triangles. Allow two centimetres for that difference, not a tall step.
		TestTrue(Label + TEXT(" body and entrance terrain remain within the 15cm relief contract"), Maximum - Minimum <= 17.0);
		TestTrue(Label + TEXT(" floor clears the highest body ground by approximately 8cm"), FMath::Abs(FloorZ - BodyMaximum - 8.0) <= 2.0);
		TestTrue(Label + TEXT(" floor clears its body ground with normal steps from the approach"), FloorZ > BodyMaximum
			&& FMath::Abs(FloorZ - Maximum) < Movement->MaxStepHeight && FMath::Abs(FloorZ - Minimum) < Movement->MaxStepHeight);

		// npc._front_of and the generator both define front as local -Y.
		const FVector ExpectedAmenity = At(Privy, 0, -225.0, 0);
		AUEGT2Amenity* Amenity = nullptr; int32 AmenityCount = 0;
		for (AUEGT2Amenity* Candidate : Washrooms)
		{
			if (Candidate->GetActorLocation().Equals(ExpectedAmenity, 2.0)) { Amenity = Candidate; ++AmenityCount; }
		}
		if (!TestEqual(Label + TEXT(" has exactly one washroom at its 225cm front anchor"), AmenityCount, 1)
			|| !TestNotNull(Label + TEXT(" washroom exists"), Amenity)) { return false; }
		TestFalse(Label + TEXT(" does not share another privy's amenity"), MatchedAmenities.Contains(Amenity));
		MatchedAmenities.Add(Amenity);
		TestTrue(Label + TEXT(" washroom answers the relief activity"), Amenity->GetActivity() == EUEGT2Activity::Washroom);

		for (double Lane : { -60.0, 0.0, 60.0 })
		{
			FVector PreviousRaised = FVector::ZeroVector; bool bPrevious = false;
			for (double Distance : { 350.0, 325.0, 300.0, 275.0, 250.0, 225.0, 200.0, 175.0, 160.0 })
			{
				const FString Sample = FString::Printf(TEXT("%s front %.0f lane %.0f"), *Label, Distance, Lane);
				FHitResult Ground;
				if (!TestTrue(Sample + TEXT(" has walkable real ground"), TerrainAt(World, At(Privy, Lane, -Distance), nullptr, Ground)
					&& Ground.ImpactNormal.Z >= Movement->GetWalkableFloorZ())) { return false; }
				const FVector GroundCenter = Ground.ImpactPoint + FVector(0, 0, Half);
				FHitResult Standing;
				const bool bSupported = World->SweepSingleByProfile(Standing,
					GroundCenter + FVector(0, 0, Movement->MaxStepHeight), GroundCenter - FVector(0, 0, Movement->MaxStepHeight),
					FQuat::Identity, Capsule->GetCollisionProfileName(), Shape, Params);
				if (!TestTrue(Sample + FString::Printf(TEXT(" accepts the normal standing capsule (hit=%s component=%s penetrating=%d normal=%s at=%s expected=%s)"),
					*GetNameSafe(Standing.GetActor()), *GetNameSafe(Standing.GetComponent()), Standing.bStartPenetrating,
					*Standing.ImpactNormal.ToCompactString(), *Standing.ImpactPoint.ToCompactString(), *GroundCenter.ToCompactString()),
					bSupported && !Standing.bStartPenetrating && Cast<ALandscapeProxy>(Standing.GetActor())
					&& Standing.ImpactNormal.Z >= Movement->GetWalkableFloorZ())) { return false; }
				const FVector Stand = Standing.Location + FVector(0, 0, FloorGap);
				if (!TestFalse(Sample + TEXT(" standing capsule is clear of later props"), World->OverlapBlockingTestByProfile(
					Stand, FQuat::Identity, Capsule->GetCollisionProfileName(), Shape, Params))) { return false; }
				++StandingSamples;
				// Lift by at most the real step allowance to the doorway's floor
				// plane, then sweep the complete body continuously along the lane.
				// Separate terrain-supported poses above prove the lower stances fit.
				const FVector Raised(Stand.X, Stand.Y, FMath::Max(FloorZ + Half + FloorGap, Stand.Z));
				if (!TestTrue(Sample + TEXT(" floor-level clearance stays within a normal step"), Raised.Z >= Stand.Z - 1.0
					&& Raised.Z - Stand.Z <= Movement->MaxStepHeight)) { return false; }
				FHitResult Hit;
				if (!TestTrue(Sample + TEXT(" capsule clears the upward step space"), CapsuleClear(World, Stand, Raised, Capsule, Hit))) { return false; }
				if (bPrevious && !TestTrue(Sample + TEXT(" capsule corridor is unobstructed"), CapsuleClear(World, PreviousRaised, Raised, Capsule, Hit))) { return false; }
				PreviousRaised = Raised; bPrevious = true; ++CorridorSweeps;
				if (Distance == 225.0 || Distance == 350.0)
				{
					TestTrue(Sample + TEXT(" stays in its washroom's ordinary use range"), FVector::Dist(Stand, Amenity->GetActorLocation()) <= Amenity->GetUseRange());
					const FVector Eye = Stand + FVector(0, 0, Player->GetCamera()->GetRelativeLocation().Z);
					TestTrue(Sample + TEXT(" amenity point is within normal probe reach"), FVector::Dist(Eye, Amenity->GetInteractionPoint()) <= Player->GetInteraction()->Reach);
				}
			}
		}
		for (double Lane : { -15.0, 0.0, 15.0 })
		{
			FVector Outside = At(Privy, Lane, -160.0) + FVector(0, 0, Half + FloorGap);
			FHitResult OutsideGround;
			if (!TestTrue(Label + TEXT(" doorway approach has ordinary capsule support"), World->SweepSingleByProfile(OutsideGround,
				Outside + FVector(0, 0, Movement->MaxStepHeight), Outside - FVector(0, 0, Movement->MaxStepHeight),
				FQuat::Identity, Capsule->GetCollisionProfileName(), Shape, Params) && !OutsideGround.bStartPenetrating
				&& Cast<ALandscapeProxy>(OutsideGround.GetActor()) && OutsideGround.ImpactNormal.Z >= Movement->GetWalkableFloorZ())) { return false; }
			// The approach can be a little higher than the floor. A normal small
			// step down must not fail merely because a test pose starts underground.
			Outside.Z = FMath::Max(Outside.Z, OutsideGround.Location.Z + FloorGap);
			const FVector Inside = At(Privy, Lane, -40.0) + FVector(0, 0, Half + FloorGap);
			FHitResult Hit;
			const bool bClear = CapsuleClear(World, Outside, Inside, Capsule, Hit);
			if (!TestTrue(FString::Printf(TEXT("%s normal capsule clears doorway lane %.0f (hit=%s)"), *Label, Lane, *GetNameSafe(Hit.GetActor())),
				bClear)) { return false; }
			FHitResult Floor;
			if (!TestTrue(Label + TEXT(" doorway leads onto its own walkable floor"), World->LineTraceSingleByObjectType(Floor,
				Inside, Inside - FVector(0, 0, Half + 20.0), FCollisionObjectQueryParams(ECC_WorldStatic), Params)
				&& Floor.GetComponent() == Component && Floor.ImpactNormal.Z >= Movement->GetWalkableFloorZ()
				&& FMath::Abs(Floor.ImpactPoint.Z - FloorZ) <= 1.0)) { return false; }
			TestFalse(Label + TEXT(" a standing body fits inside the doorway"), World->OverlapBlockingTestByProfile(
				Inside, FQuat::Identity, Capsule->GetCollisionProfileName(), Shape, Params));
			++CorridorSweeps;
		}
	}
	TestEqual(TEXT("all four square privies own distinct washroom amenities"), MatchedAmenities.Num(), 4);
	AddInfo(FString::Printf(TEXT("Square privies: four identities and amenities, %d terrain-supported capsule poses, %d approach/doorway corridor samples; capsule %.0f/%.0fcm, step %.0fcm. Actual interaction remains a packaged probe/use check."),
		StandingSamples, CorridorSweeps, Radius, Half, Movement->MaxStepHeight));
	return true;
}

#endif
