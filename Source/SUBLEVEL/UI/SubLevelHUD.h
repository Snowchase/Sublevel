#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "SubLevelHUD.generated.h"

// §12 (interim) — Canvas debug HUD.
// Draws session clock, economy, faction standings, city state, incident
// alerts, and the active decision card directly to the screen so the test
// build is playable before UMG widgets exist.

UCLASS()
class SUBLEVEL_API ASubLevelHUD : public AHUD
{
    GENERATED_BODY()

public:
    virtual void DrawHUD() override;

private:
    // Draws one line and advances the cursor.
    void Line(const FString& Text, const FLinearColor& Color, float X, float& Y, float Scale = 1.f);
};
