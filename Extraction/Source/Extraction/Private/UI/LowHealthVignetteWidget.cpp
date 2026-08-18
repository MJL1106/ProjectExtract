// ULowHealthVignetteWidget implementation.

#include "UI/LowHealthVignetteWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/HealthComponent.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Engine/Texture2D.h"
#include "GameFramework/Pawn.h"

namespace
{
	/** Mask resolution. The gradient is smooth and stretched full-screen, so quality is set by the
	 *  corner curve, not texel density — 512 keeps the rounded corners clean at 4K. */
	constexpr int32 VignetteTextureSize = 512;
}

bool ULowHealthVignetteWidget::Initialize()
{
	if (!Super::Initialize()) return false;
	if (!WidgetTree) return false;

	// A Blueprint subclass brings its own designer tree; only the raw C++ spawn builds one here.
	if (!WidgetTree->RootWidget)
	{
		UOverlay* Root = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("VignetteRoot"));
		WidgetTree->RootWidget = Root;

		VignetteTexture = BuildVignetteTexture();

		DarkenImage = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("DarkenImage"));
		EdgeImage = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("EdgeImage"));

		// Darken below, red edges above. The default UImage brush is a plain white box, which is
		// exactly what the uniform darken layer needs — only the tint matters.
		for (UImage* Image : { DarkenImage.Get(), EdgeImage.Get() })
		{
			UOverlaySlot* ImageSlot = Root->AddChildToOverlay(Image);
			ImageSlot->SetHorizontalAlignment(HAlign_Fill);
			ImageSlot->SetVerticalAlignment(VAlign_Fill);
		}

		if (VignetteTexture)
			EdgeImage->SetBrushFromTexture(VignetteTexture);

		DarkenImage->SetColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, MaxDarkenOpacity));
		EdgeImage->SetColorAndOpacity(FLinearColor(EdgeColor.R, EdgeColor.G, EdgeColor.B, MaxRedOpacity));
	}

	// Never eats input, and stays visible (not collapsed) at zero intensity: a collapsed widget
	// stops ticking, and the tick is what fades the effect back in.
	SetVisibility(ESlateVisibility::HitTestInvisible);
	SetRenderOpacity(0.f);
	return true;
}

void ULowHealthVignetteWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	const float Combined = ResolveCombinedPool();
	const float Target = (Combined < 0.f) ? 0.f : MapIntensity(Combined);

	DisplayedIntensity = FMath::FInterpTo(DisplayedIntensity, Target, InDeltaTime, FadeSpeed);
	if (FMath::IsNearlyEqual(DisplayedIntensity, Target, 0.005f))
		DisplayedIntensity = Target;

	// One knob fades both layers together; their relative strengths are baked into the tints.
	SetRenderOpacity(DisplayedIntensity);
}

float ULowHealthVignetteWidget::ResolveCombinedPool()
{
	const APawn* Pawn = GetOwningPlayerPawn();
	if (!IsValid(Pawn)) return -1.f;

	if (CachedPawn.Get() != Pawn || !CachedHealth.IsValid())
	{
		CachedPawn = Pawn;
		CachedHealth = Pawn->FindComponentByClass<UHealthComponent>();
	}

	const UHealthComponent* Health = CachedHealth.Get();
	if (!IsValid(Health)) return -1.f;

	// Dead reads as "nothing to warn about": the death flow owns the screen, and a fade-out under
	// it beats a red frame frozen at full intensity behind the failure UI.
	if (Health->IsDead()) return -1.f;

	return Health->GetCurrentHealth() + Health->GetCurrentShield();
}

float ULowHealthVignetteWidget::MapIntensity(float Combined) const
{
	if (Combined >= WarnThreshold) return 0.f;
	if (Combined <= MaxThreshold) return 1.f;

	if (Combined > IntenseThreshold)
	{
		const float Band = FMath::Max(WarnThreshold - IntenseThreshold, KINDA_SMALL_NUMBER);
		return IntenseKneeIntensity * (WarnThreshold - Combined) / Band;
	}

	const float Band = FMath::Max(IntenseThreshold - MaxThreshold, KINDA_SMALL_NUMBER);
	return IntenseKneeIntensity + (1.f - IntenseKneeIntensity) * (IntenseThreshold - Combined) / Band;
}

UTexture2D* ULowHealthVignetteWidget::BuildVignetteTexture() const
{
	UTexture2D* Texture = UTexture2D::CreateTransient(VignetteTextureSize, VignetteTextureSize, PF_B8G8R8A8);
	if (!Texture) return nullptr;

	// Linear: the RGB is flat white (tint carries the color) and the alpha ramp must not be
	// gamma-bent, or the falloff reads as a hard ring instead of a smooth bleed.
	Texture->SRGB = false;
	Texture->CompressionSettings = TC_EditorIcon;
	Texture->Filter = TF_Bilinear;

	FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
	uint8* Pixels = static_cast<uint8*>(Mip.BulkData.Lock(LOCK_READ_WRITE));

	// Rounded-rect distance field: alpha 0 inside the inset rect, ramping to 1 at the border.
	// The corner of the inset rect is a point, so the iso-lines around it are arcs — the red
	// reaches further inward at the corners, which is the "corners curved in" look. EdgeInset is
	// both the edge thickness and the corner radius by construction.
	const float Inset = FMath::Clamp(EdgeInset, 0.1f, 0.9f);
	for (int32 Y = 0; Y < VignetteTextureSize; ++Y)
	{
		for (int32 X = 0; X < VignetteTextureSize; ++X)
		{
			const float U = ((X + 0.5f) / VignetteTextureSize) * 2.f - 1.f;
			const float V = ((Y + 0.5f) / VignetteTextureSize) * 2.f - 1.f;

			const float QX = FMath::Max(FMath::Abs(U) - (1.f - Inset), 0.f);
			const float QY = FMath::Max(FMath::Abs(V) - (1.f - Inset), 0.f);
			const float Dist = FMath::Sqrt(QX * QX + QY * QY);

			float Alpha = FMath::Clamp(Dist / Inset, 0.f, 1.f);
			Alpha = FMath::Pow(Alpha, EdgeFalloffPower);

			uint8* Px = Pixels + (Y * VignetteTextureSize + X) * 4;
			Px[0] = 255; // B
			Px[1] = 255; // G
			Px[2] = 255; // R
			Px[3] = static_cast<uint8>(Alpha * 255.f + 0.5f);
		}
	}

	Mip.BulkData.Unlock();
	Texture->UpdateResource();
	return Texture;
}
