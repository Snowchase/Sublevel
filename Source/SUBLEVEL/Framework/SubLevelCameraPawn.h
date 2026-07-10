#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "SubLevelCameraPawn.generated.h"

class UCameraComponent;

// Top-down management camera. The player controller drives pan (XY)
// and zoom (Z height) from keyboard/mouse input.

UCLASS()
class SUBLEVEL_API ASubLevelCameraPawn : public APawn
{
    GENERATED_BODY()

public:
    ASubLevelCameraPawn();

    // Moves the pawn in world XY (already scaled by the controller).
    void AddPanInput(const FVector2D& WorldDelta);

    void  SetCameraHeight(float NewHeight);
    float GetCameraHeight() const { return GetActorLocation().Z; }

private:
    UPROPERTY() USceneComponent*  SceneRoot = nullptr;
    UPROPERTY() UCameraComponent* Camera    = nullptr;
};
