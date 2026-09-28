#include "ArachnePawn.h"
#include "Components/SphereComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/PlayerController.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "Engine/LocalPlayer.h"
#include "DrawDebugHelpers.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputMappingContext.h"
#include "InputAction.h"
#include "InputModifiers.h"
#include "InputActionValue.h"

DEFINE_LOG_CATEGORY_STATIC(LogArachne, Log, All);

namespace ArachneMath
{
    static double Damp(double Rate, double Dt) { return 1.0 - FMath::Exp(-Rate * Dt); }

    static FVector PlaneDir(const FVector& V, const FVector& Normal, const FVector& Fallback)
    {
        const FVector P = FVector::VectorPlaneProject(V, Normal).GetSafeNormal();
        return P.IsNearlyZero() ? Fallback : P;
    }

    static double SignedAngleAround(const FVector& From, const FVector& To, const FVector& Axis)
    {
        const FVector A = FVector::VectorPlaneProject(From, Axis).GetSafeNormal();
        const FVector B = FVector::VectorPlaneProject(To, Axis).GetSafeNormal();
        if (A.IsNearlyZero() || B.IsNearlyZero()) return 0.0;
        return FMath::Atan2(FVector::DotProduct(FVector::CrossProduct(A, B), Axis), FVector::DotProduct(A, B));
    }

    static FVector SlerpNormal(const FVector& From, const FVector& To, double Alpha)
    {
        const FQuat Full = FQuat::FindBetweenNormals(From, To);
        return FQuat::Slerp(FQuat::Identity, Full, Alpha).RotateVector(From).GetSafeNormal();
    }

    /** Rigid rotation Q about a pivot point, as a transform applied after a component-space pose. */
    static FTransform RotateAbout(const FVector& Pivot, const FQuat& Q)
    {
        return FTransform(Q, Pivot - Q.RotateVector(Pivot));
    }
}
using namespace ArachneMath;

static const TCHAR* ArachneMeshPath = TEXT("/Game/ARACHNE/Characters/SK_Arachne.SK_Arachne");

void FArachneSpring::Step(const FVector& Target, double Frequency, double Damping, double Dt)
{
    const double W = 2.0 * PI * Frequency;
    V += ((Target - X) * (W * W) - V * (2.0 * Damping * W)) * Dt;
    X += V * Dt;
}

// =====================================================================================================================
// Construction
// =====================================================================================================================

AArachnePawn::AArachnePawn()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickGroup = TG_PostPhysics;   // movers/platforms have already moved this frame

    Collision = CreateDefaultSubobject<USphereComponent>(TEXT("BodyCollision"));
    SetRootComponent(Collision);
    Collision->InitSphereRadius(46.f);
    Collision->SetCollisionProfileName(TEXT("Pawn"));
    Collision->SetCanEverAffectNavigation(false);

    SpiderMesh = CreateDefaultSubobject<UPoseableMeshComponent>(TEXT("ArachneSkeletalMesh"));
    SpiderMesh->SetupAttachment(Collision);
    SpiderMesh->SetRelativeLocation(FVector(0, 0, -BodyHeight));
    SpiderMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    SpiderMesh->SetCanEverAffectNavigation(false);
    SpiderMesh->BoundsScale = 1.8f;

    CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("SurfaceCameraBoom"));
    CameraBoom->SetupAttachment(Collision);
    CameraBoom->SetRelativeLocation(FVector(0, 0, 55));
    CameraBoom->SetUsingAbsoluteRotation(true);
    CameraBoom->TargetArmLength = DesiredArmLength;
    CameraBoom->bEnableCameraLag = true;
    CameraBoom->CameraLagSpeed = 10.f;
    CameraBoom->bEnableCameraRotationLag = true;
    CameraBoom->CameraRotationLagSpeed = 9.f;
    CameraBoom->ProbeSize = 14.f;
    CameraBoom->bUsePawnControlRotation = false;

    Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
    Camera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
    Camera->FieldOfView = 85.f;

    AutoPossessAI = EAutoPossessAI::Disabled;

    for (int32 S = 0; S < 2; ++S)
    {
        for (int32 K = 0; K < 4; ++K) { PalpBones[S][K] = INDEX_NONE; PalpPivots[S][K] = FVector::ZeroVector; }
    }
}

void AArachnePawn::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
    if (!SpiderAsset) SpiderAsset = LoadObject<USkeletalMesh>(nullptr, ArachneMeshPath, nullptr, LOAD_NoWarn);
    if (SpiderAsset && SpiderMesh->GetSkinnedAsset() != SpiderAsset) SpiderMesh->SetSkinnedAssetAndUpdate(SpiderAsset);
    SpiderMesh->SetRelativeLocationAndRotation(FVector(0, 0, -BodyHeight), FQuat::Identity);
}

void AArachnePawn::BeginPlay()
{
    Super::BeginPlay();
    if (!SpiderAsset) SpiderAsset = LoadObject<USkeletalMesh>(nullptr, ArachneMeshPath, nullptr, LOAD_NoWarn);
    if (SpiderAsset && SpiderMesh->GetSkinnedAsset() != SpiderAsset) SpiderMesh->SetSkinnedAssetAndUpdate(SpiderAsset);
    if (!SpiderAsset) UE_LOG(LogArachne, Error, TEXT("ARACHNE: %s missing - run Scripts/setup_arachne.py"), ArachneMeshPath);

    SpawnLocation = GetActorLocation();
    SpawnRotation = GetActorRotation();
    InitializeRig();
    ResetCrawler(SpawnLocation, SpawnRotation);

    if (APlayerController* PC = Cast<APlayerController>(GetController()))
    {
        PC->SetInputMode(FInputModeGameOnly());
        PC->bShowMouseCursor = false;
    }
}

// =====================================================================================================================
// Input (Enhanced Input, created at runtime so the project needs no input assets)
// =====================================================================================================================

void AArachnePawn::EnsureInputAssets()
{
    if (InputContext) return;

    auto MakeAction = [this](const TCHAR* Name, EInputActionValueType Type)
    {
        UInputAction* Action = NewObject<UInputAction>(this, Name);
        Action->ValueType = Type;
        return Action;
    };
    MoveAction   = MakeAction(TEXT("IA_ArachneMove"), EInputActionValueType::Axis2D);
    LookAction   = MakeAction(TEXT("IA_ArachneLook"), EInputActionValueType::Axis2D);
    ZoomAction   = MakeAction(TEXT("IA_ArachneZoom"), EInputActionValueType::Axis1D);
    SprintAction = MakeAction(TEXT("IA_ArachneSprint"), EInputActionValueType::Boolean);
    JumpAction   = MakeAction(TEXT("IA_ArachneJump"), EInputActionValueType::Boolean);
    DebugAction  = MakeAction(TEXT("IA_ArachneDebug"), EInputActionValueType::Boolean);
    ResetAction  = MakeAction(TEXT("IA_ArachneReset"), EInputActionValueType::Boolean);

    InputContext = NewObject<UInputMappingContext>(this, TEXT("IMC_Arachne"));
    auto Map = [this](UInputAction* Action, const FKey& Key, bool bSwizzle, bool bNegate) -> FEnhancedActionKeyMapping&
    {
        FEnhancedActionKeyMapping& Mapping = InputContext->MapKey(Action, Key);
        if (bSwizzle) Mapping.Modifiers.Add(NewObject<UInputModifierSwizzleAxis>(InputContext));  // default order YXZ
        if (bNegate) Mapping.Modifiers.Add(NewObject<UInputModifierNegate>(InputContext));
        return Mapping;
    };
    // Move: X = forward, Y = right
    Map(MoveAction, EKeys::W, false, false);
    Map(MoveAction, EKeys::S, false, true);
    Map(MoveAction, EKeys::D, true, false);
    Map(MoveAction, EKeys::A, true, true);
    {
        FEnhancedActionKeyMapping& Stick = Map(MoveAction, EKeys::Gamepad_Left2D, true, false);
        Stick.Modifiers.Insert(NewObject<UInputModifierDeadZone>(InputContext), 0);
    }
    Map(LookAction, EKeys::Mouse2D, false, false);
    {
        FEnhancedActionKeyMapping& Stick = Map(LookAction, EKeys::Gamepad_Right2D, false, false);
        Stick.Modifiers.Add(NewObject<UInputModifierDeadZone>(InputContext));
        Stick.Modifiers.Add(NewObject<UInputModifierScaleByDeltaTime>(InputContext));
        UInputModifierScalar* Scalar = NewObject<UInputModifierScalar>(InputContext);
        Scalar->Scalar = FVector(60.0, 45.0, 1.0);
        Stick.Modifiers.Add(Scalar);
    }
    Map(ZoomAction, EKeys::MouseWheelAxis, false, false);
    Map(SprintAction, EKeys::LeftShift, false, false);
    Map(SprintAction, EKeys::Gamepad_LeftThumbstick, false, false);
    Map(JumpAction, EKeys::SpaceBar, false, false);
    Map(JumpAction, EKeys::Gamepad_FaceButton_Bottom, false, false);
    Map(DebugAction, EKeys::F1, false, false);
    Map(ResetAction, EKeys::R, false, false);
    Map(ResetAction, EKeys::Gamepad_Special_Left, false, false);
}

void AArachnePawn::NotifyControllerChanged()
{
    Super::NotifyControllerChanged();
    EnsureInputAssets();
    if (APlayerController* PC = Cast<APlayerController>(Controller))
    {
        if (ULocalPlayer* LocalPlayer = PC->GetLocalPlayer())
        {
            if (UEnhancedInputLocalPlayerSubsystem* Subsystem = LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
            {
                Subsystem->RemoveMappingContext(InputContext);
                Subsystem->AddMappingContext(InputContext, 0);
            }
        }
    }
}

void AArachnePawn::SetupPlayerInputComponent(UInputComponent* Input)
{
    Super::SetupPlayerInputComponent(Input);
    EnsureInputAssets();
    UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(Input);
    if (!EIC)
    {
        UE_LOG(LogArachne, Error, TEXT("ARACHNE: Enhanced Input component required (Project Settings > Input > Default Input Component Class)"));
        return;
    }
    EIC->BindAction(MoveAction, ETriggerEvent::Triggered, this, &AArachnePawn::OnMove);
    EIC->BindAction(MoveAction, ETriggerEvent::Completed, this, &AArachnePawn::OnMoveStop);
    EIC->BindAction(LookAction, ETriggerEvent::Triggered, this, &AArachnePawn::OnLook);
    EIC->BindAction(ZoomAction, ETriggerEvent::Triggered, this, &AArachnePawn::OnZoom);
    EIC->BindAction(SprintAction, ETriggerEvent::Started, this, &AArachnePawn::OnSprintOn);
    EIC->BindAction(SprintAction, ETriggerEvent::Completed, this, &AArachnePawn::OnSprintOff);
    EIC->BindAction(JumpAction, ETriggerEvent::Started, this, &AArachnePawn::OnJump);
    EIC->BindAction(DebugAction, ETriggerEvent::Started, this, &AArachnePawn::OnDebug);
    EIC->BindAction(ResetAction, ETriggerEvent::Started, this, &AArachnePawn::OnReset);
}

void AArachnePawn::OnMove(const FInputActionValue& Value) { const FVector2D V = Value.Get<FVector2D>(); SetMovementInput(V.X, V.Y); }
void AArachnePawn::OnMoveStop(const FInputActionValue&) { MoveInput = FVector2D::ZeroVector; }
void AArachnePawn::OnLook(const FInputActionValue& Value)
{
    const FVector2D V = Value.Get<FVector2D>();
    ViewHeading = FQuat(SurfaceUp, FMath::DegreesToRadians(V.X * 2.2 * MouseSensitivity)).RotateVector(ViewHeading);
    CameraPitch = FMath::Clamp(CameraPitch + static_cast<float>(V.Y) * 1.6f * MouseSensitivity, -78.f, 40.f);
}
void AArachnePawn::OnZoom(const FInputActionValue& Value) { DesiredArmLength = FMath::Clamp(DesiredArmLength - Value.Get<float>() * 45.f, 260.f, 950.f); }
void AArachnePawn::OnSprintOn(const FInputActionValue&) { bSprint = true; }
void AArachnePawn::OnSprintOff(const FInputActionValue&) { bSprint = false; }
void AArachnePawn::OnJump(const FInputActionValue&) { Jump(); }
void AArachnePawn::OnDebug(const FInputActionValue&) { bShowDebug = !bShowDebug; }
void AArachnePawn::OnReset(const FInputActionValue&) { ResetCrawler(SpawnLocation, SpawnRotation); }

void AArachnePawn::SetMovementInput(float Forward, float Right)
{
    MoveInput = FVector2D(Forward, Right).GetClampedToMaxSize(1.0);
}

// =====================================================================================================================
// Public actions
// =====================================================================================================================

void AArachnePawn::ResetCrawler(FVector Location, FRotator Rotation)
{
    SetActorLocationAndRotation(Location, Rotation, false, nullptr, ETeleportType::TeleportPhysics);
    Facing = GetActorForwardVector();
    SurfaceUp = GetActorUpVector();
    ViewHeading = Facing;
    PrevFacing = Facing;
    CameraUp = SurfaceUp;
    SupportNormal = SurfaceUp;
    SupportPoint = Location - SurfaceUp * BodyHeight;
    Velocity = TravelVelocity = PrevTravelVelocity = FVector::ZeroVector;
    Transition = FArachneTransition();
    SetBodySupport(nullptr);
    UnsupportedTime = AttachCooldown = LandingBoost = IdleTime = 0.f;
    BodyOffsetSpring.Reset();
    BodyTiltSpring.Reset();
    AbdomenSpring.Reset();

    FHitResult Ground;
    bAttached = Probe(Location, Location - SurfaceUp * (BodyHeight + AdhesionReach), Ground, 6.f);
    if (bAttached)
    {
        SupportNormal = Ground.ImpactNormal;
        SupportPoint = Ground.ImpactPoint;
        SetBodySupport(Ground.GetComponent());
    }

    const FTransform Base = BaseMeshTransform();
    for (int32 I = 0; I < Legs.Num(); ++I)
    {
        FArachneLeg& L = Legs[I];
        L.Foot = Base.TransformPosition(L.Rest.Last());
        L.bPlanted = L.bSwinging = false;
        L.Swing = 1.f;
        L.Support.Reset();
        L.AirVelocity = FVector::ZeroVector;
        FHitResult Hit;
        if (bAttached && FindFoot(L, L.Foot, Hit))
        {
            L.Target = Hit.ImpactPoint;
            L.TargetNormal = Hit.ImpactNormal;
            L.TargetSupport = Hit.GetComponent();
            L.SwingLength = 0.f;
            PlantLeg(I, false);
        }
    }
    CameraBoom->SetWorldRotation(FRotationMatrix::MakeFromXZ(Facing, SurfaceUp).ToQuat() * FRotator(CameraPitch, 0, 0).Quaternion());
}

void AArachnePawn::Jump()
{
    if (!(bAttached || Transition.bActive) || AttachCooldown > 0.f) return;
    const FVector Up = SurfaceUp;
    Velocity = FVector::VectorPlaneProject(Velocity, Up) + Up * JumpSpeed + WishDirection() * JumpForwardBoost;
    Detach();
    AttachCooldown = .22f;
    BodyOffsetSpring.V.Z -= 160.0;            // compress, the spring throws the body up with the jump
    for (FArachneLeg& L : Legs) { L.bPlanted = L.bSwinging = false; L.AirVelocity = -Velocity * .15; }
    OnJumped.Broadcast();
}

void AArachnePawn::AddImpulse(FVector Impulse)
{
    const FVector Local = GetActorTransform().InverseTransformVectorNoScale(Impulse);
    BodyOffsetSpring.V += Local * .35;
    if (!bAttached && !Transition.bActive) { Velocity += Impulse; return; }
    if (FVector::DotProduct(Impulse, SurfaceUp) > 250.0)
    {
        Velocity = FVector::VectorPlaneProject(Velocity, SurfaceUp) + Impulse;
        Detach();
        AttachCooldown = .15f;
        for (FArachneLeg& L : Legs) { L.bPlanted = L.bSwinging = false; L.AirVelocity = FVector::ZeroVector; }
        return;
    }
    Velocity += FVector::VectorPlaneProject(Impulse, SurfaceUp);
}

// =====================================================================================================================
// Locomotion
// =====================================================================================================================

bool AArachnePawn::Probe(const FVector& Start, const FVector& End, FHitResult& Hit, float Radius) const
{
    FCollisionQueryParams Params(SCENE_QUERY_STAT(ArachneSurface), false, this);
    const bool bFound = Radius > 0.f
        ? GetWorld()->SweepSingleByChannel(Hit, Start, End, FQuat::Identity, ECC_Visibility, FCollisionShape::MakeSphere(Radius), Params)
        : GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params);
    return bFound && !Hit.bStartPenetrating && Hit.GetComponent() && Hit.ImpactNormal.IsNormalized();
}

void AArachnePawn::SetBodySupport(UPrimitiveComponent* Component)
{
    if (BodySupport.Get() == Component) return;
    BodySupport = Component;
    LastSupportTransform = Component ? Component->GetComponentTransform() : FTransform::Identity;
}

void AArachnePawn::FollowSupport()
{
    UPrimitiveComponent* Support = BodySupport.Get();
    if (!Support) return;
    const FTransform Now = Support->GetComponentTransform();
    if (Now.Equals(LastSupportTransform, 1e-4)) return;
    const FVector P = GetActorLocation();
    const FVector Carried = Now.TransformPosition(LastSupportTransform.InverseTransformPosition(P));
    const FQuat DeltaRot = Now.GetRotation() * LastSupportTransform.GetRotation().Inverse();
    LastSupportTransform = Now;
    if (FVector::DistSquared(Carried, P) > FMath::Square(300.0)) return;
    SetActorLocation(Carried, false, nullptr, ETeleportType::None);
    RotateFrame(DeltaRot, true);
}

void AArachnePawn::RotateFrame(const FQuat& Delta, bool bRotateVelocity)
{
    SurfaceUp = Delta.RotateVector(SurfaceUp).GetSafeNormal();
    Facing = PlaneDir(Delta.RotateVector(Facing), SurfaceUp, Facing);
    ViewHeading = PlaneDir(Delta.RotateVector(ViewHeading), SurfaceUp, Facing);
    if (bRotateVelocity) Velocity = Delta.RotateVector(Velocity);
}

void AArachnePawn::ApplyFrame()
{
    Facing = PlaneDir(Facing, SurfaceUp, FVector::CrossProduct(GetActorRightVector(), SurfaceUp));
    SetActorRotation(FRotationMatrix::MakeFromZX(SurfaceUp, Facing).ToQuat());
}

FVector AArachnePawn::WishDirection() const
{
    const FVector Fwd = PlaneDir(ViewHeading, SurfaceUp, Facing);
    const FVector Right = FVector::CrossProduct(SurfaceUp, Fwd);
    return (Fwd * MoveInput.X + Right * MoveInput.Y).GetClampedToMaxSize(1.0);
}

bool AArachnePawn::MoveBody(const FVector& Delta, FHitResult& Block)
{
    if (Delta.IsNearlyZero()) return true;
    SetActorLocation(GetActorLocation() + Delta, true, &Block);
    if (!Block.bBlockingHit) return true;
    const FVector Remaining = FVector::VectorPlaneProject(Delta * (1.0 - Block.Time), Block.Normal);
    if (!Remaining.IsNearlyZero())
    {
        FHitResult Second;
        SetActorLocation(GetActorLocation() + Remaining, true, &Second);
    }
    return false;
}

bool AArachnePawn::IsStepUp(const FHitResult& Wall) const
{
    const double Height = FVector::DotProduct(Wall.ImpactPoint - SupportPoint, SurfaceUp);
    const FVector Start = Wall.ImpactPoint - Wall.ImpactNormal * 18.0 + SurfaceUp * (MaxStepUp - Height + 2.0);
    FHitResult Top;
    return Probe(Start, Start - SurfaceUp * (MaxStepUp + 4.0), Top) && FVector::DotProduct(Top.ImpactNormal, SurfaceUp) > .8;
}

bool AArachnePawn::FindConvexEdge(const FVector& Direction, FHitResult& Face, FVector& Edge) const
{
    const FVector Up = SurfaceUp;
    const FVector Dir = FVector::VectorPlaneProject(Direction, Up).GetSafeNormal();
    if (Dir.IsNearlyZero()) return false;
    const FVector C = GetActorLocation();
    for (const double Depth : {18.0, 50.0, 95.0})
    {
        const FVector Start = C + Dir * 30.0 - Up * (BodyHeight + Depth);
        if (!Probe(Start, Start - Dir * (30.0 + EdgeWrapReach), Face, 4.f)) continue;
        if (FVector::DotProduct(Face.ImpactNormal, Dir) < .3 || FVector::DotProduct(Face.ImpactNormal, Up) > .8) continue;
        const FVector N0 = SupportNormal, N1 = Face.ImpactNormal;
        const double Cos = FVector::DotProduct(N0, N1);
        if (FMath::Abs(Cos) > .97) continue;
        const double D0 = FVector::DotProduct(C - SupportPoint, N0);
        const double D1 = FVector::DotProduct(C - Face.ImpactPoint, N1);
        const double Den = 1.0 - Cos * Cos;
        Edge = C - N0 * ((D0 - Cos * D1) / Den) - N1 * ((D1 - Cos * D0) / Den);
        return FVector::Dist(Edge, C) < BodyHeight * 1.8;
    }
    return false;
}

void AArachnePawn::BeginTransition(const FVector& Pivot, const FVector& NewUp, const FVector& Forward, bool bConvex, UPrimitiveComponent* NewSupport)
{
    const FVector Up0 = SurfaceUp;
    const FVector Up1 = NewUp.GetSafeNormal();
    const double Angle = FMath::Acos(FMath::Clamp(FVector::DotProduct(Up0, Up1), -1.0, 1.0));
    if (Angle < FMath::DegreesToRadians(5.0)) return;
    FVector Axis = FVector::CrossProduct(Up0, Up1);
    if (Axis.SizeSquared() < 1e-6) Axis = FVector::CrossProduct(Up0, Forward);
    Axis.Normalize();

    FArachneTransition& T = Transition;
    T = FArachneTransition();
    T.bActive = true;
    T.bConvex = bConvex;
    T.Pivot = Pivot;
    T.Offset0 = GetActorLocation() - Pivot;
    T.Radius = T.Offset0.Size();
    T.Up0 = Up0;
    T.Up1 = Up1;
    T.Axis = Axis;
    T.Angle = Angle;
    T.Forward0 = PlaneDir(Forward, Up0, Facing);
    T.Speed = FMath::Max(FVector::DotProduct(Velocity, T.Forward0), static_cast<double>(MoveSpeed) * .35);
    T.Support = NewSupport;
    SetBodySupport(nullptr);
}

void AArachnePawn::Land(const FHitResult& Hit)
{
    const FVector N = Hit.ImpactNormal;
    const double Impact = FMath::Max(0.0, -FVector::DotProduct(Velocity, N));
    bAttached = true;
    UnsupportedTime = 0.f;
    SupportNormal = N;
    SupportPoint = Hit.ImpactPoint;
    SetBodySupport(Hit.GetComponent());
    Velocity = FVector::VectorPlaneProject(Velocity, N) * .45;
    LandingBoost = .4f;
    BodyOffsetSpring.V.Z -= FMath::Min(Impact * .3, 420.0);
    OnLanded.Broadcast(static_cast<float>(Impact));
}

void AArachnePawn::Detach()
{
    bAttached = false;
    Transition.bActive = false;
    UnsupportedTime = 0.f;
    SetBodySupport(nullptr);
}

void AArachnePawn::SimulateStep(float Dt)
{
    AttachCooldown = FMath::Max(0.f, AttachCooldown - Dt);
    LandingBoost = FMath::Max(0.f, LandingBoost - Dt);
    if (Transition.bActive) StepTransition(Dt);
    else if (bAttached) StepAttached(Dt);
    else StepAir(Dt);
    if (GetActorLocation().Z < -8000.0) ResetCrawler(SpawnLocation, SpawnRotation);
}

void AArachnePawn::StepAttached(float Dt)
{
    FollowSupport();

    const double MaxSpeed = MoveSpeed * (bSprint ? SprintMultiplier : 1.f);
    const FVector Wish = WishDirection();
    const FVector Up = SurfaceUp;
    const bool bWants = !Wish.IsNearlyZero();

    Velocity = FVector::VectorPlaneProject(Velocity, Up);
    Velocity += (Wish * MaxSpeed - Velocity) * Damp(bWants ? Acceleration : Deceleration, Dt);

    // Body faces the camera heading while travelling; strafing is a crab walk.
    if (bWants)
    {
        const FVector ViewFwd = PlaneDir(ViewHeading, Up, Facing);
        const double Yaw = SignedAngleAround(Facing, ViewFwd, Up);
        Facing = FQuat(Up, Yaw * Damp(FacingTurnSpeed, Dt)).RotateVector(Facing);
    }

    const FVector C = GetActorLocation();
    const double Speed = Velocity.Size();
    const FVector MoveDir = Speed > 5.0 ? Velocity / Speed : Wish.GetSafeNormal();
    const double SteepCos = FMath::Cos(FMath::DegreesToRadians(TransitionMinAngle));

    // 1. Inner corner ahead: roll up onto it along an arc that starts CornerLead before contact.
    if (bWants && !MoveDir.IsNearlyZero())
    {
        FHitResult Wall;
        if (Probe(C, C + MoveDir * (BodyHeight + CornerLead + Speed * Dt), Wall, 8.f))
        {
            const double Cos = FVector::DotProduct(Wall.ImpactNormal, Up);
            const double Dist = FVector::DotProduct(C - Wall.ImpactPoint, Wall.ImpactNormal);
            if (Cos < SteepCos && FVector::DotProduct(Wall.ImpactNormal, MoveDir) < -.2 && Dist <= BodyHeight + CornerLead && !IsStepUp(Wall))
            {
                const double Rho = FMath::Max(10.0, (Dist - BodyHeight) / FMath::Max(1.0 - Cos, .2));
                BeginTransition(C + Up * Rho, Wall.ImpactNormal, MoveDir, false, Wall.GetComponent());
                if (Transition.bActive) return;
            }
        }
    }

    // 2. Sense the surface: straight down, then along the last known normal (fresh landings on walls).
    FHitResult Down;
    bool bDown = Probe(C + Up * 5.0, C - Up * (BodyHeight + AdhesionReach), Down, 6.f);
    if (!bDown && !SupportNormal.Equals(Up, .02))
        bDown = Probe(C + SupportNormal * 5.0, C - SupportNormal * (BodyHeight + AdhesionReach), Down, 6.f);

    FVector NormalSum = FVector::ZeroVector;
    double HeightError = 0.0;
    if (bDown)
    {
        NormalSum += Down.ImpactNormal;
        const double D = FVector::DotProduct(C - Down.ImpactPoint, Down.ImpactNormal);
        HeightError = BodyHeight - D;
        SupportNormal = Down.ImpactNormal;
        SupportPoint = Down.ImpactPoint;
        SetBodySupport(Down.GetComponent());
        UnsupportedTime = 0.f;

        // Look ahead for slopes and small steps so the body starts adapting before it arrives.
        if (!MoveDir.IsNearlyZero() && Speed > 5.0)
        {
            const FVector A = C + MoveDir * (Collision->GetScaledSphereRadius() + 25.0);
            FHitResult Ahead;
            if (Probe(A + Up * 5.0, A - Up * (BodyHeight + AdhesionReach), Ahead, 6.f) && FVector::DotProduct(Ahead.ImpactNormal, Up) > .55)
            {
                NormalSum += Ahead.ImpactNormal * .6;
                const double DA = FVector::DotProduct(C - Ahead.ImpactPoint, Up);
                if (DA < D - 4.0 && D - DA < MaxStepUp) HeightError = FMath::Max(HeightError, BodyHeight - DA);
            }
        }
        // A ring of diagonal probes averages out small bumps and seams.
        const FVector Right = FVector::CrossProduct(Up, Facing);
        const FVector Ring[4] = {Facing, -Facing, Right, -Right};
        for (const FVector& R : Ring)
        {
            FHitResult H;
            const FVector Dir = (R - Up).GetSafeNormal();
            if (Probe(C, C + Dir * (BodyHeight * 1.42 + AdhesionReach), H) && FVector::DotProduct(H.ImpactNormal, Up) > .5)
                NormalSum += H.ImpactNormal * .25;
        }
    }
    else
    {
        UnsupportedTime += Dt;
        // 3. Convex edge: nothing below, so wrap around the edge towards the face underneath.
        const FVector EdgeDir = !MoveDir.IsNearlyZero() ? MoveDir : Facing;
        FHitResult Face;
        FVector Edge;
        if (bWants && FindConvexEdge(EdgeDir, Face, Edge))
        {
            BeginTransition(Edge, Face.ImpactNormal, EdgeDir, true, Face.GetComponent());
            if (Transition.bActive) return;
        }
        if (UnsupportedTime > CoyoteTime) { Detach(); return; }
    }

    if (!NormalSum.IsNearlyZero())
    {
        const FVector TargetUp = NormalSum.GetSafeNormal();
        const double Rate = SurfaceTurnSpeed * (LandingBoost > 0.f ? 3.0 : 1.0);
        RotateFrame(FQuat::Slerp(FQuat::Identity, FQuat::FindBetweenNormals(SurfaceUp, TargetUp), Damp(Rate, Dt)), true);
    }

    FVector Delta = Velocity * Dt;
    if (bDown) Delta += SurfaceUp * (HeightError * Damp(HoverStiffness * (LandingBoost > 0.f ? 1.8 : 1.0), Dt));
    FHitResult Block;
    if (!MoveBody(Delta, Block) && Block.bBlockingHit && bWants)
    {
        // Blocked by something the probes missed (low wall, lip of a box): climb it.
        const double Cos = FVector::DotProduct(Block.ImpactNormal, SurfaceUp);
        if (Cos < SteepCos && FVector::DotProduct(Block.ImpactNormal, MoveDir) < -.1)
        {
            const FVector P = GetActorLocation();
            const double Dist = FVector::DotProduct(P - Block.ImpactPoint, Block.ImpactNormal);
            BeginTransition(P + SurfaceUp * FMath::Max(10.0, (Dist - BodyHeight) / FMath::Max(1.0 - Cos, .2)), Block.ImpactNormal, MoveDir, false, Block.GetComponent());
        }
    }
    ApplyFrame();
}

void AArachnePawn::StepTransition(float Dt)
{
    FArachneTransition& T = Transition;
    const double MaxSpeed = MoveSpeed * (bSprint ? SprintMultiplier : 1.f);
    const FVector Wish = WishDirection();
    const FQuat QNow(T.Axis, T.Alpha * T.Angle);
    const FVector Fwd = QNow.RotateVector(T.Forward0);

    // Input along the arc drives it (and can reverse it); sideways input slides along the edge line.
    const double Want = FVector::DotProduct(Wish, Fwd) * MaxSpeed;
    T.Speed += (Want - T.Speed) * Damp(Wish.IsNearlyZero() ? Deceleration : Acceleration, Dt);
    const double Lateral = FVector::DotProduct(Wish, T.Axis) * MaxSpeed * .6;
    T.Pivot += T.Axis * (Lateral * Dt);

    const double ArcLength = FMath::Max(T.Radius, 25.0) * T.Angle;
    const double Raw = T.Alpha + T.Speed * Dt / ArcLength;
    const double Alpha = FMath::Clamp(Raw, 0.0, 1.0);
    const FQuat QNew(T.Axis, Alpha * T.Angle);
    const FVector Goal = T.Pivot + QNew.RotateVector(T.Offset0);

    FHitResult Block;
    SetActorLocation(Goal, true, &Block);
    if (Block.bBlockingHit && FVector::DistSquared(GetActorLocation(), Goal) > FMath::Square(14.0))
    {
        // Something is in the way of the arc: fall back to regular surface sensing.
        T.bActive = false;
        bAttached = true;
        ApplyFrame();
        return;
    }
    RotateFrame(QNew * QNow.Inverse(), false);
    T.Alpha = Alpha;
    Velocity = QNew.RotateVector(T.Forward0) * T.Speed + T.Axis * Lateral;

    if (Raw >= 1.0)
    {
        T.bActive = false;
        bAttached = true;
        SurfaceUp = T.Up1;
        SupportNormal = T.Up1;
        SetBodySupport(T.Support.Get());
        UnsupportedTime = 0.f;
    }
    else if (Raw <= 0.0 && T.Speed < 0.0)
    {
        T.bActive = false;
        bAttached = true;
        SurfaceUp = T.Up0;
    }
    ApplyFrame();
}

void AArachnePawn::StepAir(float Dt)
{
    const FVector C = GetActorLocation();
    Velocity += FVector(0, 0, GetWorld()->GetGravityZ() * GravityScale) * Dt;
    Velocity += WishDirection() * (MoveSpeed * AirControl * Dt);
    Velocity = Velocity.GetClampedToMaxSize(4000.0);

    // Reach for whatever we are about to hit; otherwise slowly right ourselves.
    FVector TargetUp = FVector::UpVector;
    FHitResult Grab;
    const double Speed = Velocity.Size();
    if (Speed > 50.0 && Probe(C, C + Velocity / Speed * AirGrabReach, Grab, 12.f)) TargetUp = Grab.ImpactNormal;
    else if (Probe(C, C - SurfaceUp * AirGrabReach, Grab, 12.f)) TargetUp = Grab.ImpactNormal;
    RotateFrame(FQuat::Slerp(FQuat::Identity, FQuat::FindBetweenNormals(SurfaceUp, TargetUp), Damp(5.0, Dt)), false);

    FHitResult Block;
    SetActorLocation(C + Velocity * Dt, true, &Block);
    if (Block.bBlockingHit)
    {
        if (AttachCooldown <= 0.f && Block.GetComponent()) Land(Block);
        else Velocity = FVector::VectorPlaneProject(Velocity, Block.Normal);
    }
    ApplyFrame();
}

void AArachnePawn::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (DeltaSeconds <= 0.f) return;

    const FVector Before = GetActorLocation();
    const float Clamped = FMath::Min(DeltaSeconds, 1.f / 15.f);
    const int32 Steps = FMath::Clamp(FMath::CeilToInt(Clamped * 120.f), 1, 8);
    PrevFacing = Facing;
    for (int32 I = 0; I < Steps; ++I) SimulateStep(Clamped / Steps);

    PrevTravelVelocity = TravelVelocity;
    TravelVelocity = (GetActorLocation() - Before) / DeltaSeconds;
    if (TravelVelocity.SizeSquared() > FMath::Square(4500.0)) TravelVelocity = Velocity;   // teleport / reset
    YawRate = static_cast<float>(SignedAngleAround(PrevFacing, Facing, SurfaceUp) / DeltaSeconds);
    AnimTime += DeltaSeconds;
    GaitPhase += static_cast<float>(Velocity.Size() / FMath::Max(StepDistance, 1.f)) * DeltaSeconds * PI;

    UpdateCamera(DeltaSeconds);
    UpdateLegs(DeltaSeconds);
    UpdateBodyVisual(DeltaSeconds);
    BuildPose(DeltaSeconds);

    if (bShowDebug)
    {
        const FVector P = GetActorLocation();
        DrawDebugDirectionalArrow(GetWorld(), P, P + SurfaceUp * 110.0, 15.f, bAttached ? FColor::Cyan : FColor::Red, false, 0.f, 0, 3.f);
        DrawDebugDirectionalArrow(GetWorld(), P, P + Velocity * .4, 12.f, FColor::Orange, false, 0.f, 0, 2.f);
        if (Transition.bActive)
        {
            DrawDebugSphere(GetWorld(), Transition.Pivot, 8.f, 8, Transition.bConvex ? FColor::Magenta : FColor::Yellow, false, 0.f);
            DrawDebugLine(GetWorld(), Transition.Pivot - Transition.Axis * 120.0, Transition.Pivot + Transition.Axis * 120.0, FColor::Magenta, false, 0.f, 0, 1.5f);
        }
    }
}

// =====================================================================================================================
// Camera
// =====================================================================================================================

void AArachnePawn::UpdateCamera(float Dt)
{
    CameraUp = SlerpNormal(CameraUp, SurfaceUp, Damp(CameraUpFollowSpeed, Dt));
    const FVector Fwd = PlaneDir(ViewHeading, CameraUp, PlaneDir(Facing, CameraUp, FVector::ForwardVector));
    const FQuat Base = FRotationMatrix::MakeFromXZ(Fwd, CameraUp).ToQuat();
    CameraBoom->SetWorldRotation(Base * FRotator(CameraPitch, 0, 0).Quaternion());
    CameraBoom->TargetArmLength = FMath::FInterpTo(CameraBoom->TargetArmLength, DesiredArmLength, Dt, 6.f);
}

// =====================================================================================================================
// Rig
// =====================================================================================================================

FTransform AArachnePawn::BaseMeshTransform() const
{
    return FTransform(FVector(0, 0, -BodyHeight)) * GetActorTransform();
}

void AArachnePawn::InitializeRig()
{
    Legs.Empty();
    USkeletalMesh* Asset = Cast<USkeletalMesh>(SpiderMesh->GetSkinnedAsset());
    if (!Asset) { UE_LOG(LogArachne, Error, TEXT("ARACHNE: skeletal mesh missing")); return; }

    const FReferenceSkeleton& Ref = Asset->GetRefSkeleton();
    ReferenceCS = Ref.GetRefBonePose();
    Children.Empty();
    Children.SetNum(ReferenceCS.Num());
    for (int32 I = 0; I < ReferenceCS.Num(); ++I)
    {
        const int32 Parent = Ref.GetParentIndex(I);
        if (Parent != INDEX_NONE) { ReferenceCS[I] = ReferenceCS[I] * ReferenceCS[Parent]; Children[Parent].Add(I); }
    }

    static const TCHAR* Parts[] = {TEXT("coxa"), TEXT("trochanter"), TEXT("femur"), TEXT("patella"), TEXT("tibia"), TEXT("metatarsus"), TEXT("tarsus")};
    TArray<FArachneLeg> Built;
    for (int32 Pair = 1; Pair <= 4; ++Pair)
    {
        for (int32 Side = 0; Side < 2; ++Side)
        {
            FArachneLeg L;
            L.Pair = Pair;
            L.Side = Side;
            const TCHAR* S = Side == 0 ? TEXT("l") : TEXT("r");
            for (const TCHAR* Part : Parts) L.Bones.Add(Ref.FindBoneIndex(FName(*FString::Printf(TEXT("leg_%02d_%s_%s"), Pair, Part, S))));
            L.Bones.Add(Ref.FindBoneIndex(FName(*FString::Printf(TEXT("foot_%02d_%s"), Pair, S))));
            if (L.Bones.Contains(INDEX_NONE)) { UE_LOG(LogArachne, Error, TEXT("ARACHNE: missing leg bones %d %s"), Pair, S); continue; }
            for (const int32 Bone : L.Bones) L.Rest.Add(ReferenceCS[Bone].GetLocation());
            for (int32 J = 0; J < 7; ++J) L.Lengths.Add(FVector::Distance(L.Rest[J], L.Rest[J + 1]));
            for (int32 J = 2; J < 7; ++J) L.Reach += L.Lengths[J];
            Built.Add(L);
        }
    }
    if (Built.Num() == 8) Legs = Built;

    // Abdomen + everything hanging off it (spinnerets, shields)
    AbdomenBone = Ref.FindBoneIndex(TEXT("abdomen"));
    AbdomenSubtree.Empty();
    if (AbdomenBone != INDEX_NONE)
    {
        TArray<int32> Stack = {AbdomenBone};
        while (Stack.Num()) { const int32 B = Stack.Pop(); AbdomenSubtree.Add(B); Stack.Append(Children[B]); }
    }

    // Pedipalps. The Blender rig placed the right palp joints on the left side, so mirror the left pivots when needed.
    for (int32 K = 0; K < 4; ++K)
    {
        PalpBones[0][K] = Ref.FindBoneIndex(FName(*FString::Printf(TEXT("pedipalp_%02d_l"), K + 1)));
        PalpBones[1][K] = Ref.FindBoneIndex(FName(*FString::Printf(TEXT("pedipalp_%02d_r"), K + 1)));
        for (int32 S = 0; S < 2; ++S)
            PalpPivots[S][K] = PalpBones[S][K] != INDEX_NONE ? ReferenceCS[PalpBones[S][K]].GetLocation() : FVector::ZeroVector;
        if (PalpBones[0][K] != INDEX_NONE && PalpBones[1][K] != INDEX_NONE && PalpPivots[0][K].Y * PalpPivots[1][K].Y > 1.0)
            PalpPivots[1][K] = FVector(PalpPivots[0][K].X, -PalpPivots[0][K].Y, PalpPivots[0][K].Z);
    }
    for (int32 S = 0; S < 2; ++S)
    {
        ChelBones[S] = Ref.FindBoneIndex(S == 0 ? TEXT("chelicera_l") : TEXT("chelicera_r"));
        FangBones[S] = Ref.FindBoneIndex(S == 0 ? TEXT("fang_l") : TEXT("fang_r"));
    }

    UE_LOG(LogArachne, Display, TEXT("ARACHNE: %d IK chains, %d bones, abdomen %d, palps %d/%d"),
        Legs.Num(), ReferenceCS.Num(), AbdomenBone, PalpBones[0][0], PalpBones[1][0]);
}

bool AArachnePawn::FindFoot(const FArachneLeg& Leg, const FVector& Home, FHitResult& Hit) const
{
    const FTransform Base = BaseMeshTransform();
    const FVector Up = SurfaceUp;
    const FVector Hip = Base.TransformPosition(Leg.Rest[2]);
    const double Reach = Leg.Reach * .98;
    auto Reachable = [&](const FHitResult& H) { return FVector::Dist(Hip, H.ImpactPoint) <= Reach; };

    const FVector Above = Home + Up * 55.0;
    const FVector Below = Home - Up * FootProbeReach;
    // 1. anything between the hip and the column above the foot: walls, inner corners, steps up
    if (Probe(Hip, Above, Hit, 2.f) && Reachable(Hit)) return true;
    // 2. straight down onto the surface
    if (Probe(Above, Below, Hit, 2.f) && Reachable(Hit)) return true;
    // 3. convex edge: from beneath the ledge back towards the body
    const FVector BodyUnder = GetActorLocation() - Up * (BodyHeight + FootProbeReach * .5);
    if (Probe(Below, BodyUnder, Hit, 2.f) && Reachable(Hit)) return true;
    // 4. straight out from the hip
    return Probe(Hip, Home + (Home - Hip).GetSafeNormal() * 60.0 - Up * 40.0, Hit, 2.f) && Reachable(Hit);
}

void AArachnePawn::PlantLeg(int32 Index, bool bBroadcast)
{
    FArachneLeg& L = Legs[Index];
    L.bSwinging = false;
    L.bPlanted = true;
    L.Swing = 1.f;
    L.Plant = L.Target;
    L.PlantNormal = L.TargetNormal;
    L.Support = L.TargetSupport;
    if (UPrimitiveComponent* S = L.Support.Get()) L.LocalPlant = S->GetComponentTransform().InverseTransformPosition(L.Plant);
    L.Foot = L.Plant;
    L.PlantedTime = 0.f;
    if (bBroadcast)
    {
        const float Strength = FMath::Clamp(L.SwingLength / FMath::Max(StepDistance, 1.f), .15f, 1.5f);
        BodyOffsetSpring.V.Z -= Strength * 14.0;   // each footfall nudges the body: a subtle, rhythmic bob
        OnFootPlanted.Broadcast(Index, L.Plant, L.PlantNormal, Strength);
    }
}

void AArachnePawn::UpdateLegs(float Dt)
{
    if (!IsRigValid()) return;
    const FTransform Base = BaseMeshTransform();
    const FVector Up = SurfaceUp;
    const FVector C = GetActorLocation();
    const bool bGrounded = bAttached || Transition.bActive;
    const double Speed = Velocity.Size();
    const bool bIdle = Speed < 8.0 && FMath::Abs(YawRate) < .25f && bGrounded;
    IdleTime = bIdle ? IdleTime + Dt : 0.f;
    const double SpeedRatio = FMath::Clamp(Speed / FMath::Max(MoveSpeed, 1.f), 0.0, 2.0);
    const float SwingTime = StepDuration / (1.f + .55f * static_cast<float>(SpeedRatio));
    const double Threshold = StepDistance * (1.0 + .35 * SpeedRatio);
    const bool bSettling = IdleTime > .18f;

    int32 Swinging = 0;
    SupportedFeet = 0;
    for (FArachneLeg& L : Legs)
    {
        if (L.bPlanted)
        {
            if (UPrimitiveComponent* S = L.Support.Get()) L.Plant = S->GetComponentTransform().TransformPosition(L.LocalPlant);
            L.Foot = L.Plant;
            L.PlantedTime += Dt;
            ++SupportedFeet;
        }
        if (L.bSwinging) ++Swinging;
    }

    if (!bGrounded)
    {
        // Airborne: legs paddle and reach, each foot on its own critically damped spring.
        const int32 Sub = FMath::Clamp(FMath::CeilToInt(Dt * 120.f), 1, 8);
        const double H = Dt / Sub;
        for (int32 I = 0; I < Legs.Num(); ++I)
        {
            FArachneLeg& L = Legs[I];
            L.bPlanted = L.bSwinging = false;
            const FVector Home = Base.TransformPosition(L.Rest.Last());
            const FVector Hip = Base.TransformPosition(L.Rest[2]);
            const double Phase = AnimTime * 11.0 + I * 1.7;
            const FVector Goal = FMath::Lerp(Home, Hip, .22) + Up * (FMath::Sin(Phase) * 9.0 + 12.0) + (Home - Hip).GetSafeNormal() * (FMath::Cos(Phase) * 7.0);
            const double W = 2.0 * PI * 5.0;
            for (int32 S = 0; S < Sub; ++S)
            {
                L.AirVelocity += ((Goal - L.Foot) * (W * W) - L.AirVelocity * (2.0 * .75 * W)) * H;
                L.Foot += L.AirVelocity * H;
            }
        }
        return;
    }

    // Where a leg would like to put its foot, ahead of the body by the distance it travels while the foot is in the air.
    auto PredictHome = [&](const FArachneLeg& L, float Remaining)
    {
        const FVector Home = Base.TransformPosition(L.Rest.Last());
        const FVector Dir = Speed > 1.0 ? Velocity / Speed : FVector::ZeroVector;
        const FVector Lead = (Velocity * Remaining + Dir * (Threshold * .5 * StepLead)).GetClampedToMaxSize(Threshold * 1.3);
        const FQuat Turn(Up, YawRate * Remaining * 1.5f);
        return C + Turn.RotateVector(Home - C) + Lead;
    };
    auto NeighbourSwinging = [&](int32 Index)
    {
        const int32 Pair = Index / 2, Side = Index % 2;
        const int32 N[3] = {(Pair - 1) * 2 + Side, (Pair + 1) * 2 + Side, Pair * 2 + (1 - Side)};
        for (const int32 K : N) if (K >= 0 && K < Legs.Num() && Legs[K].bSwinging) return true;
        return false;
    };

    // 1. Which legs want to step, most urgent first.
    struct FCandidate { int32 Index; double Urgency; };
    TArray<FCandidate, TInlineAllocator<8>> Candidates;
    for (int32 I = 0; I < Legs.Num(); ++I)
    {
        const FArachneLeg& L = Legs[I];
        if (L.bSwinging) continue;
        if (!L.bPlanted) { Candidates.Add({I, 100.0}); continue; }
        const FVector Home = Base.TransformPosition(L.Rest.Last());
        const FVector Hip = Base.TransformPosition(L.Rest[2]);
        const FVector Offset = L.Plant - Home;
        double Err = FMath::Max(FVector::VectorPlaneProject(Offset, Up).Size(), FMath::Abs(FVector::DotProduct(Offset, Up)) * .8);
        const bool bOverReach = FVector::Dist(Hip, L.Plant) > L.Reach * .97;
        if (!L.Support.IsValid()) Err = Threshold * 3.0;
        const double Limit = bSettling ? IdleSettleDistance : Threshold;
        if (Err > Limit || bOverReach) Candidates.Add({I, Err / Threshold + (bOverReach ? 10.0 : 0.0)});
    }
    Candidates.Sort([](const FCandidate& A, const FCandidate& B) { return A.Urgency > B.Urgency; });

    for (const FCandidate& Cand : Candidates)
    {
        if (Swinging >= MaxSwingingLegs) break;
        const bool bUrgent = Cand.Urgency > 2.2;
        if (bSettling && Swinging > 0 && !bUrgent) break;       // settle shuffle: one leg at a time
        if (!bUrgent && NeighbourSwinging(Cand.Index)) continue; // wave gait: neighbours never lift together
        FArachneLeg& L = Legs[Cand.Index];
        FHitResult Hit;
        if (!FindFoot(L, PredictHome(L, SwingTime), Hit))
        {
            // Nothing reachable to stand on: let a loose foot hang towards its rest pose instead of freezing.
            if (!L.bPlanted) L.Foot = FMath::Lerp(L.Foot, Base.TransformPosition(L.Rest.Last()) + Up * 8.0, Damp(8.0, Dt));
            continue;
        }
        const bool bWasPlanted = L.bPlanted;
        L.Start = L.Foot;
        L.StartNormal = bWasPlanted ? L.PlantNormal : Up;
        L.Target = Hit.ImpactPoint;
        L.TargetNormal = Hit.ImpactNormal;
        L.TargetSupport = Hit.GetComponent();
        if (UPrimitiveComponent* S = L.TargetSupport.Get()) L.LocalTarget = S->GetComponentTransform().InverseTransformPosition(L.Target);
        L.SwingLength = static_cast<float>(FVector::Dist(L.Start, L.Target));
        if (L.SwingLength < 1.5f) { PlantLeg(Cand.Index, false); continue; }
        L.bPlanted = false;
        L.bSwinging = true;
        L.Swing = 0.f;
        L.SwingDuration = bWasPlanted ? SwingTime : SwingTime * .75f;
        L.SwingHeight = StepHeight * FMath::Clamp(L.SwingLength / FMath::Max(StepDistance, 1.f), .45f, 1.35f);
        ++Swinging;
    }

    // 2. Advance swinging feet along a lifted arc; keep re-aiming during the first half of the swing.
    for (int32 I = 0; I < Legs.Num(); ++I)
    {
        FArachneLeg& L = Legs[I];
        if (!L.bSwinging) continue;
        L.Swing = FMath::Min(1.f, L.Swing + Dt / FMath::Max(L.SwingDuration, .05f));
        const float T = L.Swing;
        FHitResult Hit;
        if (T < .55f && (Speed > 10.0 || FMath::Abs(YawRate) > .2f) && FindFoot(L, PredictHome(L, L.SwingDuration * (1.f - T)), Hit))
        {
            L.Target = FMath::Lerp(L.Target, Hit.ImpactPoint, .35);
            L.TargetNormal = Hit.ImpactNormal;
            L.TargetSupport = Hit.GetComponent();
            if (UPrimitiveComponent* S = L.TargetSupport.Get()) L.LocalTarget = S->GetComponentTransform().InverseTransformPosition(L.Target);
        }
        else if (UPrimitiveComponent* S = L.TargetSupport.Get())
        {
            L.Target = S->GetComponentTransform().TransformPosition(L.LocalTarget);
        }
        const float Ease = T * T * (3.f - 2.f * T);
        const float Arc = FMath::Sin(PI * T) * (1.f + .3f * (1.f - T));   // lifts briskly, plants decisively
        const FVector Lift = (L.StartNormal + L.TargetNormal + Up * 1.5).GetSafeNormal();
        L.Foot = FMath::Lerp(L.Start, L.Target, static_cast<double>(Ease)) + Lift * (L.SwingHeight * Arc);
        if (T >= 1.f) PlantLeg(I, true);
    }
}

void AArachnePawn::UpdateBodyVisual(float Dt)
{
    if (!IsRigValid())
    {
        SpiderMesh->SetRelativeLocationAndRotation(FVector(0, 0, -BodyHeight), FQuat::Identity);
        return;
    }
    const FTransform Base = BaseMeshTransform();

    // Fit a plane through the feet (relative to their rest heights): body pitches/rolls with the terrain.
    double Front = 0, Back = 0, Left = 0, Right = 0, Height = 0, Lift = 0;
    int32 NF = 0, NB = 0, NL = 0, NR = 0;
    for (const FArachneLeg& L : Legs)
    {
        const double Z = Base.InverseTransformPosition(L.Foot).Z - L.Rest.Last().Z;
        Height += Z;
        if (L.Pair <= 2) { Front += Z; ++NF; } else { Back += Z; ++NB; }
        if (L.Side == 0) { Left += Z; ++NL; } else { Right += Z; ++NR; }
        if (L.bSwinging) Lift += FMath::Sin(PI * L.Swing);
    }
    Height /= Legs.Num();
    const double SlopeX = (Front / FMath::Max(NF, 1) - Back / FMath::Max(NB, 1)) / 170.0;   // + = front higher
    const double SlopeY = (Right / FMath::Max(NR, 1) - Left / FMath::Max(NL, 1)) / 200.0;   // + = right higher

    // Inertia: acceleration in the body frame (lags the body and tips it).
    const FVector Accel = GetActorTransform().InverseTransformVectorNoScale((TravelVelocity - PrevTravelVelocity) / FMath::Max(Dt, 1e-3f)).GetClampedToMaxSize(4000.0);
    const bool bGrounded = bAttached || Transition.bActive;
    const double Breath = FMath::Sin(AnimTime * 2.1) * .8;

    FVector OffsetTarget(
        FMath::Clamp(-Accel.X * .008 * LeanAmount, -14.0, 14.0),
        FMath::Clamp(-Accel.Y * .008 * LeanAmount, -14.0, 14.0),
        FMath::Clamp(Height * .5, -30.0, 30.0) + Lift * 1.4 + Breath);
    FVector TiltTarget(
        FMath::Clamp(-SlopeX * .75 - Accel.X * .00011 * LeanAmount, -.45, .45),
        FMath::Clamp(-SlopeY * .75 - Accel.Y * .00011 * LeanAmount, -.45, .45),
        0.0);
    if (!bGrounded) { OffsetTarget = FVector(0, 0, 6.0); TiltTarget = FVector(-TravelVelocity.Z * .00012, 0, 0).GetClampedToMaxSize(.3); }

    // Abdomen: heavy, loosely coupled mass (deg): roll, pitch, yaw
    const double SpeedRatio = FMath::Clamp(Velocity.Size() / FMath::Max(MoveSpeed, 1.f), 0.0, 2.0);
    const FVector AbdomenTarget(
        FMath::Clamp(Accel.Y * .004, -8.0, 8.0) * AbdomenJiggle,
        (FMath::Clamp(Accel.X * .006, -12.0, 12.0) + FMath::Sin(GaitPhase * 2.0) * 1.6 * SpeedRatio + FMath::Sin(AnimTime * 1.6) * 1.2) * AbdomenJiggle,
        FMath::Clamp(-YawRate * 9.0 - Accel.Y * .003, -16.0, 16.0) * AbdomenJiggle);

    const int32 Sub = FMath::Clamp(FMath::CeilToInt(Dt * 120.f), 1, 8);
    const double H = Dt / Sub;
    for (int32 S = 0; S < Sub; ++S)
    {
        BodyOffsetSpring.Step(OffsetTarget, BodySpringFrequency, BodySpringDamping, H);
        BodyTiltSpring.Step(TiltTarget, BodySpringFrequency * .8, BodySpringDamping, H);
        AbdomenSpring.Step(AbdomenTarget, 2.1, .32, H);
    }
    BodyOffsetSpring.X = BodyOffsetSpring.X.BoundToBox(FVector(-25, -25, -40), FVector(25, 25, 40));

    const FVector N = FVector(BodyTiltSpring.X.X, BodyTiltSpring.X.Y, 1.0).GetSafeNormal();
    const FQuat Tilt = FQuat::FindBetweenNormals(FVector::UpVector, N);
    SpiderMesh->SetRelativeLocationAndRotation(Tilt.RotateVector(FVector(0, 0, -BodyHeight)) + BodyOffsetSpring.X, Tilt);
}

void AArachnePawn::SolveLeg(const FArachneLeg& L, const FVector& FootWorld, TArray<FTransform>& Pose)
{
    const FTransform MeshT = SpiderMesh->GetComponentTransform();
    const FVector Target = MeshT.InverseTransformPosition(FootWorld);
    const FVector Z = FVector::UpVector;
    TArray<FVector, TInlineAllocator<8>> P;
    P.Append(L.Rest);

    // 1. Yaw: the coxa takes a third, the trochanter the rest, so the leg's bend plane faces the foot.
    const double TotalYaw = FMath::Clamp(SignedAngleAround(L.Rest[7] - L.Rest[1], Target - L.Rest[1], Z), -1.2, 1.2);
    const FQuat CoxaYaw(Z, TotalYaw * .35);
    for (int32 J = 1; J < 8; ++J) P[J] = L.Rest[0] + CoxaYaw.RotateVector(L.Rest[J] - L.Rest[0]);
    const double TrochAngle = FMath::Clamp(SignedAngleAround(P[7] - P[1], Target - P[1], Z), -1.2, 1.2);
    const FQuat TrochYaw(Z, TrochAngle);
    for (int32 J = 2; J < 8; ++J) P[J] = P[1] + TrochYaw.RotateVector(P[J] - P[1]);
    const FQuat LegYaw = TrochYaw * CoxaYaw;

    // 2. Planar FABRIK femur -> claw. The bend plane contains the knee (patella), so knees always stay high.
    const FVector Root = P[2];
    FVector Goal = Target;
    if (FVector::Dist(Root, Goal) > L.Reach * .999) Goal = Root + (Goal - Root).GetSafeNormal() * (L.Reach * .999);
    const FVector PlaneN = FVector::CrossProduct(Goal - Root, P[4] - Root).GetSafeNormal();
    const bool bPlane = !PlaneN.IsNearlyZero();
    for (int32 Iter = 0; Iter < 16; ++Iter)
    {
        P[7] = Goal;
        for (int32 J = 6; J >= 2; --J) P[J] = P[J + 1] + (P[J] - P[J + 1]).GetSafeNormal() * L.Lengths[J];
        if (bPlane) for (int32 J = 3; J < 7; ++J) P[J] = FVector::PointPlaneProject(P[J], Root, PlaneN);
        P[2] = Root;
        for (int32 J = 2; J < 7; ++J) P[J + 1] = P[J] + (P[J + 1] - P[J]).GetSafeNormal() * L.Lengths[J];
        if (FVector::DistSquared(P[7], Goal) < .04) break;
    }
    MaxFootError = FMath::Max(MaxFootError, static_cast<float>(FVector::Dist(P[7], Target)));

    // 3. Bone rotations = rest rotation, then yaw, then the swing that lines the segment up with the solved chain.
    for (int32 J = 0; J < 7; ++J)
    {
        const int32 Bone = L.Bones[J];
        const FQuat Yaw = J == 0 ? CoxaYaw : LegYaw;
        const FVector From = Yaw.RotateVector(L.Rest[J + 1] - L.Rest[J]).GetSafeNormal();
        const FVector To = (P[J + 1] - P[J]).GetSafeNormal();
        const FQuat Rot = (FQuat::FindBetweenNormals(From, To) * Yaw * ReferenceCS[Bone].GetRotation()).GetNormalized();
        Pose[Bone] = FTransform(Rot, P[J], ReferenceCS[Bone].GetScale3D());
    }
    const int32 Tarsus = L.Bones[6], Claw = L.Bones[7];
    const FQuat TarsusDelta = Pose[Tarsus].GetRotation() * ReferenceCS[Tarsus].GetRotation().Inverse();
    Pose[Claw] = FTransform((TarsusDelta * ReferenceCS[Claw].GetRotation()).GetNormalized(), P[7], ReferenceCS[Claw].GetScale3D());

    if (bShowDebug)
    {
        for (int32 J = 0; J < 7; ++J)
            DrawDebugLine(GetWorld(), MeshT.TransformPosition(P[J]), MeshT.TransformPosition(P[J + 1]), FColor::Yellow, false, 0.f, 0, 1.5f);
        DrawDebugSphere(GetWorld(), FootWorld, 4.f, 8, L.bPlanted ? FColor::Green : FColor::Orange, false, 0.f);
        if (L.bSwinging) DrawDebugSphere(GetWorld(), L.Target, 3.f, 6, FColor::Blue, false, 0.f);
    }
}

void AArachnePawn::AnimateExtras(float Dt, TArray<FTransform>& Pose)
{
    // Abdomen: spring driven sway about the pedicel.
    if (AbdomenBone != INDEX_NONE)
    {
        const FVector A = AbdomenSpring.X;
        const FQuat Q = FRotator(A.Y, A.Z, A.X).Quaternion();
        const FTransform Delta = RotateAbout(ReferenceCS[AbdomenBone].GetLocation(), Q);
        for (const int32 B : AbdomenSubtree) Pose[B] = Pose[B] * Delta;
    }

    // Pedipalps: they feel the ground ahead in counter-phase while walking and fidget while idle.
    const double SpeedRatio = FMath::Clamp(Velocity.Size() / FMath::Max(MoveSpeed, 1.f), 0.0, 1.5);
    static const double Share[4] = {.45, .8, .6, .35};
    for (int32 S = 0; S < 2; ++S)
    {
        const double Side = S == 0 ? 1.0 : -1.0;
        const double Walk = FMath::Sin(GaitPhase + S * PI) * 13.0 * SpeedRatio;
        const double Idle = (FMath::Sin(AnimTime * 1.3 + S * 2.1) * 4.0 + FMath::Sin(AnimTime * 3.7 + S) * 1.8) * (1.0 - FMath::Min(SpeedRatio, 1.0));
        const double Pitch = FMath::DegreesToRadians((Walk + Idle) * PedipalpMotion);
        const double Yaw = FMath::DegreesToRadians(FMath::Sin(AnimTime * .9 + S * 1.3) * 5.0 * PedipalpMotion) * Side;
        FTransform Acc = FTransform::Identity;
        for (int32 K = 0; K < 4; ++K)
        {
            const int32 Bone = PalpBones[S][K];
            if (Bone == INDEX_NONE) break;
            const FVector Pivot = Acc.TransformPosition(PalpPivots[S][K]);
            const FQuat Q = FQuat(Acc.GetRotation().RotateVector(FVector::YAxisVector), Pitch * Share[K])
                          * FQuat(Acc.GetRotation().RotateVector(FVector::ZAxisVector), Yaw * Share[K]);
            Acc = Acc * RotateAbout(Pivot, Q);
            Pose[Bone] = ReferenceCS[Bone] * Acc;
        }
    }

    // Chelicerae + fangs: slow idle flex.
    for (int32 S = 0; S < 2; ++S)
    {
        if (ChelBones[S] == INDEX_NONE) continue;
        const FQuat QC(FVector::YAxisVector, FMath::DegreesToRadians(FMath::Sin(AnimTime * .7 + S * .4) * 2.5));
        const FTransform DC = RotateAbout(ReferenceCS[ChelBones[S]].GetLocation(), QC);
        Pose[ChelBones[S]] = ReferenceCS[ChelBones[S]] * DC;
        if (FangBones[S] != INDEX_NONE)
        {
            const FVector FangPivot = DC.TransformPosition(ReferenceCS[FangBones[S]].GetLocation());
            const FQuat QF(DC.GetRotation().RotateVector(FVector::YAxisVector), FMath::DegreesToRadians((FMath::Sin(AnimTime * 1.1 + S) * .5 + .5) * 6.0));
            Pose[FangBones[S]] = ReferenceCS[FangBones[S]] * DC * RotateAbout(FangPivot, QF);
        }
    }
}

void AArachnePawn::BuildPose(float Dt)
{
    if (!IsRigValid() || !SpiderMesh->GetSkinnedAsset()) return;
    TArray<FTransform> Pose = ReferenceCS;
    MaxFootError = 0.f;
    for (const FArachneLeg& L : Legs) SolveLeg(L, L.Foot, Pose);
    AnimateExtras(Dt, Pose);

    const FReferenceSkeleton& Ref = SpiderMesh->GetSkinnedAsset()->GetRefSkeleton();
    SpiderMesh->BoneSpaceTransforms.SetNum(Pose.Num());
    for (int32 I = 0; I < Pose.Num(); ++I)
    {
        const int32 Parent = Ref.GetParentIndex(I);
        SpiderMesh->BoneSpaceTransforms[I] = Parent == INDEX_NONE ? Pose[I] : Pose[I].GetRelativeTransform(Pose[Parent]);
    }
    SpiderMesh->MarkRefreshTransformDirty();
    SpiderMesh->RefreshBoneTransforms();
}
