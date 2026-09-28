#include "Creature/ArachnePawn.h"
#include "Creature/ArachneMath.h"
#include "AI/ArachneSensesComponent.h"
#include "AI/ArachneMemoryComponent.h"
#include "AI/ArachneNavigatorComponent.h"
#include "AI/ArachneBrainComponent.h"
#include "Components/SphereComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "DrawDebugHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogArachne, Log, All);

using namespace ArachneMath;

static const TCHAR* ArachneMeshPath = TEXT("/Game/ARACHNE/Characters/SK_Arachne.SK_Arachne");

/** Wave gait rule: a leg never lifts while a neighbour (same side front/back, or its opposite) is in the air. */
static bool NeighbourSwinging(const TArray<FArachneLeg>& Legs, int32 Index)
{
    const int32 Pair = Index / 2, Side = Index % 2;
    const int32 N[3] = {(Pair - 1) * 2 + Side, (Pair + 1) * 2 + Side, Pair * 2 + (1 - Side)};
    for (const int32 K : N) if (K >= 0 && K < Legs.Num() && Legs[K].bSwinging) return true;
    return false;
}

// =====================================================================================================================
// Construction
// =====================================================================================================================

AArachnePawn::AArachnePawn()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickGroup = TG_PostPhysics;   // movers/platforms have already moved this frame; AI components tick before

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

    Senses = CreateDefaultSubobject<UArachneSensesComponent>(TEXT("Senses"));
    Memory = CreateDefaultSubobject<UArachneMemoryComponent>(TEXT("Memory"));
    Navigator = CreateDefaultSubobject<UArachneNavigatorComponent>(TEXT("Navigator"));
    Brain = CreateDefaultSubobject<UArachneBrainComponent>(TEXT("Brain"));

    AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;
}

void AArachnePawn::LoadSpiderAsset()
{
    if (!SpiderAsset) SpiderAsset = LoadObject<USkeletalMesh>(nullptr, ArachneMeshPath, nullptr, LOAD_NoWarn);
    if (SpiderAsset && SpiderMesh->GetSkinnedAsset() != SpiderAsset) SpiderMesh->SetSkinnedAssetAndUpdate(SpiderAsset);
}

void AArachnePawn::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
    LoadSpiderAsset();
    SpiderMesh->SetRelativeLocationAndRotation(FVector(0, 0, -BodyHeight), FQuat::Identity);
}

void AArachnePawn::BeginPlay()
{
    Super::BeginPlay();
    LoadSpiderAsset();
    if (!SpiderAsset) UE_LOG(LogArachne, Error, TEXT("ARACHNE: %s missing - run Scripts/setup_arachne.py"), ArachneMeshPath);
    if (Rig.Initialize(Cast<USkeletalMesh>(SpiderMesh->GetSkinnedAsset())))
        UE_LOG(LogArachne, Display, TEXT("ARACHNE: rig ready, %d IK chains, %d bones"), Rig.Legs.Num(), Rig.ReferenceCS.Num());

    SpawnLocation = GetActorLocation();
    SpawnRotation = GetActorRotation();
    ResetCrawler(SpawnLocation, SpawnRotation);
}

// =====================================================================================================================
// Steering
// =====================================================================================================================

void AArachnePawn::SetMoveDirection(FVector WorldDirection)
{
    WorldMove = WorldDirection.GetClampedToMaxSize(1.0);
    bWorldMove = true;
}

void AArachnePawn::SetMovementInput(float Forward, float Right)
{
    MoveInput = FVector2D(Forward, Right).GetClampedToMaxSize(1.0);
    bWorldMove = false;
}

void AArachnePawn::SetFacingDirection(FVector WorldDirection)
{
    FacingOverride = WorldDirection.GetSafeNormal();
}

FVector AArachnePawn::GetEyeLocation() const
{
    const FVector Right = FVector::CrossProduct(SurfaceUp, Facing);
    return GetActorLocation() + Facing * EyeOffset.X + Right * EyeOffset.Y + SurfaceUp * EyeOffset.Z;
}

FVector AArachnePawn::WishDirection() const
{
    if (bWorldMove)
    {
        const double Size = WorldMove.Size();
        if (Size < 1e-3) return FVector::ZeroVector;
        const FVector P = FVector::VectorPlaneProject(WorldMove, SurfaceUp);
        const double Len = P.Size();
        // A request pointing (almost) straight into / out of the surface has no usable direction on it.
        return Len > .05 * Size ? P / Len * FMath::Min(Size, 1.0) : FVector::ZeroVector;
    }
    const FVector Fwd = PlaneDir(Heading, SurfaceUp, Facing);
    const FVector Right = FVector::CrossProduct(SurfaceUp, Fwd);
    return (Fwd * MoveInput.X + Right * MoveInput.Y).GetClampedToMaxSize(1.0);
}

void AArachnePawn::TurnFacing(const FVector& Target, float Dt)
{
    const double Yaw = SignedAngleAround(Facing, Target, SurfaceUp);
    Facing = FQuat(SurfaceUp, Yaw * Damp(FacingTurnSpeed, Dt)).RotateVector(Facing);
}

// =====================================================================================================================
// Public actions
// =====================================================================================================================

void AArachnePawn::ResetCrawler(FVector Location, FRotator Rotation)
{
    SetActorLocationAndRotation(Location, Rotation, false, nullptr, ETeleportType::TeleportPhysics);
    Facing = GetActorForwardVector();
    SurfaceUp = GetActorUpVector();
    Heading = Facing;
    PrevFacing = Facing;
    SupportNormal = SurfaceUp;
    SupportPoint = Location - SurfaceUp * BodyHeight;
    Velocity = TravelVelocity = PrevTravelVelocity = FVector::ZeroVector;
    Transition = FArachneTransition();
    bAnchored = false;
    AnchorFeet.Reset();
    FootFitWeight = 1.f;
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
    for (int32 I = 0; I < Rig.Legs.Num(); ++I)
    {
        FArachneLeg& L = Rig.Legs[I];
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
}

void AArachnePawn::AddImpulse(FVector Impulse)
{
    if (bAnchored) EndAnchor();
    const FVector Local = GetActorTransform().InverseTransformVectorNoScale(Impulse);
    BodyOffsetSpring.V += Local * .35;
    if (!bAttached && !Transition.bActive) { Velocity += Impulse; return; }
    if (FVector::DotProduct(Impulse, SurfaceUp) > 250.0)
    {
        Velocity = FVector::VectorPlaneProject(Velocity, SurfaceUp) + Impulse;
        Detach();
        AttachCooldown = .15f;
        for (FArachneLeg& L : Rig.Legs) { L.bPlanted = L.bSwinging = false; L.AirVelocity = FVector::ZeroVector; }
        return;
    }
    Velocity += FVector::VectorPlaneProject(Impulse, SurfaceUp);
}

// =====================================================================================================================
// Anchors
// =====================================================================================================================

void AArachnePawn::BeginAnchor(const FArachneAnchor& InAnchor)
{
    Anchor = InAnchor;
    Anchor.Body.SetScale3D(FVector::OneVector);
    bAnchored = true;
    AnchorAlpha = 0.f;
    AnchorStartLocation = GetActorLocation();
    AnchorStartRotation = GetActorQuat();
    Transition.bActive = false;
    bAttached = true;
    Velocity = FVector::ZeroVector;
    SetBodySupport(nullptr);
    ComputeAnchorFeet();
}

void AArachnePawn::EndAnchor()
{
    if (!bAnchored) return;
    bAnchored = false;
    AnchorFeet.Reset();
    bAttached = true;
    SupportNormal = SurfaceUp;
    UnsupportedTime = 0.f;
    Velocity = FVector::ZeroVector;
}

bool AArachnePawn::IsAnchorSettled() const
{
    if (!bAnchored || AnchorAlpha < 1.f) return false;
    for (int32 I = 0; I < Rig.Legs.Num(); ++I)
    {
        const FArachneLeg& L = Rig.Legs[I];
        if (AnchorFeet.IsValidIndex(I) && AnchorFeet[I].bBlockingHit && (!L.bPlanted || L.bSwinging)) return false;
    }
    return true;
}

void AArachnePawn::ComputeAnchorFeet()
{
    AnchorFeet.Init(FHitResult(), Rig.Legs.Num());
    const FTransform FinalBase = FTransform(FVector(0, 0, -BodyHeight)) * Anchor.Body;
    const FVector Up = Anchor.Body.GetRotation().GetUpVector();
    for (int32 I = 0; I < Rig.Legs.Num(); ++I)
    {
        const FArachneLeg& L = Rig.Legs[I];
        if (!Anchor.bVirtualSurface)
        {
            FHitResult Hit;
            if (FArachneRig::FindAnchorFoot(GetWorld(), L, FinalBase, Up, this, Hit)) AnchorFeet[I] = Hit;
            continue;
        }
        // Virtual surface: aim each leg from the hip through its rest foot onto the plane, within reach.
        const FVector N = Anchor.VirtualNormal.GetSafeNormal();
        const FVector Home = FinalBase.TransformPosition(L.Rest.Last());
        const FVector Hip = FinalBase.TransformPosition(L.Rest[2]);
        const FVector Dir = (Home - Hip).GetSafeNormal();
        FVector P = FVector::PointPlaneProject(Home, Anchor.VirtualPoint, N);
        const double Den = FVector::DotProduct(Dir, N);
        if (FMath::Abs(Den) > .1)
        {
            const double T = FVector::DotProduct(Anchor.VirtualPoint - Hip, N) / Den;
            if (T > 0.0) P = Hip + Dir * T;
        }
        const double Reach = L.Reach * .97;
        if (FVector::Dist(Hip, P) > Reach) P = Hip + (P - Hip).GetSafeNormal() * Reach;
        FHitResult Hit;
        Hit.bBlockingHit = true;
        Hit.Location = Hit.ImpactPoint = P;
        Hit.Normal = Hit.ImpactNormal = N;
        AnchorFeet[I] = Hit;
    }
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
    Heading = PlaneDir(Delta.RotateVector(Heading), SurfaceUp, Facing);
    if (bRotateVelocity) Velocity = Delta.RotateVector(Velocity);
}

void AArachnePawn::ApplyFrame()
{
    Facing = PlaneDir(Facing, SurfaceUp, FVector::CrossProduct(GetActorRightVector(), SurfaceUp));
    SetActorRotation(FRotationMatrix::MakeFromZX(SurfaceUp, Facing).ToQuat());
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
    if (bAnchored) StepAnchored(Dt);
    else if (Transition.bActive) StepTransition(Dt);
    else if (bAttached) StepAttached(Dt);
    else StepAir(Dt);
    if (GetActorLocation().Z < -8000.0) ResetCrawler(SpawnLocation, SpawnRotation);
}

void AArachnePawn::StepAnchored(float Dt)
{
    const FVector Before = GetActorLocation();
    AnchorAlpha = FMath::Min(1.f, AnchorAlpha + Dt / FMath::Max(Anchor.BlendTime, .05f));
    const float A = FMath::SmoothStep(0.f, 1.f, AnchorAlpha);
    const FVector P = FMath::Lerp(AnchorStartLocation, Anchor.Body.GetLocation(), static_cast<double>(A));
    const FQuat Q = FQuat::Slerp(AnchorStartRotation, Anchor.Body.GetRotation(), A).GetNormalized();
    SetActorLocationAndRotation(P, Q, false, nullptr, ETeleportType::None);
    SurfaceUp = Q.GetUpVector();
    Facing = Q.GetForwardVector();
    Heading = Facing;
    Velocity = (P - Before) / FMath::Max(Dt, 1e-4f);
    bAttached = true;
    UnsupportedTime = 0.f;
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

    // Facing: an explicit facing request wins (look around, crab walk), otherwise face the way we travel.
    FVector FaceTarget = FVector::ZeroVector;
    if (!FacingOverride.IsNearlyZero()) FaceTarget = PlaneDir(FacingOverride, Up, FVector::ZeroVector);
    else if (bWants) FaceTarget = bWorldMove ? Wish.GetSafeNormal() : PlaneDir(Heading, Up, Facing);
    if (!FaceTarget.IsNearlyZero()) TurnFacing(FaceTarget, Dt);

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
    double Want = FVector::DotProduct(Wish, Fwd) * MaxSpeed;
    // AI steering aims at world targets, whose projection can vanish mid-arc: once committed, finish the arc.
    if (bWorldMove && !Wish.IsNearlyZero()) Want = FMath::Max(Want, MaxSpeed * .5);
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

    UpdateLegs(DeltaSeconds);
    UpdateBodyVisual(DeltaSeconds);
    BuildPose(DeltaSeconds);
    if (bDebugBody) DrawBodyDebug();
}

// =====================================================================================================================
// Legs
// =====================================================================================================================

FTransform AArachnePawn::BaseMeshTransform() const
{
    return FTransform(FVector(0, 0, -BodyHeight)) * GetActorTransform();
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
    FArachneLeg& L = Rig.Legs[Index];
    L.bSwinging = false;
    L.bPlanted = true;
    L.Swing = 1.f;
    L.Plant = L.Target;
    L.PlantNormal = L.TargetNormal;
    L.Support = L.TargetSupport;
    L.bVirtualPlant = !L.Support.IsValid();
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

bool AArachnePawn::BeginSwing(int32 Index, const FHitResult& Hit, float SwingTime)
{
    FArachneLeg& L = Rig.Legs[Index];
    const bool bWasPlanted = L.bPlanted;
    L.Start = L.Foot;
    L.StartNormal = bWasPlanted ? L.PlantNormal : SurfaceUp;
    L.Target = Hit.ImpactPoint;
    L.TargetNormal = Hit.ImpactNormal;
    L.TargetSupport = Hit.GetComponent();
    if (UPrimitiveComponent* S = L.TargetSupport.Get()) L.LocalTarget = S->GetComponentTransform().InverseTransformPosition(L.Target);
    L.SwingLength = static_cast<float>(FVector::Dist(L.Start, L.Target));
    if (L.SwingLength < 1.5f) { PlantLeg(Index, false); return false; }
    L.bPlanted = false;
    L.bSwinging = true;
    L.Swing = 0.f;
    L.SwingDuration = bWasPlanted ? SwingTime : SwingTime * .75f;
    L.SwingHeight = StepHeight * FMath::Clamp(L.SwingLength / FMath::Max(StepDistance, 1.f), .45f, 1.35f);
    return true;
}

void AArachnePawn::AdvanceSwing(int32 Index, float Dt)
{
    FArachneLeg& L = Rig.Legs[Index];
    if (UPrimitiveComponent* S = L.TargetSupport.Get()) L.Target = S->GetComponentTransform().TransformPosition(L.LocalTarget);
    L.Swing = FMath::Min(1.f, L.Swing + Dt / FMath::Max(L.SwingDuration, .05f));
    const float T = L.Swing;
    const float Ease = T * T * (3.f - 2.f * T);
    const float Arc = FMath::Sin(PI * T) * (1.f + .3f * (1.f - T));   // lifts briskly, plants decisively
    const FVector Lift = (L.StartNormal + L.TargetNormal + SurfaceUp * 1.5).GetSafeNormal();
    L.Foot = FMath::Lerp(L.Start, L.Target, static_cast<double>(Ease)) + Lift * (L.SwingHeight * Arc);
    if (T >= 1.f) PlantLeg(Index, true);
}

void AArachnePawn::UpdateLegs(float Dt)
{
    if (!IsRigValid()) return;
    SupportedFeet = 0;
    for (FArachneLeg& L : Rig.Legs)
    {
        if (!L.bPlanted) continue;
        if (UPrimitiveComponent* S = L.Support.Get()) L.Plant = S->GetComponentTransform().TransformPosition(L.LocalPlant);
        L.Foot = L.Plant;
        L.PlantedTime += Dt;
        ++SupportedFeet;
    }
    if (bAnchored) UpdateAnchoredLegs(Dt);
    else if (bAttached || Transition.bActive) UpdateGroundedLegs(Dt);
    else UpdateAirLegs(Dt);
}

void AArachnePawn::UpdateAirLegs(float Dt)
{
    // Airborne: legs paddle and reach, each foot on its own critically damped spring.
    const FTransform Base = BaseMeshTransform();
    const FVector Up = SurfaceUp;
    const int32 Sub = FMath::Clamp(FMath::CeilToInt(Dt * 120.f), 1, 8);
    const double H = Dt / Sub;
    for (int32 I = 0; I < Rig.Legs.Num(); ++I)
    {
        FArachneLeg& L = Rig.Legs[I];
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
}

void AArachnePawn::UpdateGroundedLegs(float Dt)
{
    TArray<FArachneLeg>& Legs = Rig.Legs;
    const FTransform Base = BaseMeshTransform();
    const FVector Up = SurfaceUp;
    const FVector C = GetActorLocation();
    const double Speed = Velocity.Size();
    const bool bIdle = Speed < 8.0 && FMath::Abs(YawRate) < .25f;
    IdleTime = bIdle ? IdleTime + Dt : 0.f;
    const double SpeedRatio = FMath::Clamp(Speed / FMath::Max(MoveSpeed, 1.f), 0.0, 2.0);
    const float SwingTime = StepDuration / (1.f + .55f * static_cast<float>(SpeedRatio));
    const double Threshold = StepDistance * (1.0 + .35 * SpeedRatio);
    const bool bSettling = IdleTime > .18f;

    int32 Swinging = 0;
    for (const FArachneLeg& L : Legs) if (L.bSwinging) ++Swinging;

    // Where a leg would like to put its foot, ahead of the body by the distance it travels while the foot is in the air.
    auto PredictHome = [&](const FArachneLeg& L, float Remaining)
    {
        const FVector Home = Base.TransformPosition(L.Rest.Last());
        const FVector Dir = Speed > 1.0 ? Velocity / Speed : FVector::ZeroVector;
        const FVector Lead = (Velocity * Remaining + Dir * (Threshold * .5 * StepLead)).GetClampedToMaxSize(Threshold * 1.3);
        const FQuat Turn(Up, YawRate * Remaining * 1.5f);
        return C + Turn.RotateVector(Home - C) + Lead;
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
        if (!L.Support.IsValid()) Err = Threshold * 3.0;   // virtual plant or support destroyed: re-plant on real geometry
        const double Limit = bSettling ? IdleSettleDistance : Threshold;
        if (Err > Limit || bOverReach) Candidates.Add({I, Err / Threshold + (bOverReach ? 10.0 : 0.0)});
    }
    Candidates.Sort([](const FCandidate& A, const FCandidate& B) { return A.Urgency > B.Urgency; });

    for (const FCandidate& Cand : Candidates)
    {
        if (Swinging >= MaxSwingingLegs) break;
        const bool bUrgent = Cand.Urgency > 2.2;
        if (bSettling && Swinging > 0 && !bUrgent) break;             // settle shuffle: one leg at a time
        if (!bUrgent && NeighbourSwinging(Legs, Cand.Index)) continue; // wave gait: neighbours never lift together
        FArachneLeg& L = Legs[Cand.Index];
        FHitResult Hit;
        if (!FindFoot(L, PredictHome(L, SwingTime), Hit))
        {
            // Nothing reachable to stand on: let a loose foot hang towards its rest pose instead of freezing.
            if (!L.bPlanted) L.Foot = FMath::Lerp(L.Foot, Base.TransformPosition(L.Rest.Last()) + Up * 8.0, Damp(8.0, Dt));
            continue;
        }
        if (BeginSwing(Cand.Index, Hit, SwingTime)) ++Swinging;
    }

    // 2. Advance swinging feet along a lifted arc; keep re-aiming during the first half of the swing.
    for (int32 I = 0; I < Legs.Num(); ++I)
    {
        FArachneLeg& L = Legs[I];
        if (!L.bSwinging) continue;
        FHitResult Hit;
        if (L.Swing < .55f && (Speed > 10.0 || FMath::Abs(YawRate) > .2f) && FindFoot(L, PredictHome(L, L.SwingDuration * (1.f - L.Swing)), Hit))
        {
            L.Target = FMath::Lerp(L.Target, Hit.ImpactPoint, .35);
            L.TargetNormal = Hit.ImpactNormal;
            L.TargetSupport = Hit.GetComponent();
            if (UPrimitiveComponent* S = L.TargetSupport.Get()) L.LocalTarget = S->GetComponentTransform().InverseTransformPosition(L.Target);
        }
        AdvanceSwing(I, Dt);
    }
}

void AArachnePawn::UpdateAnchoredLegs(float Dt)
{
    // Every leg walks to its anchor contact, most displaced first, keeping the wave gait so the grab looks deliberate.
    TArray<FArachneLeg>& Legs = Rig.Legs;
    const FTransform Base = BaseMeshTransform();
    const float SwingTime = StepDuration * 1.25f;
    IdleTime = 0.f;

    int32 Swinging = 0;
    for (const FArachneLeg& L : Legs) if (L.bSwinging) ++Swinging;

    struct FCandidate { int32 Index; double Error; };
    TArray<FCandidate, TInlineAllocator<8>> Candidates;
    for (int32 I = 0; I < Legs.Num(); ++I)
    {
        FArachneLeg& L = Legs[I];
        if (L.bSwinging) continue;
        if (!AnchorFeet.IsValidIndex(I) || !AnchorFeet[I].bBlockingHit)
        {
            // Nothing to grab for this leg: tuck it towards the rest pose.
            L.bPlanted = false;
            L.Foot = FMath::Lerp(L.Foot, Base.TransformPosition(L.Rest.Last()) + SurfaceUp * 8.0, Damp(6.0, Dt));
            continue;
        }
        const double Err = L.bPlanted ? FVector::Dist(L.Plant, AnchorFeet[I].ImpactPoint) : 1e6;
        if (Err > AnchorFootTolerance) Candidates.Add({I, Err});
    }
    Candidates.Sort([](const FCandidate& A, const FCandidate& B) { return A.Error > B.Error; });

    for (const FCandidate& Cand : Candidates)
    {
        if (Swinging >= MaxSwingingLegs) break;
        if (Swinging > 0 && NeighbourSwinging(Legs, Cand.Index)) continue;
        if (BeginSwing(Cand.Index, AnchorFeet[Cand.Index], SwingTime)) ++Swinging;
    }
    for (int32 I = 0; I < Legs.Num(); ++I) if (Legs[I].bSwinging) AdvanceSwing(I, Dt);
}

// =====================================================================================================================
// Body visuals + pose
// =====================================================================================================================

void AArachnePawn::UpdateBodyVisual(float Dt)
{
    if (!IsRigValid())
    {
        SpiderMesh->SetRelativeLocationAndRotation(FVector(0, 0, -BodyHeight), FQuat::Identity);
        return;
    }
    const FTransform Base = BaseMeshTransform();
    // Held poses define the body orientation themselves: stop fitting the body to the feet while anchored.
    FootFitWeight = FMath::Lerp(FootFitWeight, bAnchored ? 0.f : 1.f, static_cast<float>(Damp(4.0, Dt)));

    // Fit a plane through the feet (relative to their rest heights): body pitches/rolls with the terrain.
    double Front = 0, Back = 0, Left = 0, Right = 0, Height = 0, Lift = 0;
    int32 NF = 0, NB = 0, NL = 0, NR = 0;
    for (const FArachneLeg& L : Rig.Legs)
    {
        const double Z = Base.InverseTransformPosition(L.Foot).Z - L.Rest.Last().Z;
        Height += Z;
        if (L.Pair <= 2) { Front += Z; ++NF; } else { Back += Z; ++NB; }
        if (L.Side == 0) { Left += Z; ++NL; } else { Right += Z; ++NR; }
        if (L.bSwinging) Lift += FMath::Sin(PI * L.Swing);
    }
    Height = Height / Rig.Legs.Num() * FootFitWeight;
    const double SlopeX = (Front / FMath::Max(NF, 1) - Back / FMath::Max(NB, 1)) / 170.0 * FootFitWeight;   // + = front higher
    const double SlopeY = (Right / FMath::Max(NR, 1) - Left / FMath::Max(NL, 1)) / 200.0 * FootFitWeight;   // + = right higher

    // Inertia: acceleration in the body frame (lags the body and tips it).
    const FVector Accel = GetActorTransform().InverseTransformVectorNoScale((TravelVelocity - PrevTravelVelocity) / FMath::Max(Dt, 1e-3f)).GetClampedToMaxSize(4000.0);
    const bool bGrounded = bAttached || Transition.bActive;
    const double Breath = FMath::Sin(AnimTime * 2.1) * .8 * (1.0 - Stillness * .8);

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
    const double Alive = 1.0 - Stillness * .85;
    const FVector AbdomenTarget(
        FMath::Clamp(Accel.Y * .004, -8.0, 8.0) * AbdomenJiggle,
        (FMath::Clamp(Accel.X * .006, -12.0, 12.0) + FMath::Sin(GaitPhase * 2.0) * 1.6 * SpeedRatio + FMath::Sin(AnimTime * 1.6) * 1.2 * Alive) * AbdomenJiggle,
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

void AArachnePawn::BuildPose(float Dt)
{
    if (!IsRigValid() || !SpiderMesh->GetSkinnedAsset()) return;
    TArray<FTransform> Pose = Rig.ReferenceCS;
    MaxFootError = 0.f;
    const FTransform MeshT = SpiderMesh->GetComponentTransform();
    const UWorld* DebugWorld = bDebugBody ? GetWorld() : nullptr;
    for (const FArachneLeg& L : Rig.Legs) MaxFootError = FMath::Max(MaxFootError, Rig.SolveLeg(L, MeshT, L.Foot, Pose, DebugWorld));

    FArachneExtrasInput Extras;
    Extras.AbdomenEuler = AbdomenSpring.X;
    Extras.SpeedRatio = FMath::Clamp(Velocity.Size() / FMath::Max(MoveSpeed, 1.f), 0.0, 1.5);
    Extras.GaitPhase = GaitPhase;
    Extras.AnimTime = AnimTime;
    Extras.PedipalpMotion = PedipalpMotion;
    Extras.Stillness = Stillness;
    Rig.AnimateExtras(Extras, Pose);
    FArachneRig::ApplyPose(SpiderMesh, Pose);
}

void AArachnePawn::DrawBodyDebug() const
{
    UWorld* World = GetWorld();
    const FVector P = GetActorLocation();
    DrawDebugDirectionalArrow(World, P, P + SurfaceUp * 110.0, 15.f, bAttached ? FColor::Cyan : FColor::Red, false, 0.f, 0, 3.f);
    DrawDebugDirectionalArrow(World, P, P + Velocity * .4, 12.f, FColor::Orange, false, 0.f, 0, 2.f);
    if (Transition.bActive)
    {
        DrawDebugSphere(World, Transition.Pivot, 8.f, 8, Transition.bConvex ? FColor::Magenta : FColor::Yellow, false, 0.f);
        DrawDebugLine(World, Transition.Pivot - Transition.Axis * 120.0, Transition.Pivot + Transition.Axis * 120.0, FColor::Magenta, false, 0.f, 0, 1.5f);
    }
    for (const FArachneLeg& L : Rig.Legs)
    {
        DrawDebugSphere(World, L.Foot, 4.f, 8, L.bPlanted ? FColor::Green : FColor::Orange, false, 0.f);
        if (L.bSwinging) DrawDebugSphere(World, L.Target, 3.f, 6, FColor::Blue, false, 0.f);
    }
    if (bAnchored)
    {
        DrawDebugCoordinateSystem(World, Anchor.Body.GetLocation(), Anchor.Body.Rotator(), 60.f, false, 0.f, 0, 1.5f);
        for (const FHitResult& H : AnchorFeet) if (H.bBlockingHit) DrawDebugPoint(World, H.ImpactPoint, 9.f, FColor::Cyan, false, 0.f);
    }
}
