#include "Diagnostics/UEGT2CrossingSmokeSubsystem.h"

#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Diagnostics/UEGT2CaptureSubsystem.h"
#include "Engine/Console.h"
#include "Engine/GameViewportClient.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMeshSocket.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GenericPlatform/GenericPlatformInputDeviceMapper.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "InputKeyEventArgs.h"
#include "Misc/CommandLine.h"
#include "Misc/Guid.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Player/UEGT2Character.h"
#include "Player/UEGT2InputConfig.h"
#include "Player/UEGT2NeedsComponent.h"
#include "Player/UEGT2PlayerController.h"
#include "Progress/UEGT2ProgressSubsystem.h"
#include "UEGT2LogChannels.h"
#include "World/UEGT2SkyController.h"

namespace UEGT2CrossingSmoke
{
	const FName BridgeTag(TEXT("UEGT2.Crossing.LowerRiver"));
	const FName SocketNames[] = { TEXT("ApproachA"), TEXT("DeckA"), TEXT("DeckB"), TEXT("ApproachB") };
	constexpr float FloorGap = 2.15f; // CharacterMovement's normal 1.9–2.4 cm floor gap.
	constexpr float SeamMargin = 54.0f; // Full radius plus 20 cm at mesh/terrain joins.
	bool Finite(const FVector& Value) { return FMath::IsFinite(Value.X) && FMath::IsFinite(Value.Y) && FMath::IsFinite(Value.Z); }
}

bool UUEGT2CrossingSmokeSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	return FParse::Param(FCommandLine::Get(), TEXT("UEGT2CrossingSmoke")) && Super::ShouldCreateSubsystem(Outer);
}
bool UUEGT2CrossingSmokeSubsystem::DoesSupportWorldType(EWorldType::Type Type) const { return Type == EWorldType::Game; }
TStatId UUEGT2CrossingSmokeSubsystem::GetStatId() const { RETURN_QUICK_DECLARE_CYCLE_STAT(UUEGT2CrossingSmokeSubsystem, STATGROUP_Tickables); }

void UUEGT2CrossingSmokeSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	bRequested = true; StartedAt = FPlatformTime::Seconds();
	FString UserDirectory;
	FParse::Value(FCommandLine::Get(), TEXT("UserDir="), UserDirectory);
	FPaths::NormalizeDirectoryName(UserDirectory);
	RunId = FPaths::GetCleanFilename(UserDirectory);
	FGuid Guid;
	FString Expected = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("Saved/CrossingSmoke"), RunId));
	FPaths::NormalizeDirectoryName(Expected);
	if (!Check(FGuid::ParseExact(RunId, EGuidFormats::Digits, Guid) && !FPaths::IsRelative(UserDirectory)
		&& UserDirectory.Equals(Expected, ESearchCase::IgnoreCase), TEXT("expected isolated packaged Saved/CrossingSmoke/<guid> UserDir"))) { return; }
	if (!Check(!UUEGT2CaptureSubsystem::IsCaptureRequested() && !UUEGT2CaptureSubsystem::IsLifeCaptureRequested()
		&& !UUEGT2CaptureSubsystem::IsWalkSmokeRequested() && !UUEGT2CaptureSubsystem::IsFlySoakRequested(),
		TEXT("crossing smoke cannot share capture, walk or fly diagnostics"))) { return; }
	for (const TCHAR* Other : { TEXT("UEGT2AutoWalkSmoke"), TEXT("UEGT2ServicesSmoke"), TEXT("UEGT2SurveySmoke"),
		TEXT("UEGT2RestSmoke"), TEXT("UEGT2HudSizeSmoke"), TEXT("UEGT2ProgressSmoke"), TEXT("UEGT2AutosaveSmoke"), TEXT("UEGT2ContractSmoke") })
	{
		// Reject value-bearing and bare forms, including malformed diagnostic values.
		if (!Check(!FParse::Param(FCommandLine::Get(), Other) && !FCString::Strifind(FCommandLine::Get(), *(FString(TEXT("-")) + Other + TEXT("="))),
			TEXT("crossing smoke cannot share another diagnostic"))) { return; }
	}
	PostTickHandle = FWorldDelegates::OnWorldPostActorTick.AddUObject(this, &UUEGT2CrossingSmokeSubsystem::ObserveWorld);
	UE_LOG(LogUEGT2Diag, Log, TEXT("Crossing smoke starting: run=%s normal capsule, one setup teleport, two walking legs."), *RunId);
}

void UUEGT2CrossingSmokeSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	const double Now = FPlatformTime::Seconds();
	if (!Check(Now - StartedAt <= 180.0, TEXT("crossing smoke exceeded 180 seconds"))) { return; }
	if (Stage == EStage::Startup && Now - StartedAt >= 8.0)
	{
		AUEGT2PlayerController* PC = Cast<AUEGT2PlayerController>(GetWorld()->GetFirstPlayerController());
		AUEGT2Character* Pawn = PC ? Cast<AUEGT2Character>(PC->GetPawn()) : nullptr;
		if (PC && Pawn && Pawn->HasActorBegunPlay() && PC->GetInputConfig()) { Start(); }
		else { Check(Now - StartedAt <= 30.0, TEXT("player/input startup exceeded 30 seconds")); }
	}
}

bool UUEGT2CrossingSmokeSubsystem::Check(bool bCondition, const TCHAR* Reason)
{
	if (!bCondition) { Finish(false, Reason); }
	return bCondition;
}

bool UUEGT2CrossingSmokeSubsystem::IsTerrain(const AActor* Actor) const
{
	return Actor && LandscapeClass.IsValid() && Actor->IsA(LandscapeClass.Get());
}

bool UUEGT2CrossingSmokeSubsystem::ResolveBridge()
{
	using namespace UEGT2CrossingSmoke;
	int32 Count = 0;
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		if (It->ActorHasTag(BridgeTag)) { ++Count; Bridge = Cast<AStaticMeshActor>(*It); }
	}
	if (!Check(Count == 1 && Bridge.IsValid(), TEXT("expected exactly one tagged static mesh bridge"))) { return false; }
	const UStaticMeshComponent* Component = Bridge->GetStaticMeshComponent();
	const UStaticMesh* Mesh = Component ? Component->GetStaticMesh() : nullptr;
	if (!Check(Mesh && Component->Mobility == EComponentMobility::Static
		&& Component->GetCollisionObjectType() == ECC_WorldStatic && Component->GetCollisionResponseToChannel(ECC_Pawn) == ECR_Block
		&& Component->GetCollisionEnabled() != ECollisionEnabled::NoCollision
		&& Bridge->GetActorScale3D().Equals(FVector::OneVector), TEXT("bridge must have ordinary static Pawn-blocking collision and scale"))) { return false; }
	Points.SetNum(6);
	for (int32 Index = 0; Index < 4; ++Index)
	{
		int32 Matches = 0;
		for (const UStaticMeshSocket* Socket : Mesh->Sockets) { Matches += Socket && Socket->SocketName == SocketNames[Index] ? 1 : 0; }
		if (!Check(Matches == 1 && Component->DoesSocketExist(SocketNames[Index]), TEXT("bridge ground sockets missing or duplicated"))) { return false; }
		Points[Index + 1] = Component->GetSocketTransform(SocketNames[Index], RTS_World).GetLocation();
		if (!Check(Finite(Points[Index + 1]), TEXT("bridge socket is nonfinite"))) { return false; }
	}
	for (int32 Index = 1; Index < 4; ++Index)
	{
		const double Length = FVector::Dist2D(Points[Index], Points[Index + 1]);
		if (!Check(Length > 200.0 && Length < 20000.0, TEXT("bridge segment length outside bounded test contract"))) { return false; }
	}
	if (!Check(FMath::Abs(Points[2].Z - Points[3].Z) < 1.0, TEXT("bridge deck sockets must share a level surface"))) { return false; }
	// Resolve the native class by its cooked path; this diagnostic does not need a
	// Landscape module dependency just to distinguish terrain from an arbitrary prop.
	LandscapeClass = FindObject<UClass>(nullptr, TEXT("/Script/Landscape.LandscapeProxy"));
	if (!Check(LandscapeClass.IsValid(), TEXT("landscape class unavailable"))) { return false; }
	FVector UnusedStand;
	return FindDryEndpoint(1, (Points[1] - Points[2]).GetSafeNormal2D(), Points[0], StartStand)
		&& FindDryEndpoint(4, (Points[4] - Points[3]).GetSafeNormal2D(), Points[5], UnusedStand);
}

bool UUEGT2CrossingSmokeSubsystem::FindDryEndpoint(int32 SocketIndex, const FVector& Outward, FVector& Ground, FVector& Stand)
{
	const UCapsuleComponent* Capsule = Player->GetCapsuleComponent();
	const float Half = Capsule->GetScaledCapsuleHalfHeight();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(CrossingEndpoint), false, Player.Get());
	const FCollisionShape Shape = FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius(), Half);
	// Include ordinary braking room beyond each endpoint. No searching for an
	// easier route or raising the pawn to rescue a failed crossing.
	for (int32 Offset : { 100, 200, 300 })
	{
		const FVector Near = Points[SocketIndex] + Outward * Offset;
		FHitResult Hit;
		if (!Check(GetWorld()->LineTraceSingleByObjectType(Hit, Near + FVector(0, 0, 120), Near - FVector(0, 0, 120),
			FCollisionObjectQueryParams(ECC_WorldStatic), Params) && IsTerrain(Hit.GetActor()), TEXT("dry approach must have nearby Landscape support"))) { return false; }
		const FVector Centre = Hit.ImpactPoint + FVector(0, 0, Half);
		FHitResult Floor;
		if (!Check(GetWorld()->SweepSingleByProfile(Floor, Centre + FVector(0, 0, 60), Centre - FVector(0, 0, 60),
			FQuat::Identity, Capsule->GetCollisionProfileName(), Shape, Params) && !Floor.bStartPenetrating && IsTerrain(Floor.GetActor())
			&& Floor.ImpactNormal.Z >= Player->GetCharacterMovement()->GetWalkableFloorZ(), TEXT("real capsule cannot stand on dry approach"))) { return false; }
		const FVector Position = Floor.Location + FVector(0, 0, UEGT2CrossingSmoke::FloorGap);
		if (!Check(!GetWorld()->OverlapBlockingTestByProfile(Position, FQuat::Identity, Capsule->GetCollisionProfileName(), Shape, Params),
			TEXT("standing capsule overlaps an approach obstacle"))) { return false; }
		if (Offset == 100) { Ground = Hit.ImpactPoint; Stand = Position; }
	}
	return true;
}

bool UUEGT2CrossingSmokeSubsystem::CheckNormal(bool bRequireWalking)
{
	if (!Check(Player.IsValid() && Controller.IsValid() && Sky.IsValid() && Bridge.IsValid()
		&& Controller->GetPawn() == Player.Get() && Player->GetWorld() == GetWorld(), TEXT("crossing context changed"))) { return false; }
	const AUEGT2Character* Pawn = Player.Get();
	const AUEGT2Character* Defaults = GetDefault<AUEGT2Character>();
	const UCapsuleComponent* Capsule = Pawn->GetCapsuleComponent();
	const UCharacterMovementComponent* Movement = Pawn->GetCharacterMovement();
	const UUEGT2NeedsComponent* Life = Pawn->GetLife();
	const UGameViewportClient* Viewport = GetWorld()->GetGameViewport();
	const UConsole* Console = Viewport ? Viewport->ViewportConsole.Get() : nullptr;
	if (!Check(Life && UUEGT2NeedsComponent::IsValidProgress(Life->GetNeeds(), Life->GetPurse(), Life->GetTrade())
		&& UEGT2CrossingSmoke::Finite(Pawn->GetActorLocation()) && UEGT2CrossingSmoke::Finite(Pawn->GetVelocity())
		&& !Life->IsOccupied() && !Pawn->IsGodMode() && !Pawn->IsFlyEnabled() && !Pawn->IsNoclipEnabled()
		&& !Pawn->IsAutoWalking() && !Pawn->IsSprinting() && !Pawn->bIsCrouched && Pawn->GetActorEnableCollision()
		&& FMath::IsNearlyEqual(Pawn->GetSpeedMultiplier(), 1.0f)
		&& FMath::IsNearlyEqual(Pawn->WalkSpeed, Defaults->WalkSpeed)
		&& FMath::IsNearlyEqual(Capsule->GetScaledCapsuleRadius(), 34.0f)
		&& FMath::IsNearlyEqual(Capsule->GetScaledCapsuleHalfHeight(), 90.0f)
		&& FMath::IsNearlyEqual(Capsule->GetUnscaledCapsuleRadius(), 34.0f)
		&& FMath::IsNearlyEqual(Capsule->GetUnscaledCapsuleHalfHeight(), 90.0f)
		&& Capsule->GetComponentScale().Equals(FVector::OneVector)
		&& Capsule->GetCollisionEnabled() == Defaults->GetCapsuleComponent()->GetCollisionEnabled()
		&& Capsule->GetCollisionProfileName() == Defaults->GetCapsuleComponent()->GetCollisionProfileName()
		&& FMath::IsNearlyEqual(Movement->MaxStepHeight, 45.0f)
		&& FMath::IsNearlyEqual(Movement->GetWalkableFloorAngle(), 50.0f)
		&& FMath::IsNearlyEqual(Movement->MaxWalkSpeed, Pawn->WalkSpeed * Life->GetExertionScale(), 0.1f)
		&& Controller->IsLocalController() && !Controller->IsMoveInputIgnored()
		&& Controller->GetMenuState() == EUEGT2MenuState::None && !Controller->IsDialogueOpen()
		&& !GetWorld()->IsPaused() && !(Console && Console->ConsoleActive()), TEXT("normal movement, life or input invariant changed"))) { return false; }
	return !bRequireWalking || Check(Movement->MovementMode == MOVE_Walking && Movement->CurrentFloor.IsWalkableFloor(), TEXT("ordinary walking lost its walkable floor"));
}

void UUEGT2CrossingSmokeSubsystem::Start()
{
	Controller = Cast<AUEGT2PlayerController>(GetWorld()->GetFirstPlayerController());
	Player = Controller.IsValid() ? Cast<AUEGT2Character>(Controller->GetPawn()) : nullptr;
	Sky = AUEGT2SkyController::Get(GetWorld());
	if (!Check(Player.IsValid() && Sky.IsValid(), TEXT("player or sky unavailable"))) { return; }
	Controller->CloseMenu();
	ForwardKey = UUEGT2InputConfig::GetEffectiveKey(EUEGT2InputSlot::MoveForward);
	UUEGT2ProgressSubsystem* Progress = UUEGT2ProgressSubsystem::Get(GetWorld());
	if (!Check(ForwardKey.IsValid() && Controller->GetInputConfig()->MoveAction && Progress && !Progress->IsEnabled(),
		TEXT("ordinary forward binding or persistence diagnostic exclusion unavailable"))) { return; }
	bOriginalClock = Sky->IsDayNightCycleEnabled(); bClockChanged = true; Sky->SetDayNightCycleEnabled(false);
	if (!ResolveBridge() || !CheckNormal(false)) { return; }
	const FRotator Facing(0, (Points[1] - Points[0]).Rotation().Yaw, 0);
	// The only location mutation in the entire traversal: a previously validated
	// standing setup point. A teleport adjustment must not disguise bad metadata.
	if (!Check(Player->TeleportTo(StartStand, Facing, false, false) && Player->GetActorLocation().Equals(StartStand, 1.0), TEXT("setup teleport failed or adjusted position"))) { return; }
	Player->GetCharacterMovement()->StopMovementImmediately();
	Player->GetCharacterMovement()->SetMovementMode(MOVE_Falling);
	Controller->SetControlRotation(Facing);
	Stage = EStage::Settle; StageStartedAt = FPlatformTime::Seconds();
	LogState(TEXT("setup"));
}

void UUEGT2CrossingSmokeSubsystem::Key(EInputEvent Event)
{
	if (!Controller.IsValid() || !ForwardKey.IsValid()) { return; }
	const FInputDeviceId Device = IPlatformInputDeviceMapper::Get().GetPrimaryInputDeviceForUser(Controller->GetPlatformUserId());
	Controller->InputKey(FInputKeyEventArgs(nullptr, Device, ForwardKey, Event, FPlatformTime::Cycles64()));
	bKeyHeld = Event == IE_Pressed;
}

void UUEGT2CrossingSmokeSubsystem::BeginLeg(bool bReturn)
{
	Stage = bReturn ? EStage::Inbound : EStage::Outbound;
	Segment = 0; Frames = 0; SegmentBase = 0; BestProgress = 0; Distance = 0; WorstCross = 0;
	LastAlong = 0; LastCross = 0;
	for (int32& Count : SupportFrames) { Count = 0; }
	LastPosition = Player->GetActorLocation();
	LegStartedAt = LastProgressAt = LastLogAt = FPlatformTime::Seconds();
	Controller->SetControlRotation(FRotator(0, (Points[bReturn ? 4 : 1] - LastPosition).Rotation().Yaw, 0));
	Key(IE_Pressed);
	LogState(TEXT("leg started"));
}

void UUEGT2CrossingSmokeSubsystem::ObserveWorld(UWorld* World, ELevelTick TickType, float DeltaTime)
{
	if (World != GetWorld() || bFinished || Stage == EStage::Startup) { return; }
	if (!CheckNormal(Stage != EStage::Settle)) { return; }
	const double Now = FPlatformTime::Seconds();
	if (Stage == EStage::Outbound || Stage == EStage::Inbound) { ObserveLeg(); return; }
	if (!Check(Now - StageStartedAt <= 3.0, TEXT("ground settling or ordinary braking exceeded three seconds"))) { return; }
	if (!Player->GetCharacterMovement()->IsMovingOnGround() || Player->GetHorizontalSpeed() >= 5.0f) { return; }
	if (!Check(IsTerrain(Player->GetCharacterMovement()->CurrentFloor.HitResult.GetActor()), TEXT("leg endpoint must be supported by dry Landscape"))) { return; }
	if (Stage == EStage::Settle) { BeginLeg(false); }
	else if (Stage == EStage::Turnaround)
	{
		if (!Check(FVector::Dist2D(Player->GetActorLocation(), Points[5]) <= 200.0, TEXT("outbound braking escaped checked approach"))) { return; }
		BeginLeg(true);
	}
	else
	{
		if (!Check(FVector::Dist2D(Player->GetActorLocation(), Points[0]) <= 200.0, TEXT("return braking escaped checked approach"))) { return; }
		Finish(CompletedLegs == 2, TEXT("both directions completed with normal capsule and bridge floor support"));
	}
}

void UUEGT2CrossingSmokeSubsystem::ObserveLeg()
{
	using namespace UEGT2CrossingSmoke;
	const bool bReturn = Stage == EStage::Inbound;
	const int32 Index = bReturn ? 5 - Segment : Segment;
	const FVector From = Points[Index], To = Points[Index + (bReturn ? -1 : 1)];
	const FVector Direction = (To - From).GetSafeNormal2D();
	const double Length = FVector::Dist2D(From, To);
	const FVector Position = Player->GetActorLocation();
	const double Along = FVector::DotProduct(Position - From, Direction);
	const double Cross = FMath::Abs(FVector::DotProduct(Position - From, FVector(-Direction.Y, Direction.X, 0)));
	LastAlong = Along; LastCross = Cross;
	const double Now = FPlatformTime::Seconds();
	WorstCross = FMath::Max(WorstCross, Cross); ++Frames;
	Distance += FVector::Dist2D(Position, LastPosition); LastPosition = Position;
	if (!Check(Cross <= 100.0 && Along >= -200.0 && Along <= Length + 200.0, TEXT("walk escaped the authored center corridor"))
		|| !Check(Now - LegStartedAt <= 60.0, TEXT("walking leg exceeded 60 seconds"))) { return; }
	const UCharacterMovementComponent* Movement = Player->GetCharacterMovement();
	if (Segment >= 1 && Segment <= 3 && Along > SeamMargin && Along < Length - SeamMargin)
	{
		if (!Check(Movement->CurrentFloor.HitResult.GetActor() == Bridge.Get(), TEXT("ramp/deck supported by something other than the bridge"))) { return; }
		++SupportFrames[Segment - 1];
		if (Segment == 2 && !Check(Position.Z - Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() >= Points[2].Z - 5.0,
			TEXT("capsule feet below the deck surface"))) { return; }
	}
	else if ((Segment == 0 && Along < Length - SeamMargin) || (Segment == 4 && Along > SeamMargin))
	{
		if (!Check(IsTerrain(Movement->CurrentFloor.HitResult.GetActor()), TEXT("dry approach is not supported by Landscape"))) { return; }
	}
	const double Progress = SegmentBase + FMath::Clamp(Along, 0.0, Length);
	if (Progress >= BestProgress + 25.0) { BestProgress = Progress; LastProgressAt = Now; }
	if (!Check(Now - LastProgressAt <= 2.0, TEXT("walking stalled: less than 25 cm progress in two seconds"))) { return; }
	if (Now - LastLogAt >= 1.0) { LogState(TEXT("walking")); LastLogAt = Now; }
	if (Along >= Length - 10.0)
	{
		SegmentBase += Length; ++Segment;
		if (Segment == 5)
		{
			Key(IE_Released);
			if (!Check(SupportFrames[0] > 0 && SupportFrames[1] > 0 && SupportFrames[2] > 0, TEXT("leg did not observe both ramps and deck"))) { return; }
			++CompletedLegs;
			UE_LOG(LogUEGT2Diag, Log, TEXT("Crossing leg complete: direction=%s seconds=%.3f distance=%.1f frames=%d worstCross=%.2f bridgeSamples=%d/%d/%d"),
				bReturn ? TEXT("BtoA") : TEXT("AtoB"), Now - LegStartedAt, Distance, Frames, WorstCross, SupportFrames[0], SupportFrames[1], SupportFrames[2]);
			Stage = bReturn ? EStage::Brake : EStage::Turnaround; StageStartedAt = Now; LogState(TEXT("braking"));
			return;
		}
		LogState(TEXT("station crossed"));
	}
	// Facing follows the authored polyline; locomotion still comes exclusively
	// from the held, mapped forward key and ordinary CharacterMovement collision.
	const int32 TargetIndex = bReturn ? 4 - Segment : Segment + 1;
	Controller->SetControlRotation(FRotator(0, (Points[TargetIndex] - Position).Rotation().Yaw, 0));
}

void UUEGT2CrossingSmokeSubsystem::LogState(const TCHAR* Reason) const
{
	if (!Player.IsValid()) { return; }
	const UCharacterMovementComponent* Movement = Player->GetCharacterMovement();
	const UUEGT2NeedsComponent* Life = Player->GetLife();
	const FUEGT2NPCNeeds Needs = Life ? Life->GetNeeds() : FUEGT2NPCNeeds();
	const FHitResult& Floor = Movement->CurrentFloor.HitResult;
	UE_LOG(LogUEGT2Diag, Log, TEXT("Crossing state: %s stage=%d segment=%d elapsed=%.2f along=%.2f cross=%.2f location=%s velocity=%s mode=%d floor=%s component=%s floorZ=%.2f normal=%s walkSpeed=%.2f capsule=%.1f/%.1f needs=%.6f/%.6f/%.6f/%.6f coins=%.6f"),
		Reason, static_cast<int32>(Stage), Segment, FPlatformTime::Seconds() - StartedAt, LastAlong, LastCross, *Player->GetActorLocation().ToCompactString(),
		*Player->GetVelocity().ToCompactString(), static_cast<int32>(Movement->MovementMode), *GetNameSafe(Floor.GetActor()), *GetNameSafe(Floor.GetComponent()),
		Floor.ImpactPoint.Z, *Floor.ImpactNormal.ToCompactString(), Movement->MaxWalkSpeed,
		Player->GetCapsuleComponent()->GetScaledCapsuleRadius(), Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight(),
		Needs.Energy, Needs.Fed, Needs.Relief, Needs.Company, Life ? Life->GetPurse().Coins : 0.0f);
}

void UUEGT2CrossingSmokeSubsystem::Cleanup()
{
	if (bKeyHeld) { Key(IE_Released); }
	FWorldDelegates::OnWorldPostActorTick.Remove(PostTickHandle); PostTickHandle.Reset();
	if (bClockChanged && Sky.IsValid()) { Sky->SetDayNightCycleEnabled(bOriginalClock); }
	bClockChanged = false;
}

void UUEGT2CrossingSmokeSubsystem::Finish(bool bSuccess, const TCHAR* Reason)
{
	if (bFinished) { return; }
	bFinished = true; LogState(Reason); Cleanup();
	if (bSuccess) { UE_LOG(LogUEGT2Diag, Log, TEXT("UEGT2_CROSSING_SMOKE_COMPLETE run=%s legs=%d %s"), *RunId, CompletedLegs, Reason); }
	else { UE_LOG(LogUEGT2Diag, Error, TEXT("UEGT2_CROSSING_SMOKE_FAILED run=%s legs=%d %s"), *RunId, CompletedLegs, Reason); }
	FPlatformMisc::RequestExitWithStatus(false, bSuccess ? 0 : 1);
}

void UUEGT2CrossingSmokeSubsystem::Deinitialize() { Cleanup(); Super::Deinitialize(); }
