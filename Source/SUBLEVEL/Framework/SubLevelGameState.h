#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "SubLevelTypes.h"
#include "SubLevelGameState.generated.h"

UCLASS()
class SUBLEVEL_API ASubLevelGameState : public AGameStateBase
{
    GENERATED_BODY()

public:
    ASubLevelGameState();

    // ── Floor state ───────────────────────────────────────────────
    UFUNCTION(BlueprintCallable) bool  IsFloorUnlocked(int32 FloorIndex) const;
    UFUNCTION(BlueprintCallable) float GetFloorIntegrity(int32 FloorIndex) const;
    void SetFloorUnlocked(int32 FloorIndex, bool bUnlocked);
    void SetFloorIntegrity(int32 FloorIndex, float Integrity);

    // ── Faction ───────────────────────────────────────────────────
    UFUNCTION(BlueprintCallable) float GetFactionScore(EFactionType Faction) const;
    void ApplyFactionDelta(EFactionType Faction, float Delta);

    // §14 save/load — direct access to the full score block
    const FFactionScores& GetFactionScoresRaw() const { return FactionScores; }
    void SetFactionScoresRaw(const FFactionScores& InScores) { FactionScores = InScores; }

    // ── Economy snapshot (read by UI) ─────────────────────────────
    UPROPERTY(BlueprintReadOnly) float CurrentBalance   = 0.0f;
    UPROPERTY(BlueprintReadOnly) float DailyRevenue     = 0.0f;
    UPROPERTY(BlueprintReadOnly) float LoanPrincipal    = 0.0f;
    UPROPERTY(BlueprintReadOnly) float LoanRepaymentDue = 0.0f;

    // ── Session ───────────────────────────────────────────────────
    UPROPERTY(BlueprintReadOnly) int32 CurrentDay  = 1;
    UPROPERTY(BlueprintReadOnly) int32 ActiveSeed  = 0;

private:
    TArray<bool>   FloorUnlocked;
    TArray<float>  FloorIntegrity;
    FFactionScores FactionScores;
};
