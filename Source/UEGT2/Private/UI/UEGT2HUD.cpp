#include "UI/UEGT2HUD.h"

#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "GameFramework/PlayerController.h"
#include "Dev/UEGT2DevModeSubsystem.h"
#include "Interaction/UEGT2InteractionComponent.h"
#include "NPC/UEGT2NPCDirector.h"
#include "Player/UEGT2Character.h"
#include "Player/UEGT2InputConfig.h"
#include "Player/UEGT2NeedsComponent.h"
#include "Player/UEGT2PlayerController.h"
#include "Settings/UEGT2GameUserSettings.h"
#include "Survey/UEGT2SurveySubsystem.h"
#include "World/UEGT2Almanac.h"
#include "World/UEGT2Weather.h"
#include "UEGT2LogChannels.h"

// Named rather than anonymous, and referenced qualified below. SUEGT2Menu.cpp
// ends its style block with a file-scope `using namespace UEGT2Menu;`, and a
// unity build concatenates that file ahead of this one. In an anonymous
// namespace these four would sit at global scope, where Ink/Muted/Accent are
// then ambiguous against the identically named UEGT2Menu ones (C2872).
namespace UEGT2Hud
{
	const FLinearColor Ink(0.95f, 0.96f, 0.98f, 1.0f);
	const FLinearColor Muted(0.72f, 0.75f, 0.79f, 1.0f);
	const FLinearColor Accent(0.45f, 0.82f, 0.84f, 1.0f);
	const FLinearColor Shade(0.0f, 0.0f, 0.0f, 0.55f);

	/** Bubbles stop being readable well before they stop being drawn. */
	const float BubbleFadeStart = 3000.0f;
	const float BubbleFadeEnd = 4600.0f;
	const float BubbleMaxWidth = 300.0f;
}

/** Prepared once per draw, shared by placement and rendering. Never retained. */
struct FUEGT2HUDLife
{
	FUEGT2NPCNeeds Needs;
	TArray<FString> Purse, Activity;
	float PurseH = 0.0f, ActivityH = 0.0f, RowH = 0.0f, BodyW = 0.0f;
	FBox2D Bounds = FBox2D(ForceInit);
};

struct FUEGT2HUDSurvey
{
	TArray<FString> Name, Detail, Hint;
	float NameH = 0.0f, DetailH = 0.0f, HintH = 0.0f;
	float Bearing = 0.0f;
	bool bNearby = false;
	FBox2D Bounds = FBox2D(ForceInit);
};

AUEGT2HUD::AUEGT2HUD()
{
	PrimaryActorTick.bCanEverTick = false;
}

void AUEGT2HUD::ShowMessage(const FText& Message, float Duration)
{
	CurrentMessage = Message;
	MessageExpiry = GetWorld() ? GetWorld()->GetTimeSeconds() + Duration : 0.0f;
	UE_LOG(LogUEGT2UI, Log, TEXT("HUD message: %s"), *Message.ToString());
}

void AUEGT2HUD::DrawHUD()
{
	Super::DrawHUD();

	if (!Canvas)
	{
		return;
	}

	AUEGT2PlayerController* PC = Cast<AUEGT2PlayerController>(PlayerOwner);
	// The menu owns the screen when it is open.
	if (PC && PC->IsMenuOpen())
	{
		return;
	}

	AUEGT2Character* Explorer = PC ? Cast<AUEGT2Character>(PC->GetPawn()) : nullptr;
	const UUEGT2GameUserSettings* Settings = UUEGT2GameUserSettings::Get();
	HudLayout = UEGT2HUDLayout::Resolve(FVector2D(Canvas->ClipX, Canvas->ClipY),
		Settings ? Settings->GetHudScale() : 1.0f, bHudScalingEnabled);
	if (LastLoggedScale != HudLayout.Scale || bLastLoggedGate != bHudScalingEnabled)
	{
		UE_LOG(LogUEGT2UI, Log, TEXT("HUD scale %.2fx; maintainer gate %s."), HudLayout.Scale,
			bHudScalingEnabled ? TEXT("on") : TEXT("off"));
		LastLoggedScale = HudLayout.Scale;
		bLastLoggedGate = bHudScalingEnabled;
	}
	const FUEGT2HUDSurvey Survey = PrepareSurvey(PC);
	const float SurveyWidth = Survey.Bounds.bIsValid ? Survey.Bounds.GetSize().X / HudLayout.Scale : 0.0f;
	const FUEGT2HUDLife Life = (!Settings || Settings->GetShowNeeds())
		? PrepareLife(Explorer, UEGT2HUDLayout::BottomLeftMaxWidth(HudLayout, SurveyWidth, Survey.Bounds.bIsValid))
		: FUEGT2HUDLife();
	TArray<FBox2D> PlayerPanels;
	if (Life.Bounds.bIsValid) { PlayerPanels.Add(Life.Bounds); }
	if (Survey.Bounds.bIsValid) { PlayerPanels.Add(Survey.Bounds); }

	const float CentreX = Canvas->ClipX * 0.5f;
	const float CentreY = Canvas->ClipY * 0.5f;

	const bool bHasFocus = Explorer && Explorer->GetInteraction()
		&& Explorer->GetInteraction()->GetFocusedActor() != nullptr;

	if (!Settings || Settings->GetShowCrosshair())
	{
		DrawCrosshair(CentreX, CentreY, bHasFocus);
	}
	FBox2D Prompt(ForceInit);
	if (!Settings || Settings->GetShowInteractPrompts())
	{
		Prompt = DrawPrompt(CentreX / HudLayout.Scale, CentreY / HudLayout.Scale);
	}
	const FBox2D Message = DrawMessage(PlayerPanels);
	if (Prompt.bIsValid) { PlayerPanels.Add(Prompt); }
	if (Message.bIsValid) { PlayerPanels.Add(Message); }
	if (!Settings || Settings->GetShowSpeechBubbles())
	{
		DrawSpeechBubbles(PlayerPanels);
	}

	if (PC && PC->IsDiagnosticsVisible())
	{
		DrawDiagnostics(Explorer);
	}

	if (!Settings || Settings->GetShowAlmanac())
	{
		DrawAlmanac(Canvas->ClipX / HudLayout.Scale);
	}
	if (!Settings || Settings->GetShowNeeds())
	{
		DrawLife(Life);
	}
	DrawDevStatus(Canvas->ClipX);
	DrawSurveyTracking(Survey);
}

FUEGT2HUDSurvey AUEGT2HUD::PrepareSurvey(AUEGT2PlayerController* PC)
{
	FUEGT2HUDSurvey Result;
	const UUEGT2SurveySubsystem* Survey = UUEGT2SurveySubsystem::Get(GetWorld());
	if (!PC || PC->IsDialogueOpen() || !Survey) { return Result; }
	FVector ViewLocation;
	FRotator ViewRotation;
	PC->GetPlayerViewPoint(ViewLocation, ViewRotation);
	FUEGT2SurveyDirection Direction;
	if (!Survey->GetTrackedDirection(ViewLocation, ViewRotation.Yaw, Direction)) { return Result; }

	FNumberFormattingOptions Format;
	Format.SetMaximumFractionalDigits(Direction.DistanceMetres >= 1000.0f ? 1 : 0);
	const FText Distance = FText::Format(Direction.DistanceMetres >= 1000.0f
		? NSLOCTEXT("UEGT2SurveyHUD", "Kilometres", "{0} km")
		: NSLOCTEXT("UEGT2SurveyHUD", "Metres", "{0} m"),
		FText::AsNumber(Direction.DistanceMetres >= 1000.0f ? Direction.DistanceMetres / 1000.0f : Direction.DistanceMetres, &Format));
	const FText Detail = FText::Format(NSLOCTEXT("UEGT2SurveyHUD", "Direction", "{0} · {1} · straight line"),
		Distance, Direction.CompassDirection);
	const FKey Key = UUEGT2InputConfig::GetEffectiveKey(EUEGT2InputSlot::Journal);
	const FText Hint = FText::Format(NSLOCTEXT("UEGT2SurveyHUD", "JournalKey", "[{0}] Survey Journal"), Key.GetDisplayName());
	const float Width = 370.0f;
	float Measured = 0.0f;
	LayoutText(Direction.Name.ToString(), GEngine->GetMediumFont(), Width - 70.0f, Result.Name, Measured, Result.NameH, 2);
	LayoutText(Detail.ToString(), GEngine->GetSmallFont(), Width - 70.0f, Result.Detail, Measured, Result.DetailH, 2);
	LayoutText(Hint.ToString(), GEngine->GetSmallFont(), Width - 70.0f, Result.Hint, Measured, Result.HintH, 2);
	const float Height = HudLayout.bEnhanced
		? 22.0f + Result.NameH * Result.Name.Num() + Result.DetailH * Result.Detail.Num() + Result.HintH * Result.Hint.Num() + 8.0f
		: 82.0f;
	Result.Bounds = UEGT2HUDLayout::AnchorPanel(HudLayout, FVector2D(Width, Height), EUEGT2HUDAnchor::BottomRight, FVector2D(24.0, 24.0));
	Result.Bearing = Direction.RelativeBearingDegrees;
	Result.bNearby = Direction.bNearby;
	return Result;
}

void AUEGT2HUD::DrawSurveyTracking(const FUEGT2HUDSurvey& Survey)
{
	if (!Survey.Bounds.bIsValid) { return; }
	const FVector2D Origin = HudLayout.ToLogical(Survey.Bounds.Min);
	const FVector2D Size = HudLayout.ToLogical(Survey.Bounds.GetSize());
	const float X = Origin.X, Y = Origin.Y;
	DrawHudRect(UEGT2Hud::Shade, X, Y, Size.X, Size.Y);
	float Cursor = Y + 11.0f;
	for (const FString& Line : Survey.Name) { DrawHudText(Line, UEGT2Hud::Ink, X + 58.0f, Cursor, GEngine->GetMediumFont()); Cursor += Survey.NameH; }
	Cursor = HudLayout.bEnhanced ? Cursor + 4.0f : Y + 35.0f;
	for (const FString& Line : Survey.Detail) { DrawHudText(Line, UEGT2Hud::Accent, X + 58.0f, Cursor, GEngine->GetSmallFont()); Cursor += Survey.DetailH; }
	Cursor = HudLayout.bEnhanced ? Cursor + 4.0f : Y + 57.0f;
	for (const FString& Line : Survey.Hint) { DrawHudText(Line, UEGT2Hud::Muted, X + 58.0f, Cursor, GEngine->GetSmallFont()); Cursor += Survey.HintH; }

	const FVector2D Centre(X + 28.0f, Y + 38.0f);
	if (Survey.bNearby)
	{
		DrawHudRect(UEGT2Hud::Accent, Centre.X - 4.0f, Centre.Y - 4.0f, 8.0f, 8.0f);
		return;
	}
	// Up means ahead, right means turn right; the compass text is absolute north.
	const float Radians = FMath::DegreesToRadians(Survey.Bearing);
	const FVector2D Forward(FMath::Sin(Radians), -FMath::Cos(Radians));
	const FVector2D Side(-Forward.Y, Forward.X);
	const FVector2D Tip = Centre + Forward * 16.0f;
	const FVector2D Tail = Centre - Forward * 12.0f;
	const FVector2D WingA = Tip - Forward * 9.0f + Side * 7.0f;
	const FVector2D WingB = Tip - Forward * 9.0f - Side * 7.0f;
	DrawHudLine(Tail.X, Tail.Y, Tip.X, Tip.Y, UEGT2Hud::Accent, 2.0f);
	DrawHudLine(WingA.X, WingA.Y, Tip.X, Tip.Y, UEGT2Hud::Accent, 2.0f);
	DrawHudLine(WingB.X, WingB.Y, Tip.X, Tip.Y, UEGT2Hud::Accent, 2.0f);
}

void AUEGT2HUD::DrawCrosshair(float CentreX, float CentreY, bool bHasFocus)
{
	const float Gap = bHasFocus ? 7.0f : 4.0f;
	const float Length = bHasFocus ? 7.0f : 4.0f;
	const float Thickness = 1.6f;
	const FLinearColor Colour = bHasFocus ? UEGT2Hud::Accent : FLinearColor(1, 1, 1, 0.55f);

	DrawLine(CentreX - Gap - Length, CentreY, CentreX - Gap, CentreY, Colour, Thickness);
	DrawLine(CentreX + Gap, CentreY, CentreX + Gap + Length, CentreY, Colour, Thickness);
	DrawLine(CentreX, CentreY - Gap - Length, CentreX, CentreY - Gap, Colour, Thickness);
	DrawLine(CentreX, CentreY + Gap, CentreX, CentreY + Gap + Length, Colour, Thickness);
}

FBox2D AUEGT2HUD::DrawPrompt(float CentreX, float CentreY)
{
	AUEGT2PlayerController* PC = Cast<AUEGT2PlayerController>(PlayerOwner);
	AUEGT2Character* Explorer = PC ? Cast<AUEGT2Character>(PC->GetPawn()) : nullptr;
	if (!Explorer || !Explorer->GetInteraction())
	{
		return FBox2D(ForceInit);
	}
	const FText Prompt = Explorer->GetInteraction()->GetFocusedPrompt();
	if (Prompt.IsEmpty())
	{
		return FBox2D(ForceInit);
	}

	const FKey Key = UUEGT2InputConfig::GetEffectiveKey(EUEGT2InputSlot::Interact);
	const FString Text = FString::Printf(TEXT("[%s]  %s"),
		Key.IsValid() ? *Key.GetDisplayName().ToString() : TEXT("?"),
		*Prompt.ToString());

	UFont* Font = GEngine->GetMediumFont();
	float Width = 0.0f, Height = 0.0f;
	TArray<FString> Lines;
	LayoutText(Text, Font, (Canvas->ClipX - 32.0f) / HudLayout.Scale - 24.0f, Lines, Width, Height, 3);

	const float X = CentreX - Width * 0.5f;
	const float Y = CentreY + 62.0f;

	DrawHudRect(UEGT2Hud::Shade, X - 12.0f, Y - 6.0f, Width + 24.0f, Height * Lines.Num() + 12.0f);
	for (int32 Row = 0; Row < Lines.Num(); ++Row) { DrawHudText(Lines[Row], UEGT2Hud::Ink, X, Y + Row * Height, Font); }
	return FBox2D(HudLayout.ToScreen(FVector2D(X - 12.0f, Y - 6.0f)),
		HudLayout.ToScreen(FVector2D(X + Width + 12.0f, Y + Height * Lines.Num() + 6.0f)));
}

FBox2D AUEGT2HUD::DrawMessage(const TArray<FBox2D>& BottomPanels)
{
	if (CurrentMessage.IsEmpty() || !GetWorld() || GetWorld()->GetTimeSeconds() > MessageExpiry)
	{
		return FBox2D(ForceInit);
	}
	const FString Text = CurrentMessage.ToString();
	UFont* Font = GEngine->GetMediumFont();
	float Width = 0.0f, Height = 0.0f;
	TArray<FString> Lines;
	LayoutText(Text, Font, (Canvas->ClipX - 32.0f) / HudLayout.Scale - 28.0f, Lines, Width, Height, 3);

	const float Remaining = MessageExpiry - GetWorld()->GetTimeSeconds();
	const float Alpha = FMath::Clamp(Remaining, 0.0f, 1.0f);
	const FBox2D Bounds = UEGT2HUDLayout::PlaceMessage(HudLayout, FVector2D(Width + 28.0f, Height * Lines.Num() + 14.0f), BottomPanels);
	const FVector2D Origin = HudLayout.ToLogical(Bounds.Min);
	const float X = Origin.X + 14.0f, Y = Origin.Y + 7.0f;

	DrawHudRect(FLinearColor(UEGT2Hud::Shade.R, UEGT2Hud::Shade.G, UEGT2Hud::Shade.B, UEGT2Hud::Shade.A * Alpha),
		X - 14.0f, Y - 7.0f, Width + 28.0f, Height * Lines.Num() + 14.0f);
	for (int32 Row = 0; Row < Lines.Num(); ++Row)
	{
		DrawHudText(Lines[Row], FLinearColor(UEGT2Hud::Ink.R, UEGT2Hud::Ink.G, UEGT2Hud::Ink.B, Alpha), X, Y + Row * Height, Font);
	}
	return Bounds;
}

void AUEGT2HUD::DrawAlmanac(float ScreenWidth)
{
	const UUEGT2NPCDirector* Director = UUEGT2NPCDirector::Get(GetWorld());
	if (!Director)
	{
		return;
	}

	const float Hour = Director->GetHour();
	const FUEGT2Date Date = UEGT2DateFromDayIndex(Director->GetDayIndex());

	// Temperature is where the player is standing, not where the world starts:
	// walk up the mountain road and it drops, walk south and it climbs.
	FVector Where = FVector::ZeroVector;
	if (const APlayerController* PC = Cast<APlayerController>(PlayerOwner))
	{
		if (const APawn* Pawn = PC->GetPawn())
		{
			Where = Pawn->GetActorLocation();
		}
	}
	const float Celsius = UEGT2TemperatureC(Where.X, Where.Y, Where.Z, Hour,
		Director->GetDayIndex(), Director->GetWeather());

	const UUEGT2GameUserSettings* Settings = UUEGT2GameUserSettings::Get();
	const bool bFahrenheit = Settings && Settings->GetUseFahrenheit();

	const FString Clock = UEGT2FormatClock(Hour).ToString();
	const FString DateLine = UEGT2FormatDate(Date).ToString();
	const FString Conditions = FString::Printf(TEXT("%s  %s"),
		*GetWeatherDisplayName(Director->GetWeather()).ToString(),
		*UEGT2FormatTemperature(Celsius, bFahrenheit).ToString());

	UFont* Big = GEngine->GetMediumFont();
	UFont* Small = GEngine->GetSmallFont();

	float ClockW = 0.0f, ClockH = 0.0f;
	float DateW = 0.0f, DateH = 0.0f;
	float CondW = 0.0f, CondH = 0.0f;
	MeasureHudText(Clock, ClockW, ClockH, Big, 1.0f);
	MeasureHudText(DateLine, DateW, DateH, Small, 1.0f);
	MeasureHudText(Conditions, CondW, CondH, Small, 1.0f);

	const float PadX = 14.0f;
	const float PadY = 10.0f;
	const float Gap = 4.0f;
	const float BoxW = FMath::Max3(ClockW, DateW, CondW) + PadX * 2.0f;
	const float BoxH = ClockH + DateH + CondH + Gap * 2.0f + PadY * 2.0f;
	const float X = 24.0f / HudLayout.Scale;
	const float Y = 20.0f / HudLayout.Scale;

	DrawRoundedRect(UEGT2Hud::Shade, X, Y, BoxW, BoxH, 7.0f);

	float Cursor = Y + PadY;
	DrawHudText(Clock, UEGT2Hud::Ink, X + PadX, Cursor, Big, 1.0f, false);
	Cursor += ClockH + Gap;
	DrawHudText(DateLine, UEGT2Hud::Muted, X + PadX, Cursor, Small, 1.0f, false);
	Cursor += DateH + Gap;
	DrawHudText(Conditions, UEGT2Hud::Accent, X + PadX, Cursor, Small, 1.0f, false);
}

float AUEGT2HUD::DrawNeedBar(const FString& Label, float Value, float X, float Y, float Width)
{
	UFont* Font = GEngine->GetSmallFont();
	float LabelW = 0.0f, LabelH = 0.0f;
	MeasureHudText(Label, LabelW, LabelH, Font, 1.0f);

	// Warm through to alarming, at the same thresholds the needs model uses to
	// decide an NPC should stop what they are doing about it.
	const float Clamped = FMath::Clamp(Value, 0.0f, 1.0f);
	const FLinearColor Fill = Clamped < 0.18f ? FLinearColor(0.86f, 0.32f, 0.28f, 1.0f)
		: Clamped < 0.34f ? FLinearColor(0.90f, 0.68f, 0.30f, 1.0f)
		: UEGT2Hud::Accent;

	const float TrackX = X + 62.0f;
	const float TrackW = FMath::Max(24.0f, Width - 62.0f);
	const float TrackH = 6.0f;
	const float TrackY = Y + FMath::Max(0.0f, (LabelH - TrackH) * 0.5f);

	DrawHudText(Label, UEGT2Hud::Muted, X, Y, Font, 1.0f, false);
	DrawHudRect(FLinearColor(1.0f, 1.0f, 1.0f, 0.16f), TrackX, TrackY, TrackW, TrackH);
	DrawHudRect(Fill, TrackX, TrackY, TrackW * Clamped, TrackH);
	return LabelH;
}

FUEGT2HUDLife AUEGT2HUD::PrepareLife(AUEGT2Character* Explorer, float MaxWidth)
{
	FUEGT2HUDLife Result;
	const UUEGT2NeedsComponent* Life = Explorer ? Explorer->GetLife() : nullptr;
	if (!Life) { return Result; }
	Result.Needs = Life->GetNeeds();
	const FString Purse = FString::Printf(TEXT("%s    %d coins"),
		*GetRoleDisplayName(Life->GetTrade()).ToString(), Life->GetCoins());
	const FString Doing = Life->GetActivityText().ToString();
	float PurseW = 0.0f, DoingW = 0.0f, SampleW = 0.0f;
	const float ContentWidth = FMath::Max(86.0f, FMath::Min(400.0f, MaxWidth - 28.0f));
	LayoutText(Purse, GEngine->GetMediumFont(), ContentWidth, Result.Purse, PurseW, Result.PurseH, 2);
	LayoutText(Doing, GEngine->GetSmallFont(), ContentWidth, Result.Activity, DoingW, Result.ActivityH, 2);
	MeasureHudText(TEXT("Ag"), SampleW, Result.RowH, GEngine->GetSmallFont());
	Result.BodyW = FMath::Max3(PurseW, DoingW, 186.0f);
	if (HudLayout.bEnhanced) { Result.BodyW = FMath::Min(Result.BodyW, ContentWidth); }
	const float BoxH = Result.PurseH * Result.Purse.Num() + Result.ActivityH * Result.Activity.Num()
		+ Result.RowH * 4.0f + 5.0f * 5.0f + 10.0f * 2.0f;
	Result.Bounds = UEGT2HUDLayout::AnchorPanel(HudLayout, FVector2D(Result.BodyW + 28.0f, BoxH),
		EUEGT2HUDAnchor::BottomLeft, FVector2D(24.0, 26.0));
	return Result;
}

void AUEGT2HUD::DrawLife(const FUEGT2HUDLife& Life)
{
	if (!Life.Bounds.bIsValid) { return; }
	const FVector2D Origin = HudLayout.ToLogical(Life.Bounds.Min);
	const FVector2D Size = HudLayout.ToLogical(Life.Bounds.GetSize());
	const float X = Origin.X, Y = Origin.Y;
	const float PadX = 14.0f, PadY = 10.0f, Gap = 5.0f;
	DrawRoundedRect(UEGT2Hud::Shade, X, Y, Size.X, Size.Y, 7.0f);
	float Cursor = Y + PadY;
	for (const FString& Line : Life.Purse)
	{
		DrawHudText(Line, UEGT2Hud::Ink, X + PadX, Cursor, GEngine->GetMediumFont());
		Cursor += Life.PurseH;
	}
	Cursor += Gap;
	for (const FString& Line : Life.Activity)
	{
		DrawHudText(Line, UEGT2Hud::Muted, X + PadX, Cursor, GEngine->GetSmallFont());
		Cursor += Life.ActivityH;
	}
	Cursor += Gap;
	// Keep the same order as the shared needs ledger and the existing HUD.
	Cursor += DrawNeedBar(TEXT("Relief"), Life.Needs.Relief, X + PadX, Cursor, Life.BodyW) + Gap;
	Cursor += DrawNeedBar(TEXT("Fed"), Life.Needs.Fed, X + PadX, Cursor, Life.BodyW) + Gap;
	Cursor += DrawNeedBar(TEXT("Rested"), Life.Needs.Energy, X + PadX, Cursor, Life.BodyW) + Gap;
	DrawNeedBar(TEXT("Company"), Life.Needs.Company, X + PadX, Cursor, Life.BodyW);
}

void AUEGT2HUD::DrawDevStatus(float ScreenWidth)
{
	const UUEGT2DevModeSubsystem* Dev = UUEGT2DevModeSubsystem::Get(GetWorld());
	if (!Dev || !Dev->IsDevModeEnabled())
	{
		return;
	}

	TArray<FString> Parts;
	if (Dev->IsGodMode())   { Parts.Add(TEXT("god")); }
	if (Dev->IsNoclipEnabled()) { Parts.Add(TEXT("noclip")); }
	else if (Dev->IsFlyEnabled()) { Parts.Add(TEXT("fly")); }
	if (Dev->GetSpeedMultiplier() > 1.01f)
	{
		Parts.Add(FString::Printf(TEXT("%.0fx"), Dev->GetSpeedMultiplier()));
	}
	if (!FMath::IsNearlyEqual(Dev->GetGameSpeed(), 1.0f))
	{
		Parts.Add(FString::Printf(TEXT("time %.2fx"), Dev->GetGameSpeed()));
	}

	const float Hours = Dev->GetTimeOfDay();
	const int32 Whole = FMath::Clamp(FMath::FloorToInt(Hours), 0, 23);
	const int32 Minutes = FMath::Clamp(FMath::RoundToInt((Hours - Whole) * 60.0f), 0, 59);

	const FString Text = FString::Printf(TEXT("DEV  %02d:%02d  %s%s%s"),
		Whole, Minutes,
		*GetWeatherDisplayName(Dev->GetWeather()).ToString(),
		Parts.Num() > 0 ? TEXT("  ") : TEXT(""),
		*FString::Join(Parts, TEXT("  ")));

	UFont* Font = GEngine->GetSmallFont();
	float Width = 0.0f, Height = 0.0f;
	GetTextSize(Text, Width, Height, Font, 1.0f);

	const float X = ScreenWidth - Width - 28.0f;
	const float Y = 20.0f;
	DrawRect(UEGT2Hud::Shade, X - 10.0f, Y - 5.0f, Width + 20.0f, Height + 10.0f);
	DrawText(Text, UEGT2Hud::Accent, X, Y, Font, 1.0f, false);
}

void AUEGT2HUD::DrawDiagnostics(AUEGT2Character* Explorer)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const float DeltaMs = World->GetDeltaSeconds() * 1000.0f;
	SmoothedDeltaMs = FMath::Lerp(SmoothedDeltaMs, DeltaMs, 0.08f);
	const float Fps = SmoothedDeltaMs > KINDA_SMALL_NUMBER ? 1000.0f / SmoothedDeltaMs : 0.0f;

	TArray<FString> Lines;
	Lines.Add(TEXT("FAIRHAVEN DIAGNOSTICS  (F3)"));
	Lines.Add(FString::Printf(TEXT("%.1f fps   %.2f ms"), Fps, SmoothedDeltaMs));

	if (Explorer)
	{
		const FVector Location = Explorer->GetActorLocation();
		Lines.Add(FString::Printf(TEXT("pos  X %.0f  Y %.0f  Z %.0f"), Location.X, Location.Y, Location.Z));
		Lines.Add(FString::Printf(TEXT("     %.0f m N   %.0f m E   %.0f m up"),
			Location.X / 100.0f, Location.Y / 100.0f, Location.Z / 100.0f));
		Lines.Add(FString::Printf(TEXT("speed %.0f cm/s%s"),
			Explorer->GetHorizontalSpeed(), Explorer->IsSprinting() ? TEXT("  [sprint]") : TEXT("")));

		if (const UUEGT2InteractionComponent* Interaction = Explorer->GetInteraction())
		{
			const AActor* Focus = Interaction->GetFocusedActor();
			Lines.Add(FString::Printf(TEXT("focus %s"), Focus ? *Focus->GetName() : TEXT("-")));
		}
	}

	if (const UUEGT2NPCDirector* Director = UUEGT2NPCDirector::Get(World))
	{
		const FString DayLabel = Director->GetDayLabel().ToString();
		Lines.Add(FString::Printf(TEXT("town  %d people  %d animals  %d out"),
			Director->GetPeopleCount(), Director->GetAnimalCount(), Director->GetActiveCount()));
		Lines.Add(FString::Printf(TEXT("      near %d  talking %d  density %.0f%%%s"),
			Director->GetNearCount(), Director->GetSpeakingCount(),
			Director->GetCrowdDensity() * 100.0f,
			DayLabel.IsEmpty() ? TEXT("") : *FString::Printf(TEXT("  (%s)"), *DayLabel)));
	}

	if (const UUEGT2GameUserSettings* Settings = UUEGT2GameUserSettings::Get())
	{
		Lines.Add(FString::Printf(TEXT("quality  view %d  shadow %d  gi %d  refl %d"),
			Settings->GetViewDistanceQuality(), Settings->GetShadowQuality(),
			Settings->GetGlobalIlluminationQuality(), Settings->GetReflectionQuality()));
		Lines.Add(FString::Printf(TEXT("res scale %.0f%%   fov %.0f"),
			Settings->GetResolutionScalePercent(), Settings->GetFieldOfView()));
	}

	UFont* Font = GEngine->GetSmallFont();
	const float LineHeight = 15.0f;
	const float PanelWidth = 340.0f;
	const float PanelHeight = LineHeight * Lines.Num() + 18.0f;

	DrawRect(FLinearColor(0.0f, 0.0f, 0.0f, 0.55f), 16.0f, 16.0f, PanelWidth, PanelHeight);

	float Y = 25.0f;
	for (int32 Index = 0; Index < Lines.Num(); ++Index)
	{
		DrawText(Lines[Index], Index == 0 ? UEGT2Hud::Accent : UEGT2Hud::Muted, 28.0f, Y, Font, 1.0f, false);
		Y += LineHeight;
	}
}

// ---------------------------------------------------------------------------
// Speech bubbles
// ---------------------------------------------------------------------------
void AUEGT2HUD::MeasureHudText(const FString& Text, float& W, float& H, UFont* Font, float Scale) const
{
	GetTextSize(Text, W, H, Font, Scale * HudLayout.Scale);
	W /= HudLayout.Scale;
	H /= HudLayout.Scale;
}

void AUEGT2HUD::DrawHudText(const FString& Text, FLinearColor Colour, float X, float Y, UFont* Font,
	float Scale, bool bScalePosition)
{
	if (bScalePosition) { X *= Scale; Y *= Scale; }
	DrawText(Text, Colour, X * HudLayout.Scale, Y * HudLayout.Scale, Font, Scale * HudLayout.Scale, false);
}

void AUEGT2HUD::DrawHudRect(FLinearColor Colour, float X, float Y, float W, float H)
{
	DrawRect(Colour, X * HudLayout.Scale, Y * HudLayout.Scale, W * HudLayout.Scale, H * HudLayout.Scale);
}

void AUEGT2HUD::DrawHudLine(float X1, float Y1, float X2, float Y2, FLinearColor Colour, float Thickness)
{
	DrawLine(X1 * HudLayout.Scale, Y1 * HudLayout.Scale, X2 * HudLayout.Scale, Y2 * HudLayout.Scale, Colour, Thickness * HudLayout.Scale);
}

void AUEGT2HUD::LayoutText(const FString& Text, UFont* Font, float MaxWidth, TArray<FString>& Lines,
	float& Width, float& LineHeight, int32 MaxLines) const
{
	if (!HudLayout.bEnhanced)
	{
		Lines = { Text };
		MeasureHudText(Text, Width, LineHeight, Font);
		return;
	}
	WrapText(Text, Font, 1.0f, FMath::Max(1.0f, MaxWidth), Lines, Width);
	float SampleW = 0.0f;
	MeasureHudText(TEXT("Ag"), SampleW, LineHeight, Font);
	if (Lines.Num() > MaxLines)
	{
		Lines.SetNum(MaxLines);
		Lines.Last() += TEXT("...");
	}
	Width = 0.0f;
	for (FString& Line : Lines)
	{
		float W = 0.0f, H = 0.0f;
		MeasureHudText(Line, W, H, Font);
		if (W > MaxWidth)
		{
			// Long key labels and names may contain no spaces. Keep a bounded,
			// legible prefix rather than letting one token defeat wrapping.
			FString Prefix = Line;
			do
			{
				Prefix.LeftChopInline(1);
				Line = Prefix + TEXT("...");
				MeasureHudText(Line, W, H, Font);
			} while (W > MaxWidth && !Prefix.IsEmpty());
		}
		Width = FMath::Max(Width, W);
	}
}

void AUEGT2HUD::WrapText(const FString& Text, UFont* Font, float Scale, float MaxWidth,
	TArray<FString>& OutLines, float& OutWidth) const
{
	OutLines.Reset();
	OutWidth = 0.0f;

	TArray<FString> Words;
	Text.ParseIntoArray(Words, TEXT(" "), true);
	if (Words.Num() == 0)
	{
		return;
	}

	FString Line;
	for (const FString& Word : Words)
	{
		const FString Candidate = Line.IsEmpty() ? Word : Line + TEXT(" ") + Word;
		float Width = 0.0f, Height = 0.0f;
		MeasureHudText(Candidate, Width, Height, Font, Scale);
		if (Width > MaxWidth && !Line.IsEmpty())
		{
			OutLines.Add(Line);
			Line = Word;
		}
		else
		{
			Line = Candidate;
		}
	}
	if (!Line.IsEmpty())
	{
		OutLines.Add(Line);
	}

	for (const FString& Row : OutLines)
	{
		float Width = 0.0f, Height = 0.0f;
		MeasureHudText(Row, Width, Height, Font, Scale);
		OutWidth = FMath::Max(OutWidth, Width);
	}
}

void AUEGT2HUD::DrawRoundedRect(const FLinearColor& Colour, float X, float Y, float W, float H,
	float Corner)
{
	// Canvas has no rounded rectangle, and a material-based one would mean a
	// binary asset. Three overlapping rectangles produce the same silhouette at
	// this size: a body, plus a wider band and a taller band that between them
	// knock the corners off.
	const float C = FMath::Clamp(Corner, 0.0f, FMath::Min(W, H) * 0.5f);
	DrawHudRect(Colour, X + C, Y, W - C * 2.0f, H);
	DrawHudRect(Colour, X, Y + C, W, H - C * 2.0f);
	DrawHudRect(Colour, X + C * 0.4f, Y + C * 0.4f, W - C * 0.8f, H - C * 0.8f);
}

void AUEGT2HUD::DrawSpeechBubbles(const TArray<FBox2D>& PlayerPanels)
{
	const UUEGT2NPCDirector* Director = UUEGT2NPCDirector::Get(GetWorld());
	if (!Director || !PlayerOwner || !Canvas)
	{
		return;
	}

	FVector ViewLocation = FVector::ZeroVector;
	FRotator ViewRotation = FRotator::ZeroRotator;
	PlayerOwner->GetPlayerViewPoint(ViewLocation, ViewRotation);

	TArray<FUEGT2SpeechBubble> Bubbles;
	Director->GatherBubbles(ViewLocation, Bubbles);

	// Farthest first, so a near bubble that has to move ends up above the far
	// one rather than the other way round.
	TArray<FBox2D> Placed;
	if (HudLayout.bEnhanced) { Placed = PlayerPanels; }
	Placed.Reserve(Bubbles.Num());
	for (const FUEGT2SpeechBubble& Bubble : Bubbles)
	{
		DrawOneBubble(Bubble, Placed);
	}

	// Under -UEGT2LiveNPCs, say where each bubble was laid out. A screenshot
	// with no bubbles in it cannot distinguish "nobody was speaking" from
	// "they were all projected off the top of the screen", and the round trip
	// to find out is a repackage.
	const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
	if (Director->IsLoggingSpeech() && Bubbles.Num() > 0 && Now >= NextBubbleLogTime)
	{
		NextBubbleLogTime = Now + 1.0f;
		for (const FUEGT2SpeechBubble& Bubble : Bubbles)
		{
			const FVector Screen = Project(Bubble.WorldLocation, false);
			UE_LOG(LogUEGT2UI, Log,
				TEXT("Bubble '%s' world %s -> screen (%.0f, %.0f, depth %.4f) alpha %.2f"),
				*Bubble.Line.ToString(), *Bubble.WorldLocation.ToCompactString(),
				Screen.X, Screen.Y, Screen.Z, Bubble.Alpha);
		}
	}
}

void AUEGT2HUD::DrawOneBubble(const FUEGT2SpeechBubble& Bubble, TArray<FBox2D>& Placed)
{
	// bClampToZeroPlane must be off: with it on, a speaker behind the camera
	// projects onto the screen edge and their bubble hovers over nothing.
	const FVector PhysicalScreen = Project(Bubble.WorldLocation, false);
	const FVector Screen(PhysicalScreen.X / HudLayout.Scale, PhysicalScreen.Y / HudLayout.Scale, PhysicalScreen.Z);
	if (Screen.Z <= 0.0f)
	{
		return;
	}

	const float DistanceFade = 1.0f - FMath::Clamp(
		(Bubble.Distance - UEGT2Hud::BubbleFadeStart)
		/ (UEGT2Hud::BubbleFadeEnd - UEGT2Hud::BubbleFadeStart), 0.0f, 1.0f);
	const float Alpha = Bubble.Alpha * DistanceFade;
	if (Alpha <= 0.02f)
	{
		return;
	}

	UFont* BodyFont = GEngine->GetMediumFont();
	UFont* NameFont = GEngine->GetSmallFont();

	// While "typing", the bubble is a short one with animated dots. It is the
	// same trick every messaging app uses and it does the same job here: it
	// tells you someone is about to say something and gives you time to look.
	FString BodyText;
	if (Bubble.bTyping)
	{
		const float Time = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
		const int32 Dots = 1 + (int32)(Time * 3.0f) % 3;
		BodyText = FString::ChrN(Dots, TEXT('.'));
	}
	else
	{
		BodyText = Bubble.Line.ToString();
	}

	TArray<FString> Lines;
	float TextWidth = 0.0f;
	float LineHeight = 0.0f;
	const float MaxTextWidth = FMath::Min(UEGT2Hud::BubbleMaxWidth, (Canvas->ClipX - 16.0f) / HudLayout.Scale - 22.0f);
	if (HudLayout.bEnhanced) { LayoutText(BodyText, BodyFont, MaxTextWidth, Lines, TextWidth, LineHeight, 5); }
	else { WrapText(BodyText, BodyFont, 1.0f, UEGT2Hud::BubbleMaxWidth, Lines, TextWidth); }
	if (Lines.Num() == 0)
	{
		return;
	}

	FString Speaker = Bubble.Speaker.ToString();
	float NameWidth = 0.0f, NameHeight = 0.0f;
	if (!Speaker.IsEmpty())
	{
		TArray<FString> Names;
		LayoutText(Speaker, NameFont, MaxTextWidth, Names, NameWidth, NameHeight, 1);
		Speaker = Names.IsEmpty() ? FString() : Names[0];
	}

	float SampleWidth = 0.0f;
	MeasureHudText(TEXT("Ag"), SampleWidth, LineHeight, BodyFont, 1.0f);

	const float PadX = 11.0f;
	const float PadY = 8.0f;
	const float HeaderHeight = Speaker.IsEmpty() ? 0.0f : NameHeight + 3.0f;
	const float BoxWidth = FMath::Max(TextWidth, NameWidth) + PadX * 2.0f;
	const float BoxHeight = HeaderHeight + LineHeight * Lines.Num() + PadY * 2.0f;

	const float TailHeight = 9.0f;
	float BoxX = Screen.X - BoxWidth * 0.5f;
	float BoxY = Screen.Y - BoxHeight - TailHeight;
	if (HudLayout.bEnhanced)
	{
		FBox2D Bounds(ForceInit);
		if (!UEGT2HUDLayout::PlaceBubble(HudLayout, FVector2D(PhysicalScreen.X, PhysicalScreen.Y),
			FVector2D(BoxWidth, BoxHeight + TailHeight), Placed, Bounds)) { return; }
		const FVector2D Origin = HudLayout.ToLogical(Bounds.Min);
		BoxX = Origin.X;
		BoxY = Origin.Y;
		Placed.Add(Bounds);
	}
	else
	{

		// Keep the whole bubble on screen; the tail still points at the speaker.
		const float Margin = 8.0f;
		BoxX = FMath::Clamp(BoxX, Margin, FMath::Max(Margin, Canvas->ClipX - BoxWidth - Margin));
		BoxY = FMath::Max(BoxY, Margin);

		// Push up out of anything already drawn. Bounded: after a few tries the
		// screen is simply too crowded, and one more unreadable overlap helps
		// nobody, so the bubble is dropped instead.
		for (int32 Attempt = 0; Attempt < 8; ++Attempt)
		{
			bool bClear = true;
			for (const FBox2D& Taken : Placed)
			{
				if (BoxX < Taken.Max.X && BoxX + BoxWidth > Taken.Min.X
					&& BoxY < Taken.Max.Y && BoxY + BoxHeight > Taken.Min.Y)
				{
					BoxY = Taken.Min.Y - BoxHeight - 6.0f;
					bClear = false;
					break;
				}
			}
			if (bClear)
			{
				break;
			}
		}
		if (BoxY < Margin)
		{
			return;
		}
		Placed.Emplace(FVector2D(BoxX, BoxY), FVector2D(BoxX + BoxWidth, BoxY + BoxHeight + TailHeight));
	}

	auto WithAlpha = [Alpha](const FLinearColor& Colour, float Scale)
	{
		return FLinearColor(Colour.R, Colour.G, Colour.B, Colour.A * Alpha * Scale);
	};

	// Dark and fairly opaque. The bubbles sit over grass, sand and sky in the
	// same frame, and anything lighter than this stops being readable over one
	// of the three.
	const FLinearColor Body(Bubble.Tint.R * 0.30f, Bubble.Tint.G * 0.30f, Bubble.Tint.B * 0.30f, 0.93f);
	const FLinearColor Shadow(0.0f, 0.0f, 0.0f, 0.5f);

	DrawRoundedRect(WithAlpha(Shadow, 1.0f), BoxX + 2.0f, BoxY + 3.0f, BoxWidth, BoxHeight, 7.0f);
	DrawRoundedRect(WithAlpha(Body, 1.0f), BoxX, BoxY, BoxWidth, BoxHeight, 7.0f);

	// The tail: a stack of narrowing rows, which is a triangle at this size.
	const float TailX = FMath::Clamp(Screen.X, BoxX + 10.0f, BoxX + BoxWidth - 10.0f);
	for (int32 Row = 0; Row < 7; ++Row)
	{
		const float RowWidth = 14.0f * (1.0f - Row / 7.0f);
		DrawHudRect(WithAlpha(Body, 1.0f), TailX - RowWidth * 0.5f,
			BoxY + BoxHeight + Row * (TailHeight / 7.0f), RowWidth, TailHeight / 7.0f + 0.6f);
	}

	float Y = BoxY + PadY;
	if (!Speaker.IsEmpty())
	{
		const FLinearColor NameColour(
			FMath::Min(Bubble.Tint.R * 1.9f + 0.3f, 1.0f),
			FMath::Min(Bubble.Tint.G * 1.9f + 0.3f, 1.0f),
			FMath::Min(Bubble.Tint.B * 1.9f + 0.3f, 1.0f), 1.0f);
		DrawHudText(Speaker, WithAlpha(NameColour, 0.95f), BoxX + PadX, Y, NameFont, 1.0f, false);
		Y += HeaderHeight;
	}

	// Animals get their sounds in the muted colour: it reads as a stage
	// direction rather than as speech, which is what it is.
	const FLinearColor TextColour = Bubble.bAnimal ? UEGT2Hud::Muted : UEGT2Hud::Ink;
	for (const FString& Line : Lines)
	{
		DrawHudText(Line, WithAlpha(TextColour, 1.0f), BoxX + PadX, Y, BodyFont, 1.0f, false);
		Y += LineHeight;
	}
}
