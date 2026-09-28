#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ArachneStimulusSourceComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FArachneCaughtSignature, AActor*, Predator);

/**
 * Makes its owner prey: emits footstep noise from movement, tells predators how far away it can be seen,
 * and receives the "caught" event. Put it on the player character; tune it in BP_PlayerCharacter.
 */
UCLASS(ClassGroup=(Arachne), meta=(BlueprintSpawnableComponent))
class ARACHNE_API UArachneStimulusSourceComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UArachneStimulusSourceComponent();
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

    // ---------------------------------------------------------------- noise
    /** Hearing range of one walking footstep (cm). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stimulus|Noise") float WalkNoiseRadius = 500.f;
    /** Hearing range of one sprinting footstep (cm). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stimulus|Noise") float SprintNoiseRadius = 1400.f;
    /** Distance travelled per footstep while walking / sprinting. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stimulus|Noise") float WalkStepLength = 160.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stimulus|Noise") float SprintStepLength = 210.f;
    /** Slower than this makes no footsteps at all (creeping). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stimulus|Noise") float SilentSpeed = 60.f;

    // ---------------------------------------------------------------- visibility
    /** Distance from which the prey can be seen in the dark, flashlight off (cm). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stimulus|Visibility") float VisibleDistance = 900.f;
    /** Distance from which the prey can be seen with the flashlight on (cm). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stimulus|Visibility") float FlashlightVisibleDistance = 2200.f;
    /** Sprinting makes you easier to spot. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stimulus|Visibility") float SprintVisibilityMultiplier = 1.25f;

    // ---------------------------------------------------------------- debug
    /** Always draw noise and visibility ranges (F1 / Arachne.Debug draws them too). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stimulus|Debug") bool bDebug = false;

    UPROPERTY(BlueprintAssignable, Category="Stimulus") FArachneCaughtSignature OnCaught;

    UFUNCTION(BlueprintCallable, Category="Stimulus") void SetSprinting(bool bInSprinting) { bSprinting = bInSprinting; }
    UFUNCTION(BlueprintCallable, Category="Stimulus") void SetFlashlightOn(bool bOn) { bFlashlight = bOn; }
    /** Emit an arbitrary noise at the owner (thrown objects, doors, ...). */
    UFUNCTION(BlueprintCallable, Category="Stimulus") void MakeNoiseAt(FVector Location, float Radius);

    /** Current distance from which a predator with perfect eyes could see the prey. */
    UFUNCTION(BlueprintPure, Category="Stimulus") float GetVisibleDistance() const;
    /** Radius of the footsteps the prey makes right now (0 when still or creeping). */
    UFUNCTION(BlueprintPure, Category="Stimulus") float GetCurrentNoiseRadius() const;
    UFUNCTION(BlueprintPure, Category="Stimulus") bool IsCaught() const { return bCaught; }
    UFUNCTION(BlueprintPure, Category="Stimulus") bool IsFlashlightOn() const { return bFlashlight; }

    /** Points a predator checks for line of sight: eyes first, then the body centre. */
    void GetSensePoints(TArray<FVector>& Out) const;
    /** Called by the predator when it grabs the prey. */
    void NotifyCaught(AActor* Predator);
    bool CanBeSensed() const { return !bCaught; }

private:
    bool IsDebugOn() const;
    void DrawDebug() const;

    bool bSprinting = false;
    bool bFlashlight = false;
    bool bCaught = false;
    float StepAccumulator = 0.f;
    float LastStepRadius = 0.f;
    float LastStepAge = 100.f;
};
