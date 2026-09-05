// Fairhaven - generated lower river crossing, independent of its movement smoke.
#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Editor.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMeshSocket.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "LandscapeProxy.h"
#include "PhysicsEngine/BodySetup.h"
#include "Player/UEGT2Character.h"
#include "StaticMeshResources.h"
#include "Tests/AutomationEditorCommon.h"

namespace UEGT2CrossingContentTests
{
	constexpr double LaneOffset = 200.0; // Samples four metres of authored width with capsule/rail margin.
	constexpr double DeckThickness = 34.0;
	constexpr double UndersideClearance = 50.0;
	constexpr double VertexTolerance = 0.1; // Static mesh positions and cooked triangle hits use float precision.
	const FName CrossingTag(TEXT("UEGT2.Crossing.LowerRiver"));
	const FName Stations[] = { TEXT("ApproachA"), TEXT("DeckA"), TEXT("DeckB"), TEXT("ApproachB") };
	struct FWaterTriangle { FVector A, B, C; };

	// Water deliberately has no collision. Inspect its actual rendered triangles
	// so a landscape hit in the riverbed cannot masquerade as a dry landing.
	bool ReadRiver(UWorld* World, TArray<FWaterTriangle>& Triangles)
	{
		UStaticMeshComponent* River = nullptr;
		for (TActorIterator<AStaticMeshActor> It(World); It; ++It)
		{
			if (It->GetActorLabel() != TEXT("Water River")) { continue; }
			if (River) { return false; }
			River = It->GetStaticMeshComponent();
		}
		const UStaticMesh* Mesh = River ? River->GetStaticMesh() : nullptr;
		const FStaticMeshRenderData* Data = Mesh ? Mesh->GetRenderData() : nullptr;
		if (!Data || Data->LODResources.Num() == 0) { return false; }
		const FStaticMeshLODResources& LOD = Data->LODResources[0];
		const FPositionVertexBuffer& Positions = LOD.VertexBuffers.PositionVertexBuffer;
		const int32 IndexCount = LOD.IndexBuffer.GetNumIndices();
		if (Positions.GetNumVertices() == 0 || IndexCount == 0 || IndexCount % 3 != 0 || IndexCount > 60000) { return false; }
		const FTransform Transform = River->GetComponentTransform();
		for (int32 Index = 0; Index < IndexCount; Index += 3)
		{
			FVector Vertices[3];
			for (int32 Corner = 0; Corner < 3; ++Corner)
			{
				const uint32 Vertex = LOD.IndexBuffer.GetIndex(Index + Corner);
				if (Vertex >= Positions.GetNumVertices()) { return false; }
				Vertices[Corner] = Transform.TransformPosition(FVector(Positions.VertexPosition(Vertex)));
				if (Vertices[Corner].ContainsNaN()) { return false; }
			}
			Triangles.Add({ Vertices[0], Vertices[1], Vertices[2] });
		}
		return !Triangles.IsEmpty();
	}
	bool WaterHeight(const TArray<FWaterTriangle>& Triangles, const FVector& Point, double& Height)
	{
		bool bFound = false; Height = -TNumericLimits<double>::Max();
		for (const FWaterTriangle& T : Triangles)
		{
			const FVector AB = T.B - T.A, AC = T.C - T.A, AP = Point - T.A;
			const double Denominator = AB.X * AC.Y - AB.Y * AC.X;
			if (FMath::Abs(Denominator) < 0.000001) { continue; }
			const double U = (AP.X * AC.Y - AP.Y * AC.X) / Denominator;
			const double V = (AB.X * AP.Y - AB.Y * AP.X) / Denominator;
			if (U < -0.000001 || V < -0.000001 || U + V > 1.000001) { continue; }
			Height = FMath::Max(Height, T.A.Z + U * AB.Z + V * AC.Z); bFound = true;
		}
		return bFound;
	}
	bool TraceFloor(UWorld* World, const FVector& Expected, FHitResult& Hit)
	{
		return World->LineTraceSingleByObjectType(Hit, Expected + FVector(0, 0, 120), Expected - FVector(0, 0, 120),
			FCollisionObjectQueryParams(ECC_WorldStatic), FCollisionQueryParams(SCENE_QUERY_STAT(CrossingContentFloor), false));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEGT2CrossingContentTest,
	"UEGT2.Content.LowerRiverCrossing", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEGT2CrossingContentTest::RunTest(const FString& Parameters)
{
	using namespace UEGT2CrossingContentTests;
	FAutomationEditorCommonUtils::LoadMap(TEXT("/Game/Maps/L_Fairhaven"));
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("generated editor world exists"), World)) { return false; }
	AStaticMeshActor* Bridge = nullptr; int32 TaggedActors = 0;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (It->ActorHasTag(CrossingTag)) { ++TaggedActors; Bridge = Cast<AStaticMeshActor>(*It); }
	}
	if (!TestEqual(TEXT("exactly one actor owns the lower river crossing tag"), TaggedActors, 1)
		|| !TestNotNull(TEXT("tagged crossing is a static mesh actor"), Bridge)) { return false; }
	TestEqual(TEXT("crossing actor belongs to the town stage"), Bridge->GetActorLabel(), FString(TEXT("Town Bridge")));
	const UStaticMeshComponent* Component = Bridge->GetStaticMeshComponent();
	const UStaticMesh* Mesh = Component ? Component->GetStaticMesh() : nullptr;
	if (!TestNotNull(TEXT("crossing has its generated mesh"), Mesh)) { return false; }
	TestEqual(TEXT("fitted mesh is distinct from the generic catalog bridge"), Mesh->GetPathName(),
		FString(TEXT("/Game/Fairhaven/Meshes/Town/SM_Bridge_LowerRiver.SM_Bridge_LowerRiver")));
	if (!TestTrue(TEXT("crossing transform is finite and valid"), Component->GetComponentTransform().IsValid())) { return false; }
	TestTrue(TEXT("crossing uses authored geometry without scale distortion"), Component->GetComponentScale().Equals(FVector::OneVector, 0.0001));
	TestTrue(TEXT("crossing is upright"), Component->GetUpVector().Equals(FVector::UpVector, 0.0001));
	TestEqual(TEXT("crossing remains static"), Component->GetMobility(), EComponentMobility::Static);
	TestEqual(TEXT("ground queries can find the bridge"), Component->GetCollisionObjectType(), ECC_WorldStatic);
	TestTrue(TEXT("crossing collision answers queries"), Component->IsQueryCollisionEnabled());
	TestEqual(TEXT("crossing blocks the real player capsule"), Component->GetCollisionResponseToChannel(ECC_Pawn), ECR_Block);
	TestFalse(TEXT("bridge does not simulate physics"), Component->IsSimulatingPhysics());
	const UBodySetup* Body = Mesh->GetBodySetup();
	if (!TestNotNull(TEXT("crossing has cooked collision setup"), Body)) { return false; }
	TestTrue(TEXT("walkable deck uses complex-as-simple rather than a whole-bounds box"), Body->CollisionTraceFlag == CTF_UseComplexAsSimple);

	FVector WorldStations[4], LocalStations[4];
	for (int32 Index = 0; Index < 4; ++Index)
	{
		int32 Count = 0; const UStaticMeshSocket* Station = nullptr;
		for (const UStaticMeshSocket* Socket : Mesh->Sockets)
		{
			if (Socket && Socket->SocketName == Stations[Index]) { ++Count; Station = Socket; }
		}
		if (!TestEqual(FString::Printf(TEXT("ground station %s is unique"), *Stations[Index].ToString()), Count, 1) || !Station) { return false; }
		TestFalse(TEXT("ground station survives editor-only stripping"), Station->IsEditorOnly());
		LocalStations[Index] = Station->RelativeLocation;
		WorldStations[Index] = Component->GetSocketLocation(Stations[Index]);
		if (!TestTrue(TEXT("station location is finite"), !LocalStations[Index].ContainsNaN() && !WorldStations[Index].ContainsNaN())) { return false; }
		TestTrue(TEXT("socket transforms to its authored world ground station"),
			WorldStations[Index].Equals(Component->GetComponentTransform().TransformPosition(LocalStations[Index]), 0.001));
	}
	for (int32 Segment = 0; Segment < 3; ++Segment)
	{
		if (!TestTrue(TEXT("stations advance along the road through nonzero approach/deck segments"),
			LocalStations[Segment + 1].X > LocalStations[Segment].X + 100.0
			&& FVector::Dist2D(WorldStations[Segment], WorldStations[Segment + 1]) < 20000.0)) { return false; }
	}
	TestTrue(TEXT("deck stations align on the level local deck axis"), FMath::Abs(LocalStations[1].Y) < 0.01
		&& FMath::Abs(LocalStations[2].Y) < 0.01 && FMath::Abs(LocalStations[1].Z - LocalStations[2].Z) < 0.01);
	for (int32 Index = 0; Index < 4; ++Index)
	{
		FVector Probe = WorldStations[Index];
		// A socket is exactly on the mesh boundary; inset two centimetres avoids
		// testing which side of a float vertex that mathematically shared edge falls.
		if (Index == 0) { Probe += (WorldStations[1] - Probe).GetSafeNormal2D() * 2.0; }
		if (Index == 3) { Probe += (WorldStations[2] - Probe).GetSafeNormal2D() * 2.0; }
		FHitResult Ground;
		const bool bHit = TraceFloor(World, Probe, Ground);
		if (!TestTrue(FString::Printf(TEXT("%s denotes bridge floor, not capsule center or rail top (hit=%s delta=%.3f)"),
			*Stations[Index].ToString(), *GetNameSafe(Ground.GetActor()), Ground.ImpactPoint.Z - WorldStations[Index].Z),
			bHit && Ground.GetComponent() == Component && FMath::Abs(Ground.ImpactPoint.Z - WorldStations[Index].Z) <= 5.0)) { return false; }
	}

	TArray<FWaterTriangle> Water;
	if (!TestTrue(TEXT("actual generated river triangles are available for dry-ground checks"), ReadRiver(World, Water))) { return false; }
	double RiverZ = 0.0;
	TestTrue(TEXT("the bridge actually crosses the rendered river"), WaterHeight(Water, (WorldStations[1] + WorldStations[2]) * 0.5, RiverZ));
	const AUEGT2Character* Player = GetDefault<AUEGT2Character>();
	const UCapsuleComponent* Capsule = Player->GetCapsuleComponent();
	const double FloorZ = Player->GetCharacterMovement()->GetWalkableFloorZ();
	int32 SupportSamples = 0;
	// Ramp rows keep constant mesh-local X even when their centres bend toward
	// the road. Segment-perpendicular offsets can run beyond a terminal row.
	const FVector CrossSectionRight = Component->GetRightVector();
	for (int32 Segment = 0; Segment < 3; ++Segment)
	{
		const FVector Start = WorldStations[Segment], End = WorldStations[Segment + 1];
		const int32 Steps = FMath::CeilToInt(FVector::Dist2D(Start, End) / 100.0);
		// Sample interiors rather than ambiguous triangle boundary edges. Every
		// ramp/deck segment must be supported by this mesh, never the riverbed.
		for (int32 Step = 0; Step < Steps; ++Step)
		{
			const double Alpha = (Step + 0.5) / Steps;
			for (double Lane : { -LaneOffset, 0.0, LaneOffset })
			{
				const FVector Expected = FMath::Lerp(Start, End, Alpha) + CrossSectionRight * Lane;
				FHitResult Ground; const bool bHit = TraceFloor(World, Expected, Ground);
				if (!TestTrue(FString::Printf(TEXT("segment %d station %.2f lane %.0f is bridge floor (hit=%s z=%.2f expected=%.2f normal=%.3f)"),
					Segment, Alpha, Lane, *GetNameSafe(Ground.GetActor()), Ground.ImpactPoint.Z, Expected.Z, Ground.ImpactNormal.Z),
					bHit && Ground.GetComponent() == Component && Ground.ImpactNormal.Z >= FloorZ)) { return false; }
				if (WaterHeight(Water, Ground.ImpactPoint, RiverZ))
				{
					const double Clearance = Ground.ImpactPoint.Z - DeckThickness - RiverZ;
					if (!TestTrue(FString::Printf(TEXT("bridge underside remains 50cm above the actual river (clearance=%.3fcm)"), Clearance),
						Clearance >= UndersideClearance - VertexTolerance)) { return false; }
				}
				++SupportSamples;
			}
		}
	}

	const float Radius = Capsule->GetUnscaledCapsuleRadius(), HalfHeight = Capsule->GetUnscaledCapsuleHalfHeight();
	const FCollisionShape Shape = FCollisionShape::MakeCapsule(Radius, HalfHeight);
	const FCollisionQueryParams Params(SCENE_QUERY_STAT(CrossingContentLanding), false);
	for (int32 Side = 0; Side < 2; ++Side)
	{
		const FVector End = WorldStations[Side == 0 ? 0 : 3];
		const FVector Inner = WorldStations[Side == 0 ? 1 : 2];
		const FVector Out = (End - Inner).GetSafeNormal2D(), Right(-Out.Y, Out.X, 0.0);
		for (double Lane : { -LaneOffset, 0.0, LaneOffset })
		{
			const FVector Expected = End + Out * 100.0 + Right * Lane;
			FHitResult Ground;
			if (!TestTrue(FString::Printf(TEXT("landing %d lane %.0f has walkable landscape within 120cm"), Side, Lane),
				TraceFloor(World, Expected, Ground) && Cast<ALandscapeProxy>(Ground.GetActor()) && Ground.ImpactNormal.Z >= FloorZ)) { return false; }
			if (WaterHeight(Water, Ground.ImpactPoint, RiverZ)
				&& !TestTrue(TEXT("landing is dry rather than submerged landscape"), Ground.ImpactPoint.Z > RiverZ + 1.0)) { return false; }
			FHitResult Standing;
			const FVector Center = Ground.ImpactPoint + FVector(0, 0, HalfHeight);
			if (!TestTrue(TEXT("ordinary standing capsule sweeps onto the dry landing"), World->SweepSingleByProfile(Standing,
				Center + FVector(0, 0, 120), Center - FVector(0, 0, 120), FQuat::Identity, TEXT("Pawn"), Shape, Params)
				&& !Standing.bStartPenetrating && Cast<ALandscapeProxy>(Standing.GetActor()) && Standing.ImpactNormal.Z >= FloorZ)) { return false; }
			TestFalse(TEXT("standing capsule clears the landing and surrounding props"), World->OverlapBlockingTestByProfile(
				Standing.Location + FVector(0, 0, 2.15), FQuat::Identity, TEXT("Pawn"), Shape, Params));
		}
	}
	AddInfo(FString::Printf(TEXT("Lower river crossing: four sockets, %d bridge-owned floor samples and six dry standing-capsule landings checked."), SupportSamples));
	return true;
}

#endif
