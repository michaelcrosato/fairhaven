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
#include "Player/UEGT2PlayerController.h"
#include "Settings/UEGT2GameUserSettings.h"
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

	const float CentreX = Canvas->ClipX * 0.5f;
	const float CentreY = Canvas->ClipY * 0.5f;

	const bool bHasFocus = Explorer && Explorer->GetInteraction()
		&& Explorer->GetInteraction()->GetFocusedActor() != nullptr;

	if (!Settings || Settings->GetShowCrosshair())
	{
		DrawCrosshair(CentreX, CentreY, bHasFocus);
	}
	if (!Settings || Settings->GetShowInteractPrompts())
	{
		DrawPrompt(CentreX, CentreY);
	}
	DrawMessage(CentreX, Canvas->ClipY);
	if (!Settings || Settings->GetShowSpeechBubbles())
	{
		DrawSpeechBubbles();
	}

	if (PC && PC->IsDiagnosticsVisible())
	{
		DrawDiagnostics(Explorer);
	}

	if (!Settings || Settings->GetShowAlmanac())
	{
		DrawAlmanac(Canvas->ClipX);
	}
	DrawDevStatus(Canvas->ClipX);
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

void AUEGT2HUD::DrawPrompt(float CentreX, float CentreY)
{
	AUEGT2PlayerController* PC = Cast<AUEGT2PlayerController>(PlayerOwner);
	AUEGT2Character* Explorer = PC ? Cast<AUEGT2Character>(PC->GetPawn()) : nullptr;
	if (!Explorer || !Explorer->GetInteraction())
	{
		return;
	}
	const FText Prompt = Explorer->GetInteraction()->GetFocusedPrompt();
	if (Prompt.IsEmpty())
	{
		return;
	}

	const FKey Key = UUEGT2InputConfig::GetEffectiveKey(EUEGT2InputSlot::Interact);
	const FString Text = FString::Printf(TEXT("[%s]  %s"),
		Key.IsValid() ? *Key.GetDisplayName().ToString() : TEXT("?"),
		*Prompt.ToString());

	UFont* Font = GEngine->GetMediumFont();
	float Width = 0.0f, Height = 0.0f;
	GetTextSize(Text, Width, Height, Font, 1.0f);

	const float X = CentreX - Width * 0.5f;
	const float Y = CentreY + 62.0f;

	DrawRect(UEGT2Hud::Shade, X - 12.0f, Y - 6.0f, Width + 24.0f, Height + 12.0f);
	DrawText(Text, UEGT2Hud::Ink, X, Y, Font, 1.0f, false);
}

void AUEGT2HUD::DrawMessage(float CentreX, float ScreenHeight)
{
	if (CurrentMessage.IsEmpty() || !GetWorld() || GetWorld()->GetTimeSeconds() > MessageExpiry)
	{
		return;
	}
	const FString Text = CurrentMessage.ToString();
	UFont* Font = GEngine->GetMediumFont();
	float Width = 0.0f, Height = 0.0f;
	GetTextSize(Text, Width, Height, Font, 1.0f);

	const float Remaining = MessageExpiry - GetWorld()->GetTimeSeconds();
	const float Alpha = FMath::Clamp(Remaining, 0.0f, 1.0f);
	const float X = CentreX - Width * 0.5f;
	const float Y = ScreenHeight - 140.0f;

	DrawRect(FLinearColor(UEGT2Hud::Shade.R, UEGT2Hud::Shade.G, UEGT2Hud::Shade.B, UEGT2Hud::Shade.A * Alpha),
		X - 14.0f, Y - 7.0f, Width + 28.0f, Height + 14.0f);
	DrawText(Text, FLinearColor(UEGT2Hud::Ink.R, UEGT2Hud::Ink.G, UEGT2Hud::Ink.B, Alpha), X, Y, Font, 1.0f, false);
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
	GetTextSize(Clock, ClockW, ClockH, Big, 1.0f);
	GetTextSize(DateLine, DateW, DateH, Small, 1.0f);
	GetTextSize(Conditions, CondW, CondH, Small, 1.0f);

	const float PadX = 14.0f;
	const float PadY = 10.0f;
	const float Gap = 4.0f;
	const float BoxW = FMath::Max3(ClockW, DateW, CondW) + PadX * 2.0f;
	const float BoxH = ClockH + DateH + CondH + Gap * 2.0f + PadY * 2.0f;
	const float X = 24.0f;
	const float Y = 20.0f;

	DrawRoundedRect(UEGT2Hud::Shade, X, Y, BoxW, BoxH, 7.0f);

	float Cursor = Y + PadY;
	DrawText(Clock, UEGT2Hud::Ink, X + PadX, Cursor, Big, 1.0f, false);
	Cursor += ClockH + Gap;
	DrawText(DateLine, UEGT2Hud::Muted, X + PadX, Cursor, Small, 1.0f, false);
	Cursor += DateH + Gap;
	DrawText(Conditions, UEGT2Hud::Accent, X + PadX, Cursor, Small, 1.0f, false);
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
		const_cast<AUEGT2HUD*>(this)->GetTextSize(Candidate, Width, Height, Font, Scale);
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
		const_cast<AUEGT2HUD*>(this)->GetTextSize(Row, Width, Height, Font, Scale);
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
	DrawRect(Colour, X + C, Y, W - C * 2.0f, H);
	DrawRect(Colour, X, Y + C, W, H - C * 2.0f);
	DrawRect(Colour, X + C * 0.4f, Y + C * 0.4f, W - C * 0.8f, H - C * 0.8f);
}

void AUEGT2HUD::DrawSpeechBubbles()
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
	const FVector Screen = Project(Bubble.WorldLocation, false);
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
	WrapText(BodyText, BodyFont, 1.0f, UEGT2Hud::BubbleMaxWidth, Lines, TextWidth);
	if (Lines.Num() == 0)
	{
		return;
	}

	const FString Speaker = Bubble.Speaker.ToString();
	float NameWidth = 0.0f, NameHeight = 0.0f;
	if (!Speaker.IsEmpty())
	{
		GetTextSize(Speaker, NameWidth, NameHeight, NameFont, 1.0f);
	}

	float SampleWidth = 0.0f, LineHeight = 0.0f;
	GetTextSize(TEXT("Ag"), SampleWidth, LineHeight, BodyFont, 1.0f);

	const float PadX = 11.0f;
	const float PadY = 8.0f;
	const float HeaderHeight = Speaker.IsEmpty() ? 0.0f : NameHeight + 3.0f;
	const float BoxWidth = FMath::Max(TextWidth, NameWidth) + PadX * 2.0f;
	const float BoxHeight = HeaderHeight + LineHeight * Lines.Num() + PadY * 2.0f;

	const float TailHeight = 9.0f;
	float BoxX = Screen.X - BoxWidth * 0.5f;
	float BoxY = Screen.Y - BoxHeight - TailHeight;

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
		DrawRect(WithAlpha(Body, 1.0f), TailX - RowWidth * 0.5f,
			BoxY + BoxHeight + Row * (TailHeight / 7.0f), RowWidth, TailHeight / 7.0f + 0.6f);
	}

	float Y = BoxY + PadY;
	if (!Speaker.IsEmpty())
	{
		const FLinearColor NameColour(
			FMath::Min(Bubble.Tint.R * 1.9f + 0.3f, 1.0f),
			FMath::Min(Bubble.Tint.G * 1.9f + 0.3f, 1.0f),
			FMath::Min(Bubble.Tint.B * 1.9f + 0.3f, 1.0f), 1.0f);
		DrawText(Speaker, WithAlpha(NameColour, 0.95f), BoxX + PadX, Y, NameFont, 1.0f, false);
		Y += HeaderHeight;
	}

	// Animals get their sounds in the muted colour: it reads as a stage
	// direction rather than as speech, which is what it is.
	const FLinearColor TextColour = Bubble.bAnimal ? UEGT2Hud::Muted : UEGT2Hud::Ink;
	for (const FString& Line : Lines)
	{
		DrawText(Line, WithAlpha(TextColour, 1.0f), BoxX + PadX, Y, BodyFont, 1.0f, false);
		Y += LineHeight;
	}
}
