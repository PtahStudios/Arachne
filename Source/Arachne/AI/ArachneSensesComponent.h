#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ArachneSensesComponent.generated.h"

class AArachnePawn;
class UArachneMemoryComponent;
class UArachneStimulusSourceComponent;
struct FArachneNoise;

/**
 * Arachne's senses, all raycast based (no AI perception system, no navmesh):
 *  - Sight: cone from the eyes, line-of-sight casts to the prey's eyes and body; builds up over time, faster up close.
 *    Range = the shorter of SightRange and how far the prey can be seen (flashlight!).
 *  - Hearing: footsteps and noises; a noise with a wall in between carries only OccludedHearingScale of its range.
 *  - Touch: air movement / vibration right next to the body, any direction, needs a clear line.
 * Results go to UArachneMemoryComponent.
 */
UCLASS(ClassGroup=(Arachne), meta=(BlueprintSpawnableComponent))
class ARACHNE_API UArachneSensesComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UArachneSensesComponent();
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

    // ---------------------------------------------------------------- sight
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Senses|Sight") float SightRange = 1800.f;
    /** Half angle of the eye cone (deg). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Senses|Sight", meta=(ClampMin="1", ClampMax="180")) float SightHalfAngle = 60.f;
    /** Seconds of continuous sight at the edge of range to be sure (up close it takes ~35% of that). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Senses|Sight", meta=(ClampMin="0.05")) float SightBuildUpTime = 1.2f;
    /** Awareness at which a sighting counts as confirmed (starts the hunt). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Senses|Sight") float SightConfirmAwareness = .6f;

    // ---------------------------------------------------------------- hearing
    /** Multiplies every noise radius. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Senses|Hearing") float HearingScale = 1.f;
    /** Noise range kept when geometry is between the noise and the spider. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Senses|Hearing", meta=(ClampMin="0", ClampMax="1")) float OccludedHearingScale = .45f;
    /** Awareness from a noise right next to her (falls off with distance). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Senses|Hearing") float HearingAwareness = .4f;

    // ---------------------------------------------------------------- touch
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Senses|Touch") float TouchRadius = 160.f;

    // ---------------------------------------------------------------- debug
    /** Always draw the senses (F1 / Arachne.Debug draws them too). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Senses|Debug") bool bDebug = false;

    /** Runtime multiplier on all ranges (the brain raises it while camping). */
    UFUNCTION(BlueprintCallable, Category="Senses") void SetSensitivity(float InSensitivity) { Sensitivity = FMath::Max(0.f, InSensitivity); }
    UFUNCTION(BlueprintPure, Category="Senses") float GetSensitivity() const { return Sensitivity; }
    /** True while the prey is inside the eye cone with a clear line (this frame). */
    UFUNCTION(BlueprintPure, Category="Senses") bool IsSeeingPrey() const { return bSeeingPrey; }

private:
    void HandleNoise(const FArachneNoise& Noise);
    void SenseSource(UArachneStimulusSourceComponent* Source, float Dt);
    bool HasLineOfSight(const FVector& From, const FVector& To, const AActor* Target) const;
    bool IsDebugOn() const;
    void DrawDebug() const;

    UPROPERTY() TObjectPtr<AArachnePawn> Pawn;
    UPROPERTY() TObjectPtr<UArachneMemoryComponent> Memory;
    FDelegateHandle NoiseHandle;
    float Sensitivity = 1.f;
    bool bSeeingPrey = false;
    FVector SeenPoint = FVector::ZeroVector;
};
