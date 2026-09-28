#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ArachneMover.generated.h"

class UStaticMeshComponent;

/** Kinematic moving/rotating block for testing surface adhesion on moving supports. */
UCLASS(Blueprintable)
class ARACHNE_API AArachneMover : public AActor
{
    GENERATED_BODY()
public:
    AArachneMover();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Mover") TObjectPtr<UStaticMeshComponent> Mesh;
    /** Peak offset from the start location (sine motion). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mover") FVector Amplitude = FVector(0, 300, 0);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mover", meta=(ClampMin="0.1")) float Period = 6.f;
    /** Degrees per second. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mover") FRotator SpinRate = FRotator::ZeroRotator;

private:
    FVector Origin = FVector::ZeroVector;
    float Time = 0.f;
};
