#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "ArachnePlayerCharacter.generated.h"

class UCameraComponent;
class USpotLightComponent;
class UArachneStimulusSourceComponent;
class UInputAction;
class UInputMappingContext;
struct FInputActionValue;

/**
 * First-person player: just a camera (no arms, legs or body), a flashlight on F and sprint on Shift.
 * Noise and visibility towards Arachne live in the Stimulus component. Tune everything in BP_PlayerCharacter.
 * When caught, input stops, the view turns to the spider and the HUD offers a reset.
 */
UCLASS(Blueprintable)
class ARACHNE_API AArachnePlayerCharacter : public ACharacter
{
    GENERATED_BODY()
public:
    AArachnePlayerCharacter();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void SetupPlayerInputComponent(UInputComponent* Input) override;
    virtual void NotifyControllerChanged() override;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Player") TObjectPtr<UCameraComponent> Camera;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Player") TObjectPtr<USpotLightComponent> Flashlight;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Player") TObjectPtr<UArachneStimulusSourceComponent> Stimulus;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Player|Movement") float WalkSpeed = 260.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Player|Movement") float SprintSpeed = 520.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Player|Look") float LookSensitivity = 1.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Player|Flashlight") bool bFlashlightOnAtStart = false;
    /** Flashlight brightness in lumens. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Player|Flashlight") float FlashlightLumens = 900.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Player|Flashlight") float FlashlightRange = 2500.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Player|Flashlight") float FlashlightConeAngle = 26.f;
    /** How fast the view snaps to the spider when caught. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Player|Caught") float CaughtTurnSpeed = 14.f;

    UFUNCTION(BlueprintCallable, Category="Player") void SetFlashlight(bool bOn);
    UFUNCTION(BlueprintPure, Category="Player") bool IsFlashlightOn() const;
    UFUNCTION(BlueprintPure, Category="Player") bool IsSprinting() const { return bSprinting; }
    UFUNCTION(BlueprintPure, Category="Player") bool IsCaught() const { return bCaught; }
    /** Reloads the current level. */
    UFUNCTION(BlueprintCallable, Category="Player") void RestartLevel();

private:
    void EnsureInputAssets();
    void OnMove(const FInputActionValue& Value);
    void OnLook(const FInputActionValue& Value);
    void OnSprintOn(const FInputActionValue& Value);
    void OnSprintOff(const FInputActionValue& Value);
    void OnFlashlight(const FInputActionValue& Value);
    void OnDebug(const FInputActionValue& Value);
    void OnReset(const FInputActionValue& Value);
    void ApplySprint(bool bSprint);
    UFUNCTION() void HandleCaught(AActor* Predator);

    UPROPERTY(Transient) TObjectPtr<UInputMappingContext> InputContext;
    UPROPERTY(Transient) TObjectPtr<UInputAction> MoveAction;
    UPROPERTY(Transient) TObjectPtr<UInputAction> LookAction;
    UPROPERTY(Transient) TObjectPtr<UInputAction> SprintAction;
    UPROPERTY(Transient) TObjectPtr<UInputAction> FlashlightAction;
    UPROPERTY(Transient) TObjectPtr<UInputAction> DebugAction;
    UPROPERTY(Transient) TObjectPtr<UInputAction> ResetAction;

    bool bSprinting = false;
    bool bCaught = false;
    TWeakObjectPtr<AActor> CaughtBy;
};
