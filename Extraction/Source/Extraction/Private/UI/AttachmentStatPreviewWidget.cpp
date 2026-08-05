// UAttachmentStatPreviewWidget -- see header.

#include "UI/AttachmentStatPreviewWidget.h"

#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"

#include "Character/ExtractionPlayerInterface.h"
#include "Components/WeaponComponent.h"
#include "Data/WeaponAttachmentDataAsset.h"
#include "Data/WeaponDataAsset.h"
#include "Game/ExtractionPlayerController.h"
#include "UI/AttachmentStatRowWidget.h"
#include "Weapon/WeaponBase.h"

DEFINE_LOG_CATEGORY_STATIC(LogAttachmentPreview, Log, All);

namespace
{
	/** Below this an "already fitted" multiplier carries no meaningful ratio -- a percentage against
	 *  ~0 is either infinite or meaningless, so the row reports no change rather than a garbage number. */
	constexpr float MinComparableMultiplier = 1.e-4f;

	/** ADS FOV clamp, mirroring AWeaponBase::GetEffectiveADSFOV and the weapon DA's own ClampMin/Max.
	 *  Kept in step by hand: if that clamp moves and this does not, the Zoom row starts lying. */
	constexpr float MinADSFOV = 20.f;
	constexpr float MaxADSFOV = 120.f;

	/** Which direction of change is good for the player, per stat. */
	enum class EStatDirection : uint8
	{
		HigherIsBetter,
		LowerIsBetter
	};

	using FAttachmentOptions = TArray<TObjectPtr<UWeaponAttachmentDataAsset>>;

	/**
	 * Translates a raw kit slot byte into the weapon's option array for that slot plus the byte
	 * currently fitted there. Returns false when the slot carries no gameplay options -- kit
	 * Barrels, an unknown byte, or a slot this weapon never authored -- which the caller reads as
	 * "cosmetic fit, no numbers to show".
	 *
	 * The kit-byte translation itself lives in KitAttachmentSlots (ExtractionTypes.h) so the
	 * preview and AWeaponBase::SetAttachmentSlotOption cannot disagree about which slot a byte
	 * means -- if they ever did, the panel would quote one slot's numbers while the pickup fitted
	 * into another.
	 */
	bool ResolveKitSlot(const UWeaponDataAsset& Data, const FWeaponAttachmentSelection& Selection,
		uint8 KitSlotByte, const FAttachmentOptions*& OutOptions, uint8& OutFittedByte)
	{
		EAttachmentSlot Slot;
		if (!KitAttachmentSlots::ToAttachmentSlot(KitSlotByte, Slot)) return false;

		switch (Slot)
		{
		case EAttachmentSlot::Sight:     OutOptions = &Data.SightAttachments;     OutFittedByte = Selection.Sight;     break;
		case EAttachmentSlot::Muzzle:    OutOptions = &Data.MuzzleAttachments;    OutFittedByte = Selection.Muzzle;    break;
		case EAttachmentSlot::Laser:     OutOptions = &Data.LaserAttachments;     OutFittedByte = Selection.Laser;     break;
		case EAttachmentSlot::Grip:      OutOptions = &Data.GripAttachments;      OutFittedByte = Selection.Grip;      break;
		case EAttachmentSlot::Handguard: OutOptions = &Data.HandguardAttachments; OutFittedByte = Selection.Handguard; break;
		default:                         return false;
		}

		return OutOptions->Num() > 0;
	}

	/** Modifiers for one option byte. An out-of-range byte or a null entry is the empty slot, which
	 *  is neutral -- so comparing against an empty slot naturally shows the attachment's own effect. */
	FWeaponStatModifiers ResolveModifiers(const FAttachmentOptions& Options, uint8 OptionByte)
	{
		if (!Options.IsValidIndex(OptionByte)) return FWeaponStatModifiers();

		const UWeaponAttachmentDataAsset* Attachment = Options[OptionByte];
		return IsValid(Attachment) ? Attachment->Modifiers : FWeaponStatModifiers();
	}

	/** Display name of one candidate option, for the panel header -- only ever asked for on the
	 *  success path, so an out-of-range byte or an unnamed asset just leaves the header blank rather
	 *  than blocking the rows it titles. */
	FText ResolveOptionDisplayName(const FAttachmentOptions& Options, uint8 OptionByte)
	{
		if (!Options.IsValidIndex(OptionByte)) return FText::GetEmpty();

		const UWeaponAttachmentDataAsset* Attachment = Options[OptionByte];
		return IsValid(Attachment) ? Attachment->DisplayName : FText::GetEmpty();
	}

	/** Slot-isolated multiplicative delta as a signed percent. Multiplicative stats compose by
	 *  PRODUCT across slots, so every other slot's contribution cancels exactly in this ratio --
	 *  the number is the true end-to-end change, not an approximation.
	 *
	 *  bOutUnbounded is set when one side is effectively zero and the other is not -- a ratio
	 *  against zero is infinite, so PercentDelta is meaningless. The row widget renders this as
	 *  a distinct string rather than a misleading "+0%". */
	float MultPercentDelta(float OldMult, float NewMult, bool& bOutUnbounded)
	{
		const bool bOldNearZero = (OldMult < MinComparableMultiplier);
		const bool bNewNearZero = (NewMult < MinComparableMultiplier);

		// Both near-zero: no meaningful change.
		if (bOldNearZero && bNewNearZero) { bOutUnbounded = false; return 0.f; }

		// One near-zero, the other not: real change, but the ratio is unbounded.
		if (bOldNearZero || bNewNearZero)
		{
			bOutUnbounded = true;
			// Sign still matters for direction: gaining a stat from zero is positive, losing it is negative.
			return (NewMult > OldMult) ? 100.f : -100.f;
		}

		bOutUnbounded = false;
		return (NewMult / OldMult - 1.f) * 100.f;
	}

	/**
	 * Zoom factor (base FOV / effective ADS FOV) this slot alone would produce, using the same
	 * precedence AWeaponBase::GetEffectiveADSFOV uses: an override wins outright over the additive
	 * delta, and the result is clamped.
	 *
	 * Isolating the slot means an FOV override coming from a DIFFERENT slot is ignored here. In
	 * practice only sights set FOV, so the preview matches what the player will actually see.
	 */
	float SlotZoomFactor(float BaseADSFOV, const FWeaponStatModifiers& Modifiers)
	{
		const float Effective = (Modifiers.ADSFOVOverride > 0.f)
			? FMath::Clamp(Modifiers.ADSFOVOverride, MinADSFOV, MaxADSFOV)
			: FMath::Clamp(BaseADSFOV + Modifiers.ADSFOVDelta, MinADSFOV, MaxADSFOV);

		// The clamp floor guarantees Effective >= MinADSFOV, so this can never divide by zero.
		return BaseADSFOV / Effective;
	}

	void AddRow(TArray<FAttachmentStatDelta>& Out, const UAttachmentStatPreviewWidget& LabelSource,
		EAttachmentStatRow Row, float Percent, EStatDirection Direction, bool bUnbounded = false)
	{
		FAttachmentStatDelta& Delta = Out.AddDefaulted_GetRef();
		Delta.Label = LabelSource.GetRowLabel(Row);
		Delta.PercentDelta = Percent;
		Delta.bIsImprovement = (Direction == EStatDirection::HigherIsBetter) ? (Percent > 0.f) : (Percent < 0.f);
		Delta.bUnboundedChange = bUnbounded;
	}

	void AddMultRow(TArray<FAttachmentStatDelta>& Out, const UAttachmentStatPreviewWidget& LabelSource,
		EAttachmentStatRow Row, float OldMult, float NewMult, EStatDirection Direction)
	{
		bool bUnbounded = false;
		const float Percent = MultPercentDelta(OldMult, NewMult, bUnbounded);
		AddRow(Out, LabelSource, Row, Percent, Direction, bUnbounded);
	}
}

void UAttachmentStatPreviewWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// One allocation for the session -- rows are reused, never re-created, as the crosshair sweeps
	// from one pickup to the next.
	RowWidgets.Reserve(AttachmentStatRowCount);
	HidePreview();

	AExtractionPlayerController* PC = Cast<AExtractionPlayerController>(GetOwningPlayer());
	// Registration is the only binding in the HUD that gets a single attempt -- there is no
	// construct-time retry to fall back on. Failing it leaves GetAttachmentStatPreview() null
	// forever and every pickup silently does nothing, so say so rather than fail invisibly.
	UE_CLOG(!PC, LogAttachmentPreview, Warning,
		TEXT("%s: no AExtractionPlayerController -- attachment pickups will not drive this panel"), *GetName());
	if (PC)
		PC->RegisterAttachmentStatPreview(this);
}

void UAttachmentStatPreviewWidget::NativeDestruct()
{
	if (AExtractionPlayerController* PC = Cast<AExtractionPlayerController>(GetOwningPlayer()))
		PC->UnregisterAttachmentStatPreview(this);

	Super::NativeDestruct();
}

// ---- Public API ----

void UAttachmentStatPreviewWidget::ShowForAttachment(uint8 KitSlotByte, uint8 OptionByte, bool bCompatible)
{
	AWeaponBase* Weapon = ResolveHeldWeapon();

	// Nothing in hand means there is no "before" to compare against, so there is nothing honest to
	// show -- not even an incompatible message, which would imply the weapon rejected it.
	if (!IsValid(Weapon))
	{
		HidePreview();
		return;
	}

	// Content is a pure function of (weapon identity, its current selection, slot, option, compat).
	// Skip the entire rebuild when all five match -- the crosshair may rest on the same pickup for
	// many frames, and rebuilding per frame allocates ~10 FTexts and forces 10 STextBlock relayouts.
	if (IsCacheCurrent(Weapon, KitSlotByte, OptionByte, bCompatible)) return;

	SetVisibility(ESlateVisibility::HitTestInvisible);

	if (!bCompatible)
	{
		ShowMessageOnly(IncompatibleMessage);
		CachedWeapon = Weapon;
		CachedCandidateData.Reset();
		CachedSelection = Weapon->GetAttachmentSelection();
		CachedKitSlotByte = KitSlotByte;
		CachedOptionByte = OptionByte;
		bCachedCompatible = bCompatible;
		bCacheValid = true;
		return;
	}

	// Fetched here (not just inside BuildStatDeltas) because the header-naming branch below needs it
	// too, and BuildStatDeltas has no way to hand its internal lookup back out to a static caller.
	const UWeaponDataAsset* Data = Weapon->GetWeaponData();

	// BuildStatDeltas is static and so labels from the CDO. Swap in this instance's designer text
	// here, where "index N is EAttachmentStatRow N" is guaranteed by that builder emitting every
	// row in enum order.
	TArray<FAttachmentStatDelta> Deltas = BuildStatDeltas(Weapon, KitSlotByte, OptionByte);
	for (int32 Index = 0; Index < Deltas.Num(); ++Index)
	{
		const FText& Override = GetRowLabel(static_cast<EAttachmentStatRow>(Index));
		if (!Override.IsEmpty()) Deltas[Index].Label = Override;
	}

	if (Deltas.Num() == 0)
	{
		ShowMessageOnly(CosmeticOnlyMessage);
	}
	else if (PopulateRows(Deltas) == 0)
	{
		ShowMessageOnly(NoChangeMessage);
	}
	else
	{
		if (IsValid(MessageText))
			MessageText->SetVisibility(ESlateVisibility::Collapsed);

		// Same slot resolution BuildStatDeltas already did internally, repeated here (cheap) because
		// that builder only surfaces stat modifiers, never the asset the header needs to name itself.
		const FAttachmentOptions* Options = nullptr;
		uint8 FittedByte = 0;
		FText CandidateName;
		if (IsValid(Data) && ResolveKitSlot(*Data, Weapon->GetAttachmentSelection(), KitSlotByte, Options, FittedByte))
			CandidateName = ResolveOptionDisplayName(*Options, OptionByte);

		SetItemHeader(CandidateName, AttachmentHeadingText);
	}

	// Cache after every path so the next frame skips the rebuild.
	CachedWeapon = Weapon;
	CachedCandidateData.Reset();
	CachedSelection = Weapon->GetAttachmentSelection();
	CachedKitSlotByte = KitSlotByte;
	CachedOptionByte = OptionByte;
	bCachedCompatible = bCompatible;
	bCacheValid = true;
}

void UAttachmentStatPreviewWidget::ShowForWeapon(UWeaponDataAsset* CandidateData)
{
	AWeaponBase* Held = ResolveHeldWeapon();
	const UWeaponDataAsset* HeldData = IsValid(Held) ? Held->GetWeaponData() : nullptr;

	// Nothing in hand, or an unauthored pickup, leaves no honest comparison to draw.
	if (!IsValid(CandidateData) || !IsValid(HeldData))
	{
		HidePreview();
		return;
	}

	const bool bCacheHit = bCacheValid && CachedCandidateData.Get() == CandidateData && CachedWeapon.Get() == Held;
	UE_LOG(LogTemp, Warning, TEXT("[WpnCmp] held=%s (%s) candidate=%s cacheHit=%s"),
		*GetNameSafe(Held), *GetNameSafe(HeldData), *GetNameSafe(CandidateData), bCacheHit ? TEXT("YES") : TEXT("no"));
	if (bCacheHit) return;

	SetVisibility(ESlateVisibility::HitTestInvisible);

	if (CandidateData == HeldData)
	{
		ShowMessageOnly(SameWeaponMessage);
	}
	else
	{
		const TArray<FAttachmentStatDelta> Deltas = BuildWeaponDeltas(*HeldData, *CandidateData);
		if (PopulateRows(Deltas) == 0)
		{
			ShowMessageOnly(NoChangeMessage);
		}
		else
		{
			if (IsValid(MessageText))
				MessageText->SetVisibility(ESlateVisibility::Collapsed);

			SetItemHeader(CandidateData->DisplayName, WeaponHeadingText);
		}
	}

	// Slot bytes back to 0xFF so a later ShowForAttachment cannot hit this entry.
	CachedWeapon = Held;
	CachedCandidateData = CandidateData;
	CachedKitSlotByte = 0xFF;
	CachedOptionByte = 0xFF;
	bCacheValid = true;
}

TArray<FAttachmentStatDelta> UAttachmentStatPreviewWidget::BuildWeaponDeltas(
	const UWeaponDataAsset& Held, const UWeaponDataAsset& Candidate) const
{
	// Mean magnitude across the pattern's points. A recoil curve has no single scalar, but the
	// average kick per shot is what the player actually feels over a burst, and it stays
	// comparable between a 3-point and a 30-point pattern.
	auto MeanRecoil = [](const UWeaponDataAsset& Data)
	{
		const TArray<FVector2D>& Points = Data.RecoilPattern.Points;
		if (Points.Num() == 0) return 0.f;

		float Total = 0.f;
		for (const FVector2D& P : Points) Total += P.Size();
		return Total / Points.Num();
	};

	// Damage per second including pellets, so a shotgun's per-trigger-pull output is not
	// understated against an automatic rifle's.
	auto DPS = [](const UWeaponDataAsset& Data)
	{
		return Data.BaseDamage * Data.FireRate * FMath::Max(1, Data.PelletCount);
	};

	TArray<FAttachmentStatDelta> Deltas;
	Deltas.Reserve(6);

	auto Add = [&Deltas](const FText& Label, float Old, float New, bool bHigherIsBetter)
	{
		if (Old <= KINDA_SMALL_NUMBER) return;

		FAttachmentStatDelta& Delta = Deltas.AddDefaulted_GetRef();
		Delta.Label = Label;
		Delta.PercentDelta = (New / Old - 1.f) * 100.f;
		Delta.bIsImprovement = bHigherIsBetter ? (Delta.PercentDelta > 0.f) : (Delta.PercentDelta < 0.f);
	};

	Add(DamageLabel, Held.BaseDamage, Candidate.BaseDamage, true);
	Add(FireRateLabel, Held.FireRate, Candidate.FireRate, true);
	Add(DPSLabel, DPS(Held), DPS(Candidate), true);
	Add(MagazineLabel, Held.MagazineSize, Candidate.MagazineSize, true);
	Add(ADSTimeLabel, Held.ADSTransitionTime, Candidate.ADSTransitionTime, false);
	Add(WeaponRecoilLabel, MeanRecoil(Held), MeanRecoil(Candidate), false);

	return Deltas;
}

void UAttachmentStatPreviewWidget::HidePreview()
{
	HideRowsFrom(0);
	ClearItemHeader();
	InvalidateCache();

	if (IsValid(MessageText))
		MessageText->SetVisibility(ESlateVisibility::Collapsed);

	SetVisibility(ESlateVisibility::Collapsed);
}

TArray<FAttachmentStatDelta> UAttachmentStatPreviewWidget::BuildStatDeltas(AWeaponBase* Weapon, uint8 KitSlotByte, uint8 OptionByte)
{
	TArray<FAttachmentStatDelta> Deltas;
	if (!IsValid(Weapon)) return Deltas;

	const UWeaponDataAsset* Data = Weapon->GetWeaponData();
	if (!IsValid(Data)) return Deltas;

	const FAttachmentOptions* Options = nullptr;
	uint8 FittedByte = 0;
	if (!ResolveKitSlot(*Data, Weapon->GetAttachmentSelection(), KitSlotByte, Options, FittedByte)) return Deltas;

	// Delta is against what is ALREADY FITTED in that slot, not against the bare gun.
	const FWeaponStatModifiers Old = ResolveModifiers(*Options, FittedByte);
	const FWeaponStatModifiers New = ResolveModifiers(*Options, OptionByte);

	// Static entry point: the class defaults carry the built-in labels. An instance overrides them
	// with its own designer text in PopulateRows.
	const UAttachmentStatPreviewWidget& Labels = *GetDefault<UAttachmentStatPreviewWidget>();

	// Order below IS EAttachmentStatRow order -- PopulateRows indexes the result with it.
	Deltas.Reserve(AttachmentStatRowCount);
	AddMultRow(Deltas, Labels, EAttachmentStatRow::Damage, Old.DamageMult, New.DamageMult, EStatDirection::HigherIsBetter);
	AddMultRow(Deltas, Labels, EAttachmentStatRow::VerticalRecoil, Old.RecoilPitchMult, New.RecoilPitchMult, EStatDirection::LowerIsBetter);
	AddMultRow(Deltas, Labels, EAttachmentStatRow::HorizontalRecoil, Old.RecoilYawMult, New.RecoilYawMult, EStatDirection::LowerIsBetter);
	AddMultRow(Deltas, Labels, EAttachmentStatRow::ADSTime, Old.ADSTransitionMult, New.ADSTransitionMult, EStatDirection::LowerIsBetter);
	AddMultRow(Deltas, Labels, EAttachmentStatRow::ADSMoveSpeed, Old.ADSMoveSpeedMult, New.ADSMoveSpeedMult, EStatDirection::HigherIsBetter);
	AddMultRow(Deltas, Labels, EAttachmentStatRow::HipSpread, Old.HipSpreadMult, New.HipSpreadMult, EStatDirection::LowerIsBetter);
	AddMultRow(Deltas, Labels, EAttachmentStatRow::Noise, Old.NoiseLoudnessMult, New.NoiseLoudnessMult, EStatDirection::LowerIsBetter);
	AddMultRow(Deltas, Labels, EAttachmentStatRow::NoiseRange, Old.NoiseRangeMult, New.NoiseRangeMult, EStatDirection::LowerIsBetter);
	AddMultRow(Deltas, Labels, EAttachmentStatRow::EffectiveRange, Old.FalloffStartMult, New.FalloffStartMult, EStatDirection::HigherIsBetter);

	// Zoom is not multiplicative: override beats delta, so both sides resolve to a real FOV first.
	const float OldZoom = SlotZoomFactor(Data->ADSFOV, Old);
	const float NewZoom = SlotZoomFactor(Data->ADSFOV, New);
	{
		bool bZoomUnbounded = false;
		const float ZoomPercent = MultPercentDelta(OldZoom, NewZoom, bZoomUnbounded);
		AddRow(Deltas, Labels, EAttachmentStatRow::Zoom, ZoomPercent, EStatDirection::HigherIsBetter, bZoomUnbounded);
	}

	return Deltas;
}

const FText& UAttachmentStatPreviewWidget::GetRowLabel(EAttachmentStatRow Row) const
{
	switch (Row)
	{
	case EAttachmentStatRow::Damage:           return DamageLabel;
	case EAttachmentStatRow::VerticalRecoil:   return VerticalRecoilLabel;
	case EAttachmentStatRow::HorizontalRecoil: return HorizontalRecoilLabel;
	case EAttachmentStatRow::ADSTime:          return ADSTimeLabel;
	case EAttachmentStatRow::ADSMoveSpeed:     return ADSMoveSpeedLabel;
	case EAttachmentStatRow::HipSpread:        return HipSpreadLabel;
	case EAttachmentStatRow::Noise:            return NoiseLabel;
	case EAttachmentStatRow::NoiseRange:       return NoiseRangeLabel;
	case EAttachmentStatRow::EffectiveRange:   return EffectiveRangeLabel;
	case EAttachmentStatRow::Zoom:             return ZoomLabel;
	default:                                   return FText::GetEmpty();
	}
}

// ---- Internals ----

AWeaponBase* UAttachmentStatPreviewWidget::ResolveHeldWeapon() const
{
	const IExtractionPlayerInterface* PlayerIface = Cast<IExtractionPlayerInterface>(GetOwningPlayerPawn());
	if (!PlayerIface) return nullptr;

	const UWeaponComponent* WeaponComp = PlayerIface->GetWeaponComponent();
	if (!IsValid(WeaponComp)) return nullptr;

	return WeaponComp->GetCurrentWeapon();
}

int32 UAttachmentStatPreviewWidget::PopulateRows(const TArray<FAttachmentStatDelta>& Deltas)
{
	int32 RowIndex = 0;

	for (int32 Index = 0; Index < Deltas.Num(); ++Index)
	{
		if (FMath::Abs(Deltas[Index].PercentDelta) < MinPercentToShow) continue;

		UAttachmentStatRowWidget* RowWidget = GetOrCreateRow(RowIndex);
		if (!IsValid(RowWidget)) break;

		// Renders whatever labels it is handed. The attachment path relabels by row ordinal before
		// calling here (only valid for its fixed row set); the weapon path builds with this
		// instance's labels directly. Doing it here would apply attachment labels to weapon rows.
		RowWidget->SetStatDelta(Deltas[Index], BetterColour, WorseColour);
		RowWidget->SetVisibility(ESlateVisibility::HitTestInvisible);
		++RowIndex;
	}

	HideRowsFrom(RowIndex);
	return RowIndex;
}

UAttachmentStatRowWidget* UAttachmentStatPreviewWidget::GetOrCreateRow(int32 Index)
{
	if (RowWidgets.IsValidIndex(Index)) return RowWidgets[Index];

	if (!IsValid(StatContainer)) return nullptr;

	// A null class means a silently empty panel, so say so once rather than shipping a blank box.
	if (!ensureMsgf(RowWidgetClass != nullptr, TEXT("RowWidgetClass not assigned on %s -- the stat preview will render no rows."), *GetName()))
		return nullptr;

	UAttachmentStatRowWidget* RowWidget = CreateWidget<UAttachmentStatRowWidget>(this, RowWidgetClass);
	if (!IsValid(RowWidget)) return nullptr;

	// StatContainer must be a multi-child panel (VerticalBox / ScrollBox). A single-child panel
	// (Border / SizeBox) silently refuses the second child and returns nullptr here.
	if (!StatContainer->AddChild(RowWidget)) return nullptr;

	RowWidgets.Add(RowWidget);
	return RowWidget;
}

void UAttachmentStatPreviewWidget::HideRowsFrom(int32 FirstIndex)
{
	for (int32 Index = FirstIndex; Index < RowWidgets.Num(); ++Index)
	{
		if (IsValid(RowWidgets[Index]))
			RowWidgets[Index]->SetVisibility(ESlateVisibility::Collapsed);
	}
}

void UAttachmentStatPreviewWidget::ShowMessageOnly(const FText& Message)
{
	HideRowsFrom(0);
	ClearItemHeader();

	// MessageText is BindWidgetOptional. A WBP without it must not show a visible empty panel --
	// "incompatible" is a common case, and a blank rectangle is worse than nothing.
	if (!IsValid(MessageText))
	{
		HidePreview();
		return;
	}

	MessageText->SetText(Message);
	MessageText->SetVisibility(ESlateVisibility::HitTestInvisible);
}

void UAttachmentStatPreviewWidget::SetItemHeader(const FText& ItemName, const FText& Heading)
{
	if (IsValid(ItemNameText))
		ItemNameText->SetText(ItemName);

	if (IsValid(HeadingText))
		HeadingText->SetText(Heading);
}

void UAttachmentStatPreviewWidget::ClearItemHeader()
{
	if (IsValid(ItemNameText))
		ItemNameText->SetText(FText::GetEmpty());

	if (IsValid(HeadingText))
		HeadingText->SetText(FText::GetEmpty());
}

bool UAttachmentStatPreviewWidget::IsCacheCurrent(AWeaponBase* Weapon, uint8 KitSlotByte, uint8 OptionByte, bool bCompatible) const
{
	if (!bCacheValid) return false;
	if (!CachedWeapon.IsValid()) return false;
	if (CachedWeapon.Get() != Weapon) return false;
	// A live candidate means the last show was a weapon comparison, so this entry is not ours.
	if (CachedCandidateData.IsValid()) return false;
	if (CachedKitSlotByte != KitSlotByte) return false;
	if (CachedOptionByte != OptionByte) return false;
	if (bCachedCompatible != bCompatible) return false;

	// The player can fit an attachment while a pickup is still under the crosshair -- the selection
	// changing means the "already fitted" baseline shifted, so the delta must be recomputed.
	const FWeaponAttachmentSelection Current = Weapon->GetAttachmentSelection();
	return FMemory::Memcmp(&CachedSelection, &Current, sizeof(FWeaponAttachmentSelection)) == 0;
}

void UAttachmentStatPreviewWidget::InvalidateCache()
{
	bCacheValid = false;
	CachedWeapon.Reset();
	CachedCandidateData.Reset();
}
