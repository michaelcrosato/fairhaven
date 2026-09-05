#include "Diagnostics/UEGT2ContractWalkSmokeSubsystem.h"

#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Contracts/UEGT2SurveyContract.h"
#include "Contracts/UEGT2SurveyContractSubsystem.h"
#include "Engine/Console.h"
#include "Engine/GameViewportClient.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMeshSocket.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/WorldSettings.h"
#include "GenericPlatform/GenericPlatformInputDeviceMapper.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "Input/Events.h"
#include "InputKeyEventArgs.h"
#include "Interaction/UEGT2InteractionComponent.h"
#include "Interaction/UEGT2WorldInteractables.h"
#include "Layout/Children.h"
#include "Misc/CommandLine.h"
#include "Misc/Guid.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "NPC/UEGT2NPCDirector.h"
#include "Player/UEGT2Character.h"
#include "Player/UEGT2InputConfig.h"
#include "Player/UEGT2NeedsComponent.h"
#include "Player/UEGT2PlayerController.h"
#include "Progress/UEGT2ProgressSubsystem.h"
#include "UEGT2LogChannels.h"
#include "Widgets/Text/STextBlock.h"
#include "World/UEGT2SkyController.h"

namespace UEGT2ContractWalkSmoke
{
	constexpr double FloorGap = 2.15, SeamMargin = 54.0, MaxSeconds = 25.0 * 60.0;
	const TCHAR* RouteRevision = TEXT("F008-34-r2 seed=20260826 generation=2026-08-27T08:21:06Z");
	const FName BridgeTag(TEXT("UEGT2.Crossing.LowerRiver"));
	const FName Sockets[] = { TEXT("ApproachA"), TEXT("DeckA"), TEXT("DeckB"), TEXT("ApproachB") };
	const FName Required[] = { TEXT("fairhaven_harbour"), TEXT("fairhaven_light"), TEXT("mill_rise") };
	// Metres, with terrain Z only a setup/check hint. Runtime physics owns Z.
	// P11..P16 are replaced by live cooked bridge sockets and dry end points.
	const FVector Stations[] = {
		{-12,-29.5,15.594}, {-12,-30,15.594}, {40,-30,15.500}, {40,190,14.219},
		{39.028,210.114,10.982}, {-6.991,207.888,13.625}, {-8.228,233.481,10.445}, {-11.875,277.217,2.981},
		{40,75.455,15.219}, {140,130,14.578}, {188.049,144.014,15.173}, {189.009,144.294,15.222},
		{209.169,150.173,20.384}, {260.367,165.109,20.384}, {269.656,165.403,18.308}, {270.655,165.434,18.328},
		{215.908,285.867,2.024}, {170,265,4.656}, {163.712,263.428,5.011}, {160.364,276.818,3.058},
		{-90,-30,14.859}, {-90,-160,14.031}, {-63.699,-169.863,14.516}, {-90,-240,16.219},
		{-93,-248.998,16.578}, {-61.423,-259.526,16.397},
		// Walk around existing houses and trees between the road endpoints.
		// These keep the checked 136cm strip clear without changing the world.
		{-13,209,13.765625}, {-13,224,12.046875}, {-91,-168,14.015625}, {-76,-171,14.25},
		{243.877,229.174,13.94678125}, {240.164,247.007,10.9860625}, {41.5,202,11.8359375}, {-80,-255,16.46875}
	};
	bool Finite(const FVector& V) { return FMath::IsFinite(V.X) && FMath::IsFinite(V.Y) && FMath::IsFinite(V.Z); }
	bool SameNeeds(const FUEGT2NPCNeeds& A, const FUEGT2NPCNeeds& B, float Tolerance)
	{
		return FMath::Abs(A.Energy-B.Energy) <= Tolerance && FMath::Abs(A.Fed-B.Fed) <= Tolerance
			&& FMath::Abs(A.Relief-B.Relief) <= Tolerance && FMath::Abs(A.Company-B.Company) <= Tolerance;
	}
	int32 OptionCount(const TCHAR* Name, FString* Value = nullptr, bool* bBare = nullptr)
	{
		const TCHAR* Cursor = FCommandLine::Get(); FString Token; int32 Count = 0;
		while (FParse::Token(Cursor, Token, false))
		{
			if (!Token.RemoveFromStart(TEXT("-"))) { continue; }
			FString Key, Suffix; const bool bHasValue = Token.Split(TEXT("="), &Key, &Suffix);
			if ((bHasValue ? Key : Token).Equals(Name, ESearchCase::IgnoreCase))
			{
				// FParse::Token preserves quotes embedded in -Name="value".
				if (Suffix.Len() >= 2 && Suffix[0] == TEXT('"') && Suffix[Suffix.Len()-1] == TEXT('"')) { Suffix = Suffix.Mid(1,Suffix.Len()-2); }
				++Count; if (Value) { *Value = Suffix; } if (bBare) { *bBare = !bHasValue; }
			}
		}
		return Count;
	}
	TSharedPtr<SWidget> FindCaption(const TSharedRef<SWidget>& Widget, const FText& Caption, int32& Budget, int32 Depth = 0)
	{
		if (--Budget < 0 || Depth > 32 || !Widget->GetVisibility().IsVisible()) { return nullptr; }
		FChildren* Children = Widget->GetChildren();
		if (Widget->GetType() == TEXT("SButton") && Children->Num() == 1)
		{
			const TSharedRef<SWidget> Child = Children->GetChildAt(0);
			if (Child->GetType() == TEXT("STextBlock") && StaticCastSharedRef<STextBlock>(Child)->GetText().ToString() == Caption.ToString()) { return Widget; }
		}
		for (int32 Index = 0; Index < Children->Num() && Budget > 0; ++Index)
		{
			if (TSharedPtr<SWidget> Found = FindCaption(Children->GetChildAt(Index), Caption, Budget, Depth + 1)) { return Found; }
		}
		return nullptr;
	}
}

namespace UEGT2ContractWalkSmoke
{
	void LogFailureSweep(AUEGT2Character* Pawn, const FVector& Station, const FString& Run, int32 Leg, int32 Point)
	{
		if (!IsValid(Pawn) || !Pawn->GetWorld()) { return; }
		const UCapsuleComponent* Capsule = Pawn->GetCapsuleComponent();
		const UCharacterMovementComponent* Movement = Pawn->GetCharacterMovement();
		if (!Capsule || !Movement) { return; }
		const FVector Start = Capsule->GetComponentLocation();
		const float Radius = Capsule->GetScaledCapsuleRadius(), Half = Capsule->GetScaledCapsuleHalfHeight();
		const FQuat Rotation = Capsule->GetComponentQuat();
		const FName Profile = Capsule->GetCollisionProfileName();
		if (!Finite(Start) || !Finite(Station) || Rotation.ContainsNaN() || !Rotation.IsNormalized()
			|| !FMath::IsFinite(Radius) || !FMath::IsFinite(Half) || Radius <= 0 || Half < Radius || Profile.IsNone())
		{
			UE_LOG(LogUEGT2Diag, Log, TEXT("Contract walk failure sweep skipped: run=%s invalid current capsule/target."), *Run);
			return;
		}
		const FHitResult& Floor = Movement->CurrentFloor.HitResult;
		UE_LOG(LogUEGT2Diag, Log, TEXT("Contract walk failure floor: run=%s leg=%d next=P%02d actor=%s component=%s item=%d blocking=%d initial_penetration=%d depth=%.3f location=%s impact=%s normal=%s walkable=%d"),
			*Run,Leg,Point,*GetNameSafe(Floor.GetActor()),*GetNameSafe(Floor.GetComponent()),Floor.Item,Floor.bBlockingHit,
			Floor.bStartPenetrating,Floor.PenetrationDepth,*Floor.Location.ToCompactString(),*Floor.ImpactPoint.ToCompactString(),
			*Floor.ImpactNormal.ToCompactString(),Movement->CurrentFloor.IsWalkableFloor());
		const double Length = FMath::Min(200.0,FVector::Dist2D(Start,Station));
		if (Length < 1.0)
		{
			UE_LOG(LogUEGT2Diag, Log, TEXT("Contract walk failure sweep skipped: run=%s next station is under one centimetre away horizontally."), *Run);
			return;
		}
		const FVector End = Start+(Station-Start).GetSafeNormal2D()*Length;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(ContractWalkFailureSweep),false,Pawn);
		Params.bFindInitialOverlaps = true;
		FHitResult Hit;
		const bool bHit = Pawn->GetWorld()->SweepSingleByProfile(Hit,Start,End,Rotation,Profile,
			FCollisionShape::MakeCapsule(Radius,Half),Params);
		const UStaticMeshComponent* MeshComponent = Cast<UStaticMeshComponent>(Hit.GetComponent());
		UE_LOG(LogUEGT2Diag, Log, TEXT("Contract walk contact identity: run=%s actor_origin=%s mesh=%s"),
			*Run,Hit.GetActor() ? *Hit.GetActor()->GetActorLocation().ToCompactString() : TEXT("None"),
			*GetNameSafe(MeshComponent ? MeshComponent->GetStaticMesh() : nullptr));
		UE_LOG(LogUEGT2Diag, Log, TEXT("Contract walk failure sweep: run=%s leg=%d next=P%02d from=%s to=%s capsule=%.3f/%.3f profile=%s hit=%d actor=%s component=%s item=%d initial_penetration=%d depth=%.3f time=%.5f location=%s impact=%s normal=%s impact_normal=%s walkable_contact=%d same_floor_component=%d. Closest contact only; ground/steps can mask farther obstacles. This does not prove an impassable obstruction."),
			*Run,Leg,Point,*Start.ToCompactString(),*End.ToCompactString(),Radius,Half,*Profile.ToString(),bHit,
			*GetNameSafe(Hit.GetActor()),*GetNameSafe(Hit.GetComponent()),Hit.Item,Hit.bStartPenetrating,Hit.PenetrationDepth,Hit.Time,
			*Hit.Location.ToCompactString(),*Hit.ImpactPoint.ToCompactString(),*Hit.Normal.ToCompactString(),*Hit.ImpactNormal.ToCompactString(),
			bHit && Movement->IsWalkable(Hit),bHit && Hit.GetComponent() == Floor.GetComponent());
	}
}

bool UUEGT2ContractWalkSmokeSubsystem::IsRequested()
{
	return UEGT2ContractWalkSmoke::OptionCount(TEXT("UEGT2ContractWalkSmoke")) > 0;
}
bool UUEGT2ContractWalkSmokeSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	return IsRequested() && Super::ShouldCreateSubsystem(Outer);
}
bool UUEGT2ContractWalkSmokeSubsystem::DoesSupportWorldType(EWorldType::Type Type) const { return Type == EWorldType::Game; }
TStatId UUEGT2ContractWalkSmokeSubsystem::GetStatId() const { RETURN_QUICK_DECLARE_CYCLE_STAT(UUEGT2ContractWalkSmokeSubsystem, STATGROUP_Tickables); }

void UUEGT2ContractWalkSmokeSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld); bRequested = true; StartedAt = FPlatformTime::Seconds();
	using namespace UEGT2ContractWalkSmoke;
	FString UserDirectory; bool bBare = false, bSkipMenuBare = false;
	const bool bFlagValid = OptionCount(TEXT("UEGT2ContractWalkSmoke"), nullptr, &bBare) == 1 && bBare;
	const bool bSkipMenuValid = OptionCount(TEXT("UEGT2SkipMenu"), nullptr, &bSkipMenuBare) == 1 && bSkipMenuBare;
	const int32 UserDirs = OptionCount(TEXT("UserDir"), &UserDirectory);
	FPaths::NormalizeDirectoryName(UserDirectory); RunId = FPaths::GetCleanFilename(UserDirectory);
	FGuid Guid;
	FString Expected = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("Saved/ContractWalkSmoke"), RunId));
	FPaths::NormalizeDirectoryName(Expected);
	if (!Check(bFlagValid && UserDirs == 1 && FGuid::ParseExact(RunId, EGuidFormats::Digits, Guid)
		&& RunId == Guid.ToString(EGuidFormats::Digits).ToLower() && !FPaths::IsRelative(UserDirectory)
		&& UserDirectory.Equals(Expected, ESearchCase::IgnoreCase) && bSkipMenuValid,
		TEXT("requires plain flag, SkipMenu and exact packaged Saved/ContractWalkSmoke/<lowercase-guid> UserDir"))) { return; }
	for (const TCHAR* Other : { TEXT("UEGT2Capture"), TEXT("UEGT2CaptureLife"), TEXT("UEGT2CaptureSquareWashrooms"), TEXT("UEGT2CaptureMenu"),
		TEXT("UEGT2CaptureDialogue"), TEXT("UEGT2SmokeWalk"), TEXT("UEGT2SmokeFly"), TEXT("UEGT2CrossingSmoke"), TEXT("UEGT2AutoWalkSmoke"),
		TEXT("UEGT2ServicesSmoke"), TEXT("UEGT2SurveySmoke"), TEXT("UEGT2RestSmoke"), TEXT("UEGT2HudSizeSmoke"), TEXT("UEGT2ProgressSmoke"),
		TEXT("UEGT2AutosaveSmoke"), TEXT("UEGT2ContractSmoke"), TEXT("UEGT2ProgressSlot"), TEXT("UEGT2ContractSlot"), TEXT("UEGT2Time"), TEXT("UEGT2Weather") })
	{
		if (!Check(OptionCount(Other) == 0, TEXT("full walk cannot share capture/time/weather or another diagnostic"))) { return; }
	}
	PostTickHandle = FWorldDelegates::OnWorldPostActorTick.AddUObject(this, &UUEGT2ContractWalkSmokeSubsystem::ObserveWorld);
	UE_LOG(LogUEGT2Diag, Log, TEXT("Contract walk starting: run=%s route=%s one setup teleport, ordinary input/collision, live clock/needs."), *RunId, RouteRevision);
}

void UUEGT2ContractWalkSmokeSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (bFinished) { return; }
	const double Now = FPlatformTime::Seconds();
	if (!Check(Now - StartedAt <= UEGT2ContractWalkSmoke::MaxSeconds, TEXT("whole circuit exceeded 25 minutes"))) { return; }
	if (Stage == EStage::Startup)
	{
		if (!Check(Now - StartedAt <= 60.0, TEXT("player/world startup exceeded 60 seconds")) || Now - StartedAt < 8.0) { return; }
		AUEGT2PlayerController* PC = Cast<AUEGT2PlayerController>(GetWorld()->GetFirstPlayerController());
		AUEGT2Character* Pawn = PC ? Cast<AUEGT2Character>(PC->GetPawn()) : nullptr;
		UUEGT2NPCDirector* NPCs = UUEGT2NPCDirector::Get(GetWorld());
		if (PC && Pawn && Pawn->HasActorBegunPlay() && PC->GetInputConfig() && Pawn->GetLife()->HasBegunPlay()
			&& NPCs && NPCs->GetPopulation() > 0 && AUEGT2SkyController::Get(GetWorld()) && FSlateApplication::IsInitialized()) { Start(); }
		return;
	}
	if (Stage == EStage::Panel || Stage == EStage::Resume || Stage == EStage::ClaimFocus || Stage == EStage::Paid)
	{
		if (!CheckNormal(true, true) || !Check(Now - StageStartedAt <= 10.0, TEXT("native contract page/focus exceeded ten seconds"))) { return; }
		if (Now - StageStartedAt >= 0.3) { ObservePanel(); }
	}
	else if (Stage == EStage::UsePending)
	{
		// The board can pause the world in PlayerTick. The tickable subsystem
		// releases the real key and observes that page even while actor ticks stop.
		if (!CheckNormal(true, true)) { return; }
		ObserveInteraction();
	}
	else if (Stage == EStage::Settle || Stage == EStage::Brake || Stage == EStage::Focus)
	{
		Check(Now - StageStartedAt <= (Stage == EStage::Focus ? 8.0 : 3.0), TEXT("ground settling, braking or exact probe focus timed out"));
	}
}

bool UUEGT2ContractWalkSmokeSubsystem::ResolveCircuit()
{
	using namespace UEGT2ContractWalkSmoke;
	for (const FVector& Station : Stations) { Points.Add(Station * 100.0); }
	// One-based planning point numbers are converted once, never fed to a router.
	Legs = { {1,2,3,4,33,5,6,27,28,7,8}, {8,7,28,27,6,5,33,4,9,10,11,12,13,14,15,16,31,32,17,18,19,20},
		{20,19,18,17,32,31,16,15,14,13,12,11,10,9,3,21,22,29,30,23,24,25,34,26}, {26,34,25,24,23,30,29,22,21,2,1} };
	for (TArray<int32>& Path : Legs) { for (int32& Index : Path) { --Index; } }
	int32 Boards = 0, Bridges = 0; int32 Counts[3] = {}; Markers.SetNum(3);
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		if (AUEGT2SurveyContract* Actor = Cast<AUEGT2SurveyContract>(*It)) { ++Boards; Board = Actor; }
		if (It->ActorHasTag(BridgeTag)) { ++Bridges; Bridge = Cast<AStaticMeshActor>(*It); }
		if (AUEGT2Landmark* Actor = Cast<AUEGT2Landmark>(*It))
		{
			for (int32 Index = 0; Index < 3; ++Index) { if (Actor->GetPersistentId() == Required[Index]) { ++Counts[Index]; Markers[Index] = Actor; } }
		}
	}
	if (!Check(Boards == 1 && Board.IsValid() && Bridges == 1 && Bridge.IsValid()
		&& Counts[0] == 1 && Counts[1] == 1 && Counts[2] == 1, TEXT("board, required landmarks or tagged bridge missing/ambiguous"))) { return false; }
	const auto RequiredIds = UUEGT2SurveyContractSubsystem::RequiredLandmarkIds();
	if (!Check(RequiredIds.Num() == 3 && RequiredIds[0] == Required[0] && RequiredIds[1] == Required[1] && RequiredIds[2] == Required[2],
		TEXT("contract objectives changed; explicit circuit must be reviewed"))) { return false; }
	const FVector ExpectedActors[] = { {-1200,-2800,0}, {-1200,27871.21875,0}, {16000,27827.357421875,0}, {-6000,-26000,0} };
	const int32 Approaches[] = { 0,7,19,25 }, Neighbours[] = { 1,6,18,24 };
	for (int32 Index = 0; Index < 4; ++Index)
	{
		const AActor* Actor = Index == 0 ? static_cast<AActor*>(Board.Get()) : static_cast<AActor*>(Markers[Index-1].Get());
		if (!Check(Finite(Actor->GetActorLocation()) && FVector::Dist2D(Actor->GetActorLocation(), ExpectedActors[Index]) <= 2.0,
			TEXT("a contract actor moved beyond the route's 2cm identity tolerance"))) { return false; }
		const FVector Approach = Actor->GetActorLocation() + (Points[Neighbours[Index]] - Actor->GetActorLocation()).GetSafeNormal2D() * 150.0;
		Points[Approaches[Index]].X = Approach.X; Points[Approaches[Index]].Y = Approach.Y;
	}
	const UStaticMeshComponent* Component = Bridge->GetStaticMeshComponent();
	const UStaticMesh* Mesh = Component ? Component->GetStaticMesh() : nullptr;
	if (!Check(Mesh && Component->Mobility == EComponentMobility::Static && Component->IsQueryCollisionEnabled()
		&& Component->GetCollisionObjectType() == ECC_WorldStatic && Component->GetCollisionResponseToChannel(ECC_Pawn) == ECR_Block
		&& Bridge->GetActorScale3D().Equals(FVector::OneVector), TEXT("bridge collision or scale is not ordinary static geometry"))) { return false; }
	for (int32 Index = 0; Index < 4; ++Index)
	{
		int32 Count = 0; for (const UStaticMeshSocket* Socket : Mesh->Sockets) { Count += Socket && Socket->SocketName == Sockets[Index] ? 1 : 0; }
		if (!Check(Count == 1 && Component->DoesSocketExist(Sockets[Index]), TEXT("bridge sockets missing/duplicated"))) { return false; }
		const FVector Socket = Component->GetSocketTransform(Sockets[Index], RTS_World).GetLocation();
		if (!Check(Finite(Socket) && FVector::Dist2D(Socket, Points[11+Index]) <= 2.0, TEXT("bridge profile moved beyond the route's 2cm identity tolerance"))) { return false; }
		Points[11+Index] = Socket;
	}
	if (!Check(FMath::Abs(Points[12].Z-Points[13].Z) < 1.0, TEXT("bridge deck is no longer level"))) { return false; }
	LandscapeClass = FindObject<UClass>(nullptr, TEXT("/Script/Landscape.LandscapeProxy"));
	if (!Check(LandscapeClass.IsValid(), TEXT("cooked Landscape class unavailable"))) { return false; }
	for (int32 Side = 0; Side < 2; ++Side)
	{
		const int32 End = Side == 0 ? 11 : 14, Deck = Side == 0 ? 12 : 13, Dry = Side == 0 ? 10 : 15;
		const FVector Hint = Points[End] + (Points[End] - Points[Deck]).GetSafeNormal2D() * 100.0;
		FVector Stand;
		if (!Check(FindStanding(Hint, Points[Dry], Stand), TEXT("bridge dry endpoint lacks normal Landscape capsule support"))) { return false; }
	}
	// The offline 136cm strip covers 100cm steering, the 34cm capsule and
	// at most 2cm displacement after every live approach has been resolved.
	for (int32 Index = 0; Index < Points.Num(); ++Index)
	{
		if (!Check(FVector::Dist2D(Points[Index], Stations[Index] * 100.0) <= 2.0,
			TEXT("resolved circuit differs from the checked corridor; regenerate and review the route"))) { return false; }
	}
	for (int32 Index = 0; Index < Legs.Num(); ++Index)
	{
		double Length = 0;
		for (int32 Step = 1; Step < Legs[Index].Num(); ++Step)
		{
			const double Part = FVector::Dist2D(Points[Legs[Index][Step-1]], Points[Legs[Index][Step]]);
			if (!Check(Part >= 20.0 && Part <= 30000.0, TEXT("route contains a degenerate or unexpectedly long segment"))) { return false; }
			Length += Part;
		}
		UE_LOG(LogUEGT2Diag, Log, TEXT("Contract route leg=%d stations=%d horizontal_m=%.3f"), Index+1, Legs[Index].Num(), Length/100.0);
	}
	return true;
}

bool UUEGT2ContractWalkSmokeSubsystem::FindStanding(const FVector& Hint, FVector& Ground, FVector& Stand) const
{
	const UCapsuleComponent* Capsule = Player->GetCapsuleComponent();
	const float Half = Capsule->GetScaledCapsuleHalfHeight();
	const FCollisionShape Shape = FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius(), Half);
	const FCollisionQueryParams Params(SCENE_QUERY_STAT(ContractWalkSetup), false, Player.Get());
	FHitResult Hit;
	if (!GetWorld()->LineTraceSingleByObjectType(Hit, Hint+FVector(0,0,120), Hint-FVector(0,0,120),
		FCollisionObjectQueryParams(ECC_WorldStatic), Params) || !Hit.GetActor() || !Hit.GetActor()->IsA(LandscapeClass.Get())) { return false; }
	Ground = Hit.ImpactPoint; const FVector Center = Ground+FVector(0,0,Half);
	if (!GetWorld()->SweepSingleByProfile(Hit, Center+FVector(0,0,45), Center-FVector(0,0,45), FQuat::Identity,
		Capsule->GetCollisionProfileName(), Shape, Params) || Hit.bStartPenetrating || !Hit.GetActor()
		|| !Hit.GetActor()->IsA(LandscapeClass.Get()) || !Player->GetCharacterMovement()->IsWalkable(Hit)) { return false; }
	Stand = Hit.Location+FVector(0,0,UEGT2ContractWalkSmoke::FloorGap);
	return !GetWorld()->OverlapBlockingTestByProfile(Stand, FQuat::Identity, Capsule->GetCollisionProfileName(), Shape, Params);
}

void UUEGT2ContractWalkSmokeSubsystem::Start()
{
	Controller = Cast<AUEGT2PlayerController>(GetWorld()->GetFirstPlayerController());
	Player = Cast<AUEGT2Character>(Controller->GetPawn()); Sky = AUEGT2SkyController::Get(GetWorld());
	Director = UUEGT2NPCDirector::Get(GetWorld()); Contract = UUEGT2SurveyContractSubsystem::Get(GetWorld());
	const UUEGT2NeedsComponent* Life = Player->GetLife();
	ForwardKey = UUEGT2InputConfig::GetEffectiveKey(EUEGT2InputSlot::MoveForward);
	InteractKey = UUEGT2InputConfig::GetEffectiveKey(EUEGT2InputSlot::Interact);
	UUEGT2ProgressSubsystem* Progress = UUEGT2ProgressSubsystem::Get(GetWorld());
	if (!Check(Progress && !Progress->IsAvailable() && Progress->IsJourneyActive(GetWorld()) && Contract.IsValid() && Contract->IsEnabled()
		&& !Contract->IsPaid() && AUEGT2Landmark::GetDiscoveredCount(GetWorld()) == 0
		&& ForwardKey.IsValid() && InteractKey.IsValid() && ForwardKey != InteractKey
		&& Controller->GetInputConfig()->MoveAction && Controller->GetInputConfig()->InteractAction,
		TEXT("fresh active journey, diagnostic persistence exclusion, contract or mapped inputs unavailable"))) { return; }
	Rate = Director->GetWorldHoursPerSecond(); PreviousExertion = LastObservedExertion = Life->GetExertionScale();
	InitialNeeds = Life->GetNeeds(); InitialPurse = Life->GetPurse(); InitialTrade = Life->GetTrade();
	const FUEGT2NPCNeeds ArrivalNeeds; // Company intentionally starts at 0.6.
	if (!Check(InitialTrade == EUEGT2NPCRole::Villager && InitialPurse.Coins == UEGT2StartingCoins(InitialTrade)
		&& UEGT2ContractWalkSmoke::SameNeeds(InitialNeeds,ArrivalNeeds,0.05f),
		TEXT("fresh arrival life was modified before the circuit")) || !ResolveCircuit() || !CheckNormal(false, false)) { return; }
	FVector Ground, Stand;
	if (!Check(FindStanding(Points[0], Ground, Stand), TEXT("sign approach lacks normal dry standing capsule clearance"))) { return; }
	const FRotator View = (Board->GetInteractionPoint() - (Stand+FVector(0,0,68))).Rotation();
	// The only transform/velocity/mode setup. No route point may ever rescue a
	// blocked capsule. Falling must settle naturally onto the checked ground.
	if (!Check(Player->TeleportTo(Stand, FRotator(0,View.Yaw,0), false, false) && Player->GetActorLocation().Equals(Stand,1.0),
		TEXT("single setup teleport failed or adjusted the checked stand"))) { return; }
	Player->GetCharacterMovement()->StopMovementImmediately(); Player->GetCharacterMovement()->SetMovementMode(MOVE_Falling);
	Controller->SetControlRotation(View);
	WorldStartSeconds = LastWorldSeconds = GetWorld()->GetTimeSeconds(); StartSkyHour = LastSkyHour = Sky->GetTimeOfDay();
	StartDay = Director->GetDayIndex() + (LastSkyHour+12.0f < Director->GetHour() ? 1 : 0);
	LastPosition = Stand; bStarted = true; SetStage(EStage::Settle); LogState(TEXT("setup"));
}

bool UUEGT2ContractWalkSmokeSubsystem::CheckNormal(bool bWalking, bool bAllowPanel)
{
	using namespace UEGT2ContractWalkSmoke;
	if (!Check(Player.IsValid() && Controller.IsValid() && Director.IsValid() && Sky.IsValid() && Contract.IsValid()
		&& Board.IsValid() && Board->GetWorld() == GetWorld() && Bridge.IsValid() && Bridge->GetWorld() == GetWorld()
		&& Finite(Board->GetActorLocation()) && Controller->GetPawn() == Player.Get() && Player->GetWorld() == GetWorld()
		&& Player->GetController() == Controller.Get(), TEXT("circuit world, possession or actors changed"))) { return false; }
	for (int32 Index = 0; Index < Markers.Num(); ++Index)
	{
		if (!Check(Markers[Index].IsValid() && Markers[Index]->GetWorld() == GetWorld() && Finite(Markers[Index]->GetActorLocation())
			&& Markers[Index]->GetPersistentId() == Required[Index],
			TEXT("required landmark destroyed or reconfigured"))) { return false; }
	}
	const AUEGT2Character* Defaults = GetDefault<AUEGT2Character>();
	const UCapsuleComponent* Capsule = Player->GetCapsuleComponent();
	const UCharacterMovementComponent* Movement = Player->GetCharacterMovement();
	const UCharacterMovementComponent* DefaultMovement = Defaults->GetCharacterMovement();
	const UUEGT2NeedsComponent* Life = Player->GetLife();
	const float Exertion = Life->GetExertionScale();
	const UGameViewportClient* Viewport = GetWorld()->GetGameViewport();
	const UConsole* Console = Viewport ? Viewport->ViewportConsole.Get() : nullptr;
	UUEGT2ProgressSubsystem* Progress = UUEGT2ProgressSubsystem::Get(GetWorld());
	if (!Check(UUEGT2NeedsComponent::IsValidProgress(Life->GetNeeds(), Life->GetPurse(), Life->GetTrade())
		&& Finite(Player->GetActorLocation()) && Finite(Player->GetVelocity()) && !Controller->GetControlRotation().ContainsNaN()
		&& !Life->IsOccupied() && (Life->GetActivity() == EUEGT2Activity::Idle || Life->GetActivity() == EUEGT2Activity::Stroll)
		&& !Player->IsGodMode() && !Player->IsFlyEnabled() && !Player->IsNoclipEnabled() && !Player->IsAutoWalking()
		&& !Player->IsSprinting() && !Player->bIsCrouched && Player->GetActorEnableCollision()
		&& FMath::IsNearlyEqual(Player->GetSpeedMultiplier(),1.0f) && FMath::IsNearlyEqual(Player->WalkSpeed,380.0f)
		&& FMath::IsNearlyEqual(Capsule->GetScaledCapsuleRadius(),34.0f) && FMath::IsNearlyEqual(Capsule->GetUnscaledCapsuleRadius(),34.0f)
		&& FMath::IsNearlyEqual(Capsule->GetScaledCapsuleHalfHeight(),90.0f) && FMath::IsNearlyEqual(Capsule->GetUnscaledCapsuleHalfHeight(),90.0f)
		&& Capsule->GetComponentScale().Equals(FVector::OneVector) && Capsule->GetCollisionEnabled() == Defaults->GetCapsuleComponent()->GetCollisionEnabled()
		&& Capsule->GetCollisionProfileName() == Defaults->GetCapsuleComponent()->GetCollisionProfileName()
		&& FMath::IsNearlyEqual(Movement->MaxStepHeight,45.0f) && FMath::IsNearlyEqual(Movement->GetWalkableFloorAngle(),50.0f)
		&& FMath::IsNearlyEqual(Movement->MaxAcceleration,DefaultMovement->MaxAcceleration)
		&& FMath::IsNearlyEqual(Movement->BrakingDecelerationWalking,DefaultMovement->BrakingDecelerationWalking)
		&& FMath::IsNearlyEqual(Movement->GroundFriction,DefaultMovement->GroundFriction)
		&& FMath::IsNearlyEqual(Movement->GravityScale,DefaultMovement->GravityScale)
		&& Movement->MaxWalkSpeed >= Player->WalkSpeed * FMath::Min(PreviousExertion,Exertion)-0.1f
		&& Movement->MaxWalkSpeed <= Player->WalkSpeed * FMath::Max(PreviousExertion,Exertion)+0.1f
		&& Player->GetInteraction()->Reach == Defaults->GetInteraction()->Reach && Player->GetInteraction()->ProbeRadius == Defaults->GetInteraction()->ProbeRadius
		&& Life->TiredEnergy == Defaults->GetLife()->TiredEnergy && Life->WornOutScale == Defaults->GetLife()->WornOutScale
		&& Controller->IsLocalController() && !Controller->IsDialogueOpen() && !(Console && Console->ConsoleActive())
		&& Progress && !Progress->IsAvailable() && Progress->IsJourneyActive(GetWorld()) && Contract->IsEnabled()
		&& Sky->IsDayNightCycleEnabled() && FMath::IsFinite(Sky->GetTimeOfDay()) && Sky->GetTimeOfDay() >= 0.0f && Sky->GetTimeOfDay() < 24.0f
		&& Sky->GetDayLengthMinutes() == 20.0f && FMath::IsFinite(Rate) && Rate > 0 && FMath::IsNearlyEqual(Director->GetWorldHoursPerSecond(),Rate,0.000001f)
		&& !Director->IsFrozen() && !Director->AreSchedulesPaused() && Director->GetPopulation() > 0
		&& FMath::IsNearlyEqual(GetWorld()->GetWorldSettings()->GetEffectiveTimeDilation(),1.0f),
		TEXT("ordinary movement, input, live-clock or ledger invariant changed"))) { return false; }
	if (!bAllowPanel && !Check(!GetWorld()->IsPaused() && Controller->GetMenuState() == EUEGT2MenuState::None
		&& !Controller->IsMoveInputIgnored(), TEXT("unexpected pause or input owner during the route"))) { return false; }
	return !bWalking || Check(Movement->MovementMode == MOVE_Walking && Movement->CurrentFloor.IsWalkableFloor(), TEXT("ordinary walking lost its walkable floor"));
}

bool UUEGT2ContractWalkSmokeSubsystem::CheckDiscoveries() const
{
	for (int32 Index = 0; Index < Markers.Num(); ++Index) { if (!Markers[Index].IsValid() || Markers[Index]->IsDiscovered() != (Index < Surveyed)) { return false; } }
	return true;
}

void UUEGT2ContractWalkSmokeSubsystem::ObserveWorld(UWorld* World, ELevelTick TickType, float DeltaTime)
{
	if (World != GetWorld() || bFinished || !bStarted) { return; }
	// Keep the previous completed actor frame's exertion until this frame is
	// observed. The subsystem also ticks around paused UI work; those calls must
	// not discard the allowed Character/Needs component tick-order interval.
	PreviousExertion = LastObservedExertion;
	const bool bPanel = Stage == EStage::UsePending || Stage == EStage::Panel || Stage == EStage::Resume || Stage == EStage::ClaimFocus || Stage == EStage::Paid;
	if (!CheckNormal(Stage != EStage::Settle, bPanel)) { return; }
	LastObservedExertion = Player->GetLife()->GetExertionScale();
	const double Now = FPlatformTime::Seconds(), GameSeconds = World->GetTimeSeconds();
	const double Elapsed = GameSeconds-LastWorldSeconds; LastWorldSeconds = GameSeconds;
	if (!Check(FMath::IsFinite(DeltaTime) && DeltaTime >= 0 && Elapsed >= 0 && Elapsed < 10.0, TEXT("invalid or excessive single-frame simulation time"))) { return; }
	if (Elapsed > 0)
	{
		WorldHours += Elapsed * Rate; WorstFrame = FMath::Max(WorstFrame,DeltaTime); ++Frames;
		float SkyDelta = Sky->GetTimeOfDay()-LastSkyHour; if (SkyDelta < -12.0f) { SkyDelta += 24.0f; }
		// Sky accumulates float hours each rendered frame. Permit only its local
		// rounding error; compare the complete journey separately below.
		if (!Check(SkyDelta >= 0 && FMath::Abs(SkyDelta-Elapsed*Rate) <= 0.00001,
			TEXT("live sky stopped, scrubbed or diverged from elapsed simulation time"))) { return; }
		ObservedClockHours += SkyDelta; LastSkyHour = Sky->GetTimeOfDay();
	}
	const FVector Position = Player->GetActorLocation();
	if (Stage != EStage::Settle)
	{
		Distance2D += FVector::Dist2D(Position,LastPosition); Distance3D += FVector::Dist(Position,LastPosition);
		if (Stage == EStage::Travel || Stage == EStage::Brake)
		{
			LegDistance2D += FVector::Dist2D(Position,LastPosition); LegDistance3D += FVector::Dist(Position,LastPosition);
		}
	}
	LastPosition = Position;
	const UUEGT2NeedsComponent* Life = Player->GetLife();
	if (Stage != EStage::Paid && !Check(Life->GetPurse().Coins == InitialPurse.Coins && Life->GetTrade() == InitialTrade && !Contract->IsPaid(),
		TEXT("ordinary route changed the purse, trade or unpaid contract"))) { return; }
	const float Needs[] = { Life->GetNeeds().Energy, Life->GetNeeds().Fed, Life->GetNeeds().Relief, Life->GetNeeds().Company };
	for (int32 Index = 0; Index < 4; ++Index)
	{
		if (Needs[Index] == 0 && !(Depleted & (1 << Index)))
		{
			Depleted |= 1 << Index; UE_LOG(LogUEGT2Diag, Log, TEXT("Contract walk need reached zero: run=%s need=%d worldHours=%.5f leg=%d point=%d"), *RunId,Index,WorldHours,Leg+1,Segment);
		}
	}
	if (Now-LastLogAt >= 1.0) { LogState(TEXT("live")); LastLogAt = Now; }
	if (Stage == EStage::Travel) { ObserveTravel(); }
	else if (Stage == EStage::Settle || Stage == EStage::Brake)
	{
		if (Player->GetCharacterMovement()->IsMovingOnGround() && Player->GetHorizontalSpeed() < 5.0f)
		{
			const int32 Endpoint = Stage == EStage::Settle ? 0 : Legs[Leg].Last();
			if (!Check(FVector::Dist2D(Position,Points[Endpoint]) <= 100.0, TEXT("ordinary braking escaped its interaction approach"))) { return; }
			if (Stage == EStage::Brake)
			{
				++CompletedLegs;
				UE_LOG(LogUEGT2Diag, Log, TEXT("Contract walking leg complete: run=%s leg=%d real_seconds=%.3f world_hours=%.5f horizontal_m=%.3f surface_m=%.3f"),
					*RunId,Leg+1,Now-LegStartedAt,WorldHours-LegWorldStart,LegDistance2D/100.0,LegDistance3D/100.0);
				if (!CheckLedger()) { return; }
			}
			BeginInteraction();
		}
	}
	else if (Stage == EStage::Focus) { ObserveInteraction(); }
}

void UUEGT2ContractWalkSmokeSubsystem::BeginLeg()
{
	++Leg;
	if (!Check(Legs.IsValidIndex(Leg) && CheckDiscoveries(), TEXT("leg or surveyed set invalid"))) { return; }
	Segment = 0; SegmentBase = BestProgress = LegDistance2D = LegDistance3D = 0;
	LegStartedAt = LastProgressAt = FPlatformTime::Seconds(); LegWorldStart = WorldHours;
	double Length = 0;
	for (int32 Index = 1; Index < Legs[Leg].Num(); ++Index) { Length += FVector::Dist2D(Points[Legs[Leg][Index-1]],Points[Legs[Leg][Index]]); }
	LegDeadline = 1.25*Length/(Player->WalkSpeed*Player->GetLife()->WornOutScale)+30.0;
	Controller->SetControlRotation(FRotator(0,(Points[Legs[Leg][1]]-Player->GetActorLocation()).Rotation().Yaw,0));
	SetStage(EStage::Travel); Key(ForwardKey,IE_Pressed); LogState(TEXT("leg started"));
}

void UUEGT2ContractWalkSmokeSubsystem::ObserveTravel()
{
	using namespace UEGT2ContractWalkSmoke;
	const int32 A = Legs[Leg][Segment], B = Legs[Leg][Segment+1];
	const FVector From = Points[A], To = Points[B], Direction = (To-From).GetSafeNormal2D(), Position = Player->GetActorLocation();
	const double Length = FVector::Dist2D(From,To), Now = FPlatformTime::Seconds();
	LastAlong = FVector::DotProduct(Position-From,Direction);
	LastCross = FMath::Abs(FVector::DotProduct(Position-From,FVector(-Direction.Y,Direction.X,0)));
	WorstCross = FMath::Max(WorstCross,LastCross);
	if (!Check(LastCross <= 100.0 && LastAlong >= -100.0 && LastAlong <= Length+100.0, TEXT("capsule escaped the checked 100cm route corridor"))
		|| !Check(Now-LegStartedAt <= LegDeadline, TEXT("walking leg exceeded its distance-based ordinary-fatigue deadline"))
		|| !Check(CheckDiscoveries(), TEXT("surveyed set changed outside the target interaction"))) { return; }
	const FHitResult& Floor = Player->GetCharacterMovement()->CurrentFloor.HitResult;
	const int32 Low = FMath::Min(A,B);
	if (FMath::Abs(A-B) == 1 && Low >= 11 && Low <= 13 && LastAlong > SeamMargin && LastAlong < Length-SeamMargin)
	{
		if (!Check(Floor.GetActor() == Bridge.Get(), TEXT("bridge ramp/deck floor belongs to another actor"))) { return; }
		++BridgeSamples[Leg-1][Low-11];
		if (Low == 12 && !Check(Position.Z-Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() >= Points[12].Z-5.0,
			TEXT("walking capsule feet are below the deck"))) { return; }
	}
	if (FMath::Abs(A-B) == 1 && (Low == 10 || Low == 14))
	{
		const bool bDry = Low == 10 ? (A == 10 ? LastAlong < Length-SeamMargin : LastAlong > SeamMargin)
			: (A == 14 ? LastAlong > SeamMargin : LastAlong < Length-SeamMargin);
		if (bDry && !Check(Floor.GetActor() && Floor.GetActor()->IsA(LandscapeClass.Get()), TEXT("bridge dry approach is not supported by Landscape"))) { return; }
	}
	const double Progress = SegmentBase+FMath::Clamp(LastAlong,0.0,Length);
	if (Progress >= BestProgress+25.0) { BestProgress = Progress; LastProgressAt = Now; }
	if (!Check(Now-LastProgressAt <= 3.0, TEXT("first blocked segment: less than 25cm forward progress in three seconds"))) { return; }
	if (LastAlong >= Length-10.0)
	{
		UE_LOG(LogUEGT2Diag, Log, TEXT("Contract route station: run=%s leg=%d P%02d location=%s cross_cm=%.2f floor=%s"),
			*RunId,Leg+1,B+1,*Position.ToCompactString(),LastCross,*GetNameSafe(Floor.GetActor()));
		SegmentBase += Length; ++Segment;
		if (Segment == Legs[Leg].Num()-1)
		{
			Key(ForwardKey,IE_Released);
			if ((Leg == 1 || Leg == 2) && !Check(BridgeSamples[Leg-1][0] > 0 && BridgeSamples[Leg-1][1] > 0 && BridgeSamples[Leg-1][2] > 0,
				TEXT("leg did not physically observe both bridge ramps and deck"))) { return; }
			SetStage(EStage::Brake); LogState(TEXT("braking")); return;
		}
	}
	Controller->SetControlRotation(FRotator(0,(Points[Legs[Leg][Segment+1]]-Position).Rotation().Yaw,0));
}

AUEGT2InteractableActor* UUEGT2ContractWalkSmokeSubsystem::InteractionTarget() const
{
	return Leg >= 0 && Leg < 3 ? static_cast<AUEGT2InteractableActor*>(Markers[Leg].Get()) : static_cast<AUEGT2InteractableActor*>(Board.Get());
}
void UUEGT2ContractWalkSmokeSubsystem::BeginInteraction()
{
	if (!Check(CheckDiscoveries() && IsValid(InteractionTarget()), TEXT("interaction target or surveyed set changed"))) { return; }
	Controller->SetControlRotation((InteractionTarget()->GetInteractionPoint()-Player->GetPawnViewLocation()).Rotation());
	SetStage(EStage::Focus); LogState(TEXT("waiting for exact probe"));
}
void UUEGT2ContractWalkSmokeSubsystem::ObserveInteraction()
{
	AUEGT2InteractableActor* Target = InteractionTarget();
	if (!Check(IsValid(Target) && FPlatformTime::Seconds()-StageStartedAt <= 8.0, TEXT("real mapped interaction did not reach its target within eight seconds"))) { return; }
	if (Stage == EStage::Focus)
	{
		if (Player->GetInteraction()->GetFocusedActor() != Target) { return; }
		Key(InteractKey,IE_Pressed); SetStage(EStage::UsePending); return;
	}
	if (Target == Board.Get())
	{
		if (!Controller->IsSurveyContractOpen()) { return; }
		Key(InteractKey,IE_Released);
		if (!Check(Controller->GetSurveyContractBoard() == Board.Get() && GetWorld()->IsPaused(), TEXT("real board use opened the wrong/unpaused page"))) { return; }
		SetStage(EStage::Panel);
	}
	else if (Markers[Leg]->IsDiscovered())
	{
		Key(InteractKey,IE_Released); ++Surveyed;
		if (!Check(CheckDiscoveries() && Player->GetLife()->GetPurse().Coins == InitialPurse.Coins && !Contract->IsPaid(), TEXT("marker use changed more than its discovery"))) { return; }
		UE_LOG(LogUEGT2Diag, Log, TEXT("Contract route surveyed: run=%s id=%s via=mapped_input coins=%.6f"), *RunId,*Markers[Leg]->GetPersistentId().ToString(),InitialPurse.Coins);
		BeginLeg();
	}
}

TSharedPtr<SWidget> UUEGT2ContractWalkSmokeSubsystem::FindButton(const FText& Caption) const
{
	TSharedPtr<SWidget> Root = FSlateApplication::Get().GetKeyboardFocusedWidget();
	for (int32 Depth = 0; Root.IsValid() && Root->GetType() != TEXT("SUEGT2Menu") && Depth < 32; ++Depth) { Root = Root->GetParentWidget(); }
	int32 Budget = 512; return Root.IsValid() ? UEGT2ContractWalkSmoke::FindCaption(Root.ToSharedRef(),Caption,Budget) : nullptr;
}
bool UUEGT2ContractWalkSmokeSubsystem::HasFocus(const FText& Caption) const
{
	const TSharedPtr<SWidget> Button = FindButton(Caption);
	return Button.IsValid() && Button->IsEnabled() && Button == FSlateApplication::Get().GetKeyboardFocusedWidget();
}
bool UUEGT2ContractWalkSmokeSubsystem::SlateKey(FKey Which)
{
	const FKeyEvent Event(Which,FModifierKeysState(),0,false,0,0);
	const bool Down = FSlateApplication::Get().ProcessKeyDownEvent(Event), Up = FSlateApplication::Get().ProcessKeyUpEvent(Event);
	UE_LOG(LogUEGT2Diag, Log, TEXT("Contract route native key: %s down=%d up=%d"), *Which.ToString(),Down,Up);
	return Which != EKeys::Gamepad_FaceButton_Bottom || Check(Down && Up,TEXT("native focused button did not accept gamepad A"));
}
void UUEGT2ContractWalkSmokeSubsystem::ObservePanel()
{
	using namespace UEGT2ContractWalkSmoke;
	const FText Resume = NSLOCTEXT("UEGT2SurveyContract","Resume","Resume");
	const FText Claim = NSLOCTEXT("UEGT2SurveyContract","Claim","Claim Payment");
	const FText Paid = NSLOCTEXT("UEGT2SurveyContract","PaidButton","Paid");
	if (Stage == EStage::Resume)
	{
		if (GetWorld()->IsPaused() || Controller->IsMenuOpen()) { return; }
		bInitialPanelDone = true; BeginLeg(); return;
	}
	if (!Check(Controller->IsSurveyContractOpen() && Controller->GetSurveyContractBoard() == Board.Get() && GetWorld()->IsPaused()
		&& CheckDiscoveries(), TEXT("native board page ownership or discoveries changed"))) { return; }
	if (Stage == EStage::Panel)
	{
		if (!HasFocus(Resume)) { return; } // Allow the newly built Slate tree to arrange.
		const TSharedPtr<SWidget> Button = FindButton(Claim);
		if (!Button.IsValid() || Button->GetCachedGeometry().GetLocalSize().IsNearlyZero()) { return; }
		const auto Entries = Contract->GetEntries(Controller.Get());
		if (!Check(Entries.Num() == 3 && Button->IsEnabled() == bInitialPanelDone, TEXT("board checklist/claim state does not match the physical journey"))) { return; }
		for (int32 Index = 0; Index < Entries.Num(); ++Index)
		{
			if (!Check(Entries[Index].Id == Required[Index] && Entries[Index].bAvailable && Entries[Index].bDiscovered == bInitialPanelDone,
				TEXT("board checklist differs from actual marker discoveries"))) { return; }
		}
		if (!bInitialPanelDone)
		{
			if (!Check(!Contract->IsPaid() && Surveyed == 0, TEXT("initial contract is not unfinished"))) { return; }
			if (SlateKey(EKeys::Gamepad_FaceButton_Bottom)) { SetStage(EStage::Resume); } return;
		}
		if (!Check(CompletedLegs == 4 && Surveyed == 3, TEXT("returned without the complete walking circuit")) || !CheckLedger()) { return; }
		ClaimNeeds = Player->GetLife()->GetNeeds(); ClaimPurse = Player->GetLife()->GetPurse();
		ClaimDay = Director->GetDayIndex(); ClaimHour = Director->GetHour(); ClaimSkyHour = Sky->GetTimeOfDay(); ClaimWeather = Sky->GetWeather();
		SlateKey(EKeys::Gamepad_DPad_Right); SetStage(EStage::ClaimFocus);
	}
	else if (Stage == EStage::ClaimFocus)
	{
		if (!HasFocus(Claim)) { return; }
		if (SlateKey(EKeys::Gamepad_FaceButton_Bottom)) { SetStage(EStage::Paid); }
	}
	else if (Stage == EStage::Paid)
	{
		const TSharedPtr<SWidget> Button = FindButton(Paid);
		if (!Button.IsValid() || !HasFocus(Resume)) { return; }
		FUEGT2Purse Expected = ClaimPurse;
		if (!Check(UEGT2TryCredit(Contract->GetReward(),Expected) && Contract->IsPaid() && !Button->IsEnabled()
			&& Player->GetLife()->GetPurse().Coins == Expected.Coins && SameNeeds(Player->GetLife()->GetNeeds(),ClaimNeeds,0.0f)
			&& Player->GetLife()->GetTrade() == InitialTrade && Director->GetDayIndex() == ClaimDay && Director->GetHour() == ClaimHour
			&& Sky->GetTimeOfDay() == ClaimSkyHour && Sky->GetWeather() == ClaimWeather,
			TEXT("native Claim did not atomically pay the shared reward while preserving paused state"))) { return; }
		Finish(true,TEXT("ordinary circuit, real surveys and native return-to-board payment verified"));
	}
}

bool UUEGT2ContractWalkSmokeSubsystem::CheckLedger()
{
	using namespace UEGT2ContractWalkSmoke;
	FUEGT2NPCNeeds Expected = InitialNeeds; FUEGT2Purse Purse = InitialPurse;
	// Idle and Stroll share the ordinary ledger rates. One reference integration
	// avoids accumulating a second frame-by-frame model of the implementation.
	const bool bPaid = UEGT2AdvanceLife(static_cast<float>(WorldHours),EUEGT2Activity::Stroll,InitialTrade,Expected,Purse);
	const int32 FinalDay = Director->GetDayIndex() + (Sky->GetTimeOfDay()+12.0f < Director->GetHour() ? 1 : 0);
	const double CalendarHours = (FinalDay-StartDay)*24.0 + Sky->GetTimeOfDay() - StartSkyHour;
	// Needs tick at 0.1s, so initial/final component phases differ by at most a
	// tick. 0.001 also covers accumulated float subtraction over this bounded run.
	const bool bLife = bPaid && SameNeeds(Player->GetLife()->GetNeeds(),Expected,0.001f) && Purse.Coins == InitialPurse.Coins;
	const bool bClock = WorldHours > 1.0 && ObservedClockHours > 1.0 && FMath::Abs(ObservedClockHours-WorldHours) <= FMath::Max(0.01,Frames*0.000002)
		&& FMath::Abs(CalendarHours-ObservedClockHours) < 0.001;
	UE_LOG(LogUEGT2Diag, Log, TEXT("Contract ledger comparison: run=%s worldHours=%.6f skyHours=%.6f calendarHours=%.6f expected=%.6f/%.6f/%.6f/%.6f actual=%.6f/%.6f/%.6f/%.6f match=%d/%d"),
		*RunId,WorldHours,ObservedClockHours,CalendarHours,Expected.Energy,Expected.Fed,Expected.Relief,Expected.Company,
		Player->GetLife()->GetNeeds().Energy,Player->GetLife()->GetNeeds().Fed,Player->GetLife()->GetNeeds().Relief,Player->GetLife()->GetNeeds().Company,bLife,bClock);
	return Check(bLife && bClock,TEXT("live journey did not match the shared elapsed needs/calendar ledger"));
}

void UUEGT2ContractWalkSmokeSubsystem::Key(FKey Which, EInputEvent Event)
{
	if (!Controller.IsValid() || !Which.IsValid()) { return; }
	const FInputDeviceId Device = IPlatformInputDeviceMapper::Get().GetPrimaryInputDeviceForUser(Controller->GetPlatformUserId());
	Controller->InputKey(FInputKeyEventArgs(nullptr,Device,Which,Event,FPlatformTime::Cycles64()));
	if (Which == ForwardKey) { bForwardHeld = Event == IE_Pressed; }
	if (Which == InteractKey) { bInteractHeld = Event == IE_Pressed; }
}
void UUEGT2ContractWalkSmokeSubsystem::SetStage(EStage Next) { Stage = Next; StageStartedAt = FPlatformTime::Seconds(); }
void UUEGT2ContractWalkSmokeSubsystem::LogState(const TCHAR* Reason) const
{
	if (!Player.IsValid()) { return; }
	const UCharacterMovementComponent* Movement = Player->GetCharacterMovement();
	const UUEGT2NeedsComponent* Life = Player->GetLife(); const FUEGT2NPCNeeds& Needs = Life->GetNeeds();
	const FHitResult& Floor = Movement->CurrentFloor.HitResult;
	const int32 Point = Legs.IsValidIndex(Leg) && Legs[Leg].IsValidIndex(Segment+1) ? Legs[Leg][Segment+1]+1 : 0;
	UE_LOG(LogUEGT2Diag, Log, TEXT("Contract walk state: run=%s %s stage=%d leg=%d next=P%02d wall=%.3f active=%.3f hours=%.6f day=%d sky=%.6f location=%s speed=%.3f max=%.3f exertion=%.6f mode=%d floor=%s component=%s normal=%s along=%.2f cross=%.2f needs=%.6f/%.6f/%.6f/%.6f coins=%.6f surveyed=%d paid=%d focused=%s"),
		*RunId,Reason,static_cast<int32>(Stage),Leg+1,Point,FPlatformTime::Seconds()-StartedAt,GetWorld()->GetTimeSeconds()-WorldStartSeconds,WorldHours,
		Director.IsValid()?Director->GetDayIndex():-1,Sky.IsValid()?Sky->GetTimeOfDay():-1.0f,*Player->GetActorLocation().ToCompactString(),Player->GetHorizontalSpeed(),
		Movement->MaxWalkSpeed,Life->GetExertionScale(),static_cast<int32>(Movement->MovementMode),*GetNameSafe(Floor.GetActor()),*GetNameSafe(Floor.GetComponent()),
		*Floor.ImpactNormal.ToCompactString(),LastAlong,LastCross,Needs.Energy,Needs.Fed,Needs.Relief,Needs.Company,Life->GetPurse().Coins,Surveyed,
		Contract.IsValid()&&Contract->IsPaid(),*GetNameSafe(Player->GetInteraction()->GetFocusedActor()));
}
bool UUEGT2ContractWalkSmokeSubsystem::Check(bool bCondition, const TCHAR* Reason) { if (!bCondition) { Finish(false,Reason); } return bCondition; }
void UUEGT2ContractWalkSmokeSubsystem::Cleanup()
{
	if (bForwardHeld) { Key(ForwardKey,IE_Released); }
	if (bInteractHeld) { Key(InteractKey,IE_Released); }
	FWorldDelegates::OnWorldPostActorTick.Remove(PostTickHandle); PostTickHandle.Reset();
}
void UUEGT2ContractWalkSmokeSubsystem::Finish(bool bSuccess, const TCHAR* Reason)
{
	if (bFinished) { return; } bFinished = true; LogState(Reason);
	if (!bSuccess && Stage == EStage::Travel && Legs.IsValidIndex(Leg) && Legs[Leg].IsValidIndex(Segment+1))
	{
		const int32 Point = Legs[Leg][Segment+1];
		UEGT2ContractWalkSmoke::LogFailureSweep(Player.Get(), Points[Point], RunId, Leg+1, Point+1);
	}
	Cleanup();
	if (bSuccess)
	{
		UE_LOG(LogUEGT2Diag, Log, TEXT("UEGT2_CONTRACT_WALK_SMOKE_COMPLETE run=%s legs=%d surveyed=%d horizontal_m=%.3f surface_m=%.3f world_hours=%.6f wall_seconds=%.3f frames=%d worst_ms=%.3f worst_cross_cm=%.3f bridge_samples=%d/%d/%d,%d/%d/%d depleted_mask=%d reward=%.6f %s"),
			*RunId,CompletedLegs,Surveyed,Distance2D/100.0,Distance3D/100.0,WorldHours,FPlatformTime::Seconds()-StartedAt,Frames,WorstFrame*1000.0f,WorstCross,
			BridgeSamples[0][0],BridgeSamples[0][1],BridgeSamples[0][2],BridgeSamples[1][0],BridgeSamples[1][1],BridgeSamples[1][2],Depleted,Contract->GetReward(),Reason);
	}
	else { UE_LOG(LogUEGT2Diag, Error, TEXT("UEGT2_CONTRACT_WALK_SMOKE_FAILED run=%s leg=%d segment=%d stage=%d %s"),*RunId,Leg+1,Segment,static_cast<int32>(Stage),Reason); }
	FPlatformMisc::RequestExitWithStatus(false,bSuccess?0:1);
}
void UUEGT2ContractWalkSmokeSubsystem::Deinitialize()
{
	if (bRequested && !bFinished) { Finish(false,TEXT("world ended before the complete circuit")); }
	else { Cleanup(); }
	Super::Deinitialize();
}
