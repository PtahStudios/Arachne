#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "AI/ArachneAITypes.h"
#include "ArachneBrainComponent.generated.h"

class AArachnePawn;
class AArachneWaypoint;
class AArachneCampPoint;
class UArachneSensesComponent;
class UArachneMemoryComponent;
class UArachneNavigatorComponent;
class UArachneWaypointSubsystem;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FArachneStateChangedSignature, EArachneState, OldState, EArachneState, NewState);

/**
 * Arachne's decisions: a small state machine over the senses (via memory), the waypoint graph and the navigator.
 *  Patrol -> (sometimes) Camp: freeze in a corner, preferring places where she met the prey before
 *  noise -> Investigate -> Search -> Patrol
 *  confirmed contact (seen / touched) -> Hunt (the only time she sprints) -> Attack -> Feeding (on the camera)
 */
UCLASS(ClassGroup=(Arachne), meta=(BlueprintSpawnableComponent))
class ARACHNE_API UArachneBrainComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UArachneBrainComponent();
    virtual void BeginPlay() override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

    // ---------------------------------------------------------------- general
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Brain") bool bBrainEnabled = true;

    // ---------------------------------------------------------------- thresholds
    /** Awareness needed to go and check a noise. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Brain|Thresholds", meta=(ClampMin="0", ClampMax="1")) float InvestigateAwareness = .25f;
    /** Awareness needed (plus a confirmed contact) to hunt. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Brain|Thresholds", meta=(ClampMin="0", ClampMax="1")) float HuntAwareness = .6f;
    /** While camping she ignores noises below this awareness: she is waiting for the prey to come closer. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Brain|Thresholds", meta=(ClampMin="0", ClampMax="1")) float CampBreakAwareness = .55f;

    // ---------------------------------------------------------------- patrol / camp
    /** Chance after each patrol stop to go camping instead of walking on. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Brain|Patrol", meta=(ClampMin="0", ClampMax="1")) float CampChance = .35f;
    /** Nearer patrol destinations are more likely: weight = 1 / (1 + distance / falloff). Larger = roams the whole house. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Brain|Patrol", meta=(ClampMin="100")) float PatrolDistanceFalloff = 1500.f;
    /** Recently visited waypoints are avoided when picking the next one. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Brain|Patrol", meta=(ClampMin="0")) int32 AvoidRecentWaypoints = 3;
    /** Idle stillness at patrol stops (0 lively .. 1 frozen). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Brain|Patrol", meta=(ClampMin="0", ClampMax="1")) float PatrolStopStillness = .4f;
    /** Senses multiplier while frozen in a camp point. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Brain|Camp") float CampSensitivity = 1.35f;
    /** How strongly camp choice is pulled towards places where the prey was sensed before (0 = pure random). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Brain|Camp", meta=(ClampMin="0")) float CampHeatPreference = 1.f;

    // ---------------------------------------------------------------- investigate / search
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Brain|Investigate") float InvestigateAcceptRadius = 150.f;
    /** Seconds spent turning around at the noise / at each search spot. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Brain|Investigate") float LookAroundTimeMin = 1.5f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Brain|Investigate") float LookAroundTimeMax = 3.5f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Brain|Search") float SearchDuration = 25.f;
    /** Waypoints around the last known position checked while searching. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Brain|Search", meta=(ClampMin="0")) int32 SearchSpots = 3;

    // ---------------------------------------------------------------- hunt / attack
    /** Grab distance from body centre to the prey (cm). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Brain|Hunt") float AttackRange = 170.f;
    /** Re-plan the chase this often (s). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Brain|Hunt") float ChaseRepathInterval = .25f;
    /** No confirmed contact for this long = lost it, start searching (s). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Brain|Hunt") float LoseTargetTime = 3.f;
    /** Pause between reaching the prey and jumping onto the camera (s). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Brain|Attack") float AttackWindup = .35f;
    /** Distance of the virtual "lens" plane the legs grab, in front of the prey's eyes (cm). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Brain|Attack") float FaceGrabDistance = 16.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Brain|Attack") float FaceGrabBlendTime = .28f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Brain|Debug") bool bDebug = false;

    UPROPERTY(BlueprintAssignable, Category="Brain") FArachneStateChangedSignature OnStateChanged;

    UFUNCTION(BlueprintPure, Category="Brain") EArachneState GetState() const { return State; }
    UFUNCTION(BlueprintPure, Category="Brain") FString GetStateName() const;
    UFUNCTION(BlueprintPure, Category="Brain") float GetStateTime() const { return StateTime; }
    UFUNCTION(BlueprintCallable, Category="Brain") void SetBrainEnabled(bool bEnabled);

private:
    void EnterState(EArachneState NewState);
    void ExitState(EArachneState OldState);
    void EvaluateTransitions();
    void TickPatrol(float Dt);
    void TickCamp(float Dt);
    void TickInvestigate(float Dt);
    void TickHunt(float Dt);
    void TickAttack(float Dt);
    void TickSearch(float Dt);

    void PickNextPatrolPoint();
    AArachneCampPoint* PickCampPoint() const;
    void RouteTo(const FVector& Goal, float AcceptRadius, bool bSprint);
    void RouteToWaypoint(AArachneWaypoint* Waypoint, bool bSprint);
    void RouteAlongTrail();
    void BuildSearchQueue();
    bool StartNextSearchLeg();
    void BeginLookAround();
    void UpdateLookAround(float Dt);
    void GrabPreyFace();
    void RememberVisit(AArachneWaypoint* Waypoint);
    bool IsDebugOn() const;
    void DrawDebug() const;

    UPROPERTY() TObjectPtr<AArachnePawn> Pawn;
    UPROPERTY() TObjectPtr<UArachneSensesComponent> Senses;
    UPROPERTY() TObjectPtr<UArachneMemoryComponent> Memory;
    UPROPERTY() TObjectPtr<UArachneNavigatorComponent> Navigator;
    UArachneWaypointSubsystem* Waypoints = nullptr;

    EArachneState State = EArachneState::Patrol;
    float StateTime = 0.f;
    bool bWaiting = false;        // Patrol / Investigate / Search: arrived, lingering. Camp: anchored.
    float WaitTimer = 0.f;
    float LookTimer = 0.f;
    float RepathTimer = 0.f;
    int32 SeenStimulusSerial = 0;
    TWeakObjectPtr<AArachneWaypoint> CurrentWaypoint;
    TArray<TWeakObjectPtr<AArachneWaypoint>> RecentWaypoints;
    TWeakObjectPtr<AArachneCampPoint> CurrentCamp;
    TWeakObjectPtr<AArachneCampPoint> LastCamp;
    TArray<FVector> SearchQueue;
    FVector InvestigateTarget = FVector::ZeroVector;
    bool bGrabbed = false;
};
