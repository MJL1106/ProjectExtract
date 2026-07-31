// UAttachmentStatPreviewWidget -- see header.

#include "UI/AttachmentStatPreviewWidget.h"

#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"

#include "Character/ExtractionPlayerInterface.h"
#include "Components/WeaponComponent.h"
#include "Data/WeaponAttachmentDataAsset.h"
#include "Data/WeaponDataAsset.h"
#include "UI/AttachmentStatRowWidget.h"
#include "Weapon/WeaponBase.h"

namespace
{
	// -----------------------------------------------------------------------------------------
	// KIT SLOT BYTES -- the kit Blueprint's ENUM_AttachmentSlot, which is NOT the C++
	// EAttachmentSlot order. Kit 2 is Muzzle where C++ 2 is Laser, so casting one to the other
	// silently reads the WRONG stat array. Every kit byte is translated in ResolveKitSlot below
	// and NOWHERE ELSE.
	//
	//   kit 0 Sights     -> SightAttachments      / Selection.Sight
	//   kit 1 Laser      -> LaserAttachments      / Selection.Laser
	//   kit 2 Muzzle     -> MuzzleAttachments     / Selection.Muzzle
	//   kit 3 LeftHand   -> GripAttachments       / Selection.Grip
	//   kit 4 Handguards -> HandguardAttachments  / Selection.Handguard
	//   kit 5 Barrels    -> no stat array at all (cosmetic only)
	//
	// REORDERING ENUM_AttachmentSlot IN THE KIT BLUEPRINT SILENTLY BREAKS THIS TABLE -- the
	// preview would then quote another slot's numbers with no error anywhere. If a slot is added
	// or moved there, this table moves with it.
	// -----------------------------------------------------------------------------------------

	constexpr uint8 KitSlotSights = 0;
	constexpr uint8 KitSlotLaser = 1;
	constexpr uint8 KitSlotMuzzle = 2;
	constexpr uint8 KitSlotLeftHand = 3;
	constexpr uint8 KitSlotHandguards = 4;
	constexpr uint8 KitSlotBarrels = 5;

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
	 */
	bool ResolveKitSlot(const UWeaponDataAsset& Data, const FWeaponAttachmentSelection& Selection,
		uint8 KitSlotByte, const FAttachmentOptions*& OutOptions, uint8& OutFittedByte)
	{
		switch (KitSlotByte)
		{
		case KitSlotSights:     OutOptions = &Data.SightAttachments;     OutFittedByte = Selection.Sight;     break;
		case KitSlotLaser:      OutOptions = &Data.LaserAttachments;     OutFittedByte = Selection.Laser;     break;
		case KitSlotMuzzle:     OutOptions = &Data.MuzzleAttachments;    OutFittedByte = Selection.Muzzle;    break;
		case KitSlotLeftHand:   OutOptions = &Data.GripAttachments;      OutFittedByte = Selection.Grip;      break;
		case KitSlotHandguards: OutOptions = &Data.HandguardAttachments; OutFittedByte = Selection.Handguard; break;
		case KitSlotBarrels:    return false;
		default:                return false;
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
		CachedSelection = Weapon->GetAttachmentSelection();
		CachedKitSlotByte = KitSlotByte;
		CachedOptionByte = OptionByte;
		bCachedCompatible = bCompatible;
		bCacheValid = true;
		return;
	}

	const TArray<FAttachmentStatDelta> Deltas = BuildStatDeltas(Weapon, KitSlotByte, OptionByte);
	if (Deltas.Num() == 0)
	{
		ShowMessageOnly(CosmeticOnlyMessage);
	}
	else if (PopulateRows(Deltas) == 0)
	{
		ShowMessageOnly(NoChangeMessage);
	}
	else if (IsValid(MessageText))
	{
		MessageText->SetVisibility(ESlateVisibility::Collapsed);
	}

	// Cache after every path so the next frame skips the rebuild.
	CachedWeapon = Weapon;
	CachedSelection = Weapon->GetAttachmentSelection();
	CachedKitSlotByte = KitSlotByte;
	CachedOptionByte = OptionByte;
	bCachedCompatible = bCompatible;
	bCacheValid = true;
}

void UAttachmentStatPreviewWidget::HidePreview()
{
	HideRowsFrom(0);
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

		// Index is the EAttachmentStatRow ordinal: BuildStatDeltas emits every row in enum order,
		// which is what lets a designer's label be matched to a row without storing an id per row.
		FAttachmentStatDelta Displayed = Deltas[Index];
		const FText& Override = GetRowLabel(static_cast<EAttachmentStatRow>(Index));
		if (!Override.IsEmpty()) Displayed.Label = Override;

		RowWidget->SetStatDelta(Displayed, BetterColour, WorseColour);
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

bool UAttachmentStatPreviewWidget::IsCacheCurrent(AWeaponBase* Weapon, uint8 KitSlotByte, uint8 OptionByte, bool bCompatible) const
{
	if (!bCacheValid) return false;
	if (!CachedWeapon.IsValid()) return false;
	if (CachedWeapon.Get() != Weapon) return false;
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
}
