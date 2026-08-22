#include "Visual/ElysiumMeleeTrail.h"

#include "ElysiumAnimationIntent.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumPlayer.h"
#include "Substrate/ElysiumItemClasses.h"
#include "Visual/ElysiumNpcVisual.h"
#include "Visual/ElysiumRenderedBone.h"

#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkeletalMeshSocket.h"
#include "GameFramework/Actor.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "UObject/StrongObjectPtr.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumMeleeTrail, Log, All);

namespace
{
	FName TrailComponentTag()
	{
		static const FName Tag(TEXT("ElysiumMeleeTrail"));
		return Tag;
	}

	// Held outside any UObject, so a plain TStrongObjectPtr roots it (`Source/ElysiumUE/CLAUDE.md`
	// C++ coding policy) -- there is no owning actor/component this free-function namespace could
	// hang a UPROPERTY off. The attempt is made ONCE per process, success or failure: this is
	// called per swing frame, and a missing package would otherwise re-run a failing LoadObject
	// and re-emit the same warning every frame of every swing ("log once where the failure is
	// owned"). A system baked mid-session is picked up on the next launch.
	UNiagaraSystem* LoadTrailSystem()
	{
		static TStrongObjectPtr<UNiagaraSystem> System;
		static bool bTried = false;
		if (!bTried)
		{
			bTried = true;
			System = TStrongObjectPtr<UNiagaraSystem>(LoadObject<UNiagaraSystem>(nullptr,
				TEXT("/Game/ElysiumAuthored/VFX/NS_ElysiumMeleeTrail.NS_ElysiumMeleeTrail")));
			if (!System.IsValid())
			{
				UE_LOG(LogElysiumMeleeTrail, Warning,
					TEXT("melee trail: /Game/ElysiumAuthored/VFX/NS_ElysiumMeleeTrail did not load "
					     "-- the tracked authored asset is missing from Content/ElysiumAuthored. "
					     "Trails stay off this session."));
			}
		}
		return System.Get();
	}

	// One body's live-swing trail state. The wield model and its TrailTip socket cannot change
	// mid-swing (a weapon swap ends the transaction through OnHolstered), so they are resolved
	// once per accepted swing -- keyed by FSwing::Serial -- rather than component-walked and
	// socket-scanned every frame of every swing.
	struct FActiveTrail
	{
		int32 SwingSerial = 0;
		// False when this swing's wield mesh carries no TrailTip socket -- the ordinary answer for
		// a melee weapon outside the socket_prop corpus (fire axe, baton, severed arm), cached so
		// the refusal is not re-derived per frame either.
		bool bEligible = false;
		TWeakObjectPtr<USkeletalMeshComponent> Wield;
		FName TipBone;
		FTransform TipLocal;
		TWeakObjectPtr<UNiagaraComponent> Component;
	};

	// The live-swing set, keyed by body. Weak on both sides, swept every Advance: an entry whose
	// body is gone or whose swing ended is deactivated and dropped, so the idle majority of the
	// cast costs nothing here -- no per-frame component walk answers "no trail" for a character
	// that never swung (the walk this map exists to avoid).
	TMap<TWeakObjectPtr<USkeletalMeshComponent>, FActiveTrail> GActiveTrails;

	// Found by walking the owner's components rather than cached, the same reason
	// ElysiumNpcVisual::FindWieldModel is: the body can be rebuilt under a caller that still holds
	// a stale pointer. Reached only from the swing paths below, never per idle character.
	UNiagaraComponent* FindTrailComponent(const USkeletalMeshComponent* Body)
	{
		const AActor* const Owner = Body != nullptr ? Body->GetOwner() : nullptr;
		if (Owner == nullptr)
		{
			return nullptr;
		}
		TArray<UNiagaraComponent*> Components;
		Owner->GetComponents(Components);
		for (UNiagaraComponent* Component : Components)
		{
			if (Component->ComponentHasTag(TrailComponentTag())
				&& Component->GetAttachParent() == Body)
			{
				return Component;
			}
		}
		return nullptr;
	}

	// Take away the trail components this body no longer owns, and the ones whose body is already
	// gone -- the same ownership trap ElysiumNpcVisual::SweepWieldModels documents: the extra
	// belongs to the OWNING ACTOR, so rebuilding one body leaves its trail behind, parented to an
	// actor that is still alive.
	void SweepTrailComponents(AActor* Owner, const USkeletalMeshComponent* Body)
	{
		TArray<UNiagaraComponent*> Components;
		Owner->GetComponents(Components);
		for (UNiagaraComponent* Component : Components)
		{
			if (!Component->ComponentHasTag(TrailComponentTag()))
			{
				continue;
			}
			if (Component->GetAttachParent() == nullptr || Component->GetAttachParent() == Body)
			{
				Component->DestroyComponent();
			}
		}
	}

	UNiagaraComponent* FindOrCreateTrailComponent(USkeletalMeshComponent* Body)
	{
		if (UNiagaraComponent* const Existing = FindTrailComponent(Body))
		{
			return Existing;
		}
		AActor* const Owner = Body != nullptr ? Body->GetOwner() : nullptr;
		UNiagaraSystem* const System = LoadTrailSystem();
		if (Owner == nullptr || System == nullptr)
		{
			return nullptr;
		}
		// FIRST, the same unconditional sweep InstallWieldModel opens with: a rebuilt body leaves
		// the previous trail parented to this same actor with a stale attach parent, and creating
		// beside it would accumulate one orphan per rebuild.
		SweepTrailComponents(Owner, Body);
		UNiagaraComponent* const Component = NewObject<UNiagaraComponent>(Owner);
		Component->ComponentTags.Add(TrailComponentTag());
		Component->SetAsset(System);
		Component->SetAutoActivate(false);
		// Attaching before registering keeps the component from ticking against an unset parent for
		// a frame, the same ordering ElysiumNpcVisual::InstallWieldModel depends on.
		Component->SetupAttachment(Body);
		Component->RegisterComponent();
		Component->AttachToComponent(Body, FAttachmentTransformRules::SnapToTargetIncludingScale);
		Owner->AddInstanceComponent(Component);
		return Component;
	}

	// The once-per-swing resolution: which wield mesh this swing rides, and where its TrailTip
	// socket sits. Fills `Entry` for FSwing `Serial` on `Body`.
	void ResolveSwing(USkeletalMeshComponent& Body, int32 Serial, FActiveTrail& Entry)
	{
		Entry.SwingSerial = Serial;
		Entry.bEligible = false;
		Entry.Wield = nullptr;
		USkeletalMeshComponent* const Wield = ElysiumNpcVisual::FindWieldModel(&Body);
		const USkeletalMesh* const WieldMesh = Wield ? Wield->GetSkeletalMeshAsset() : nullptr;
		const USkeletalMeshSocket* const Tip =
			WieldMesh ? WieldMesh->FindSocket(TEXT("TrailTip")) : nullptr;
		if (Tip == nullptr)
		{
			// Only a bake-tagged (`binding == "socket_prop"`) melee wield mesh carries one -- this
			// IS the feature's whole scope gate, with no classname check anywhere in this file.
			return;
		}
		Entry.bEligible = true;
		Entry.Wield = Wield;
		Entry.TipBone = Tip->BoneName;
		Entry.TipLocal = FTransform(
			Tip->RelativeRotation.Quaternion(), Tip->RelativeLocation, Tip->RelativeScale);
	}
}

bool ElysiumMeleeTrail::ShouldTrailBeActive(const FElysiumWeapon::FSwing& Swing)
{
	return Swing.bActive && Swing.bMelee;
}

void ElysiumMeleeTrail::Advance(float /*DeltaSeconds*/, FElysiumEntityWorld& World)
{
	// The bodies whose swing is live THIS frame; everything else in GActiveTrails is swept below.
	TSet<TWeakObjectPtr<USkeletalMeshComponent>> Live;
	for (const TUniquePtr<FElysiumEntity>& EntPtr : World.Entities())
	{
		FElysiumEntity* const Ent = EntPtr.Get();
		FElysiumCombatCharacter* const Character = Ent ? Ent->AsCombatCharacter() : nullptr;
		if (Character == nullptr || Ent->IsInert())
		{
			continue;
		}
		USkeletalMeshComponent* const Body = Character->GetSkeletalBody();
		if (Body == nullptr)
		{
			continue;
		}
		FElysiumItem* const Active = Character->Inventory.Active(*Character);
		FElysiumWeapon* const Weapon = Active ? Active->AsWeapon() : nullptr;
		if (Weapon == nullptr || !ShouldTrailBeActive(Weapon->Swing))
		{
			// The idle majority: no map entry means nothing to do, and the end-of-frame sweep
			// handles a swing that just ended -- no component walk happens here.
			continue;
		}
		Live.Add(Body);

		FActiveTrail& Entry = GActiveTrails.FindOrAdd(Body);
		// Re-resolve on a new swing, and mid-swing only if the wield component itself was
		// rebuilt underneath the cached pointer.
		if (Entry.SwingSerial != Weapon->Swing.Serial
			|| (Entry.bEligible && !Entry.Wield.IsValid()))
		{
			ResolveSwing(*Body, Weapon->Swing.Serial, Entry);
		}
		USkeletalMeshComponent* const Wield = Entry.Wield.Get();
		if (!Entry.bEligible || Wield == nullptr)
		{
			continue;
		}
		// The accepted transaction may not have reached a rendering frame yet -- the same refusal
		// AdvanceSwingContact's own callers rely on.
		FElysiumClipPhase Phase;
		if (!Character->GetLiveClipPhase(
			Weapon->Swing.ClipOwnerStem, Weapon->Swing.ClipLabel, Phase))
		{
			continue;
		}
		if (Wield->bHiddenInGame)
		{
			// The same first-person-submission / entity-hidden gate the wield mesh itself already
			// answers to (`AElysiumPawn::ApplyDrawPolicy` over `FElysiumCameraView::bDrawWorldWeapon`)
			// -- a weapon that is not drawn casts no trail either.
			if (UNiagaraComponent* const Trail = Entry.Component.Get())
			{
				if (Trail->IsActive())
				{
					Trail->Deactivate();
				}
			}
			continue;
		}
		FTransform MountWorld;
		if (!ElysiumRenderedBone::RenderedWorldTransform(*Wield, Entry.TipBone, MountWorld))
		{
			continue;   // the install frame -- no render packet has arrived yet
		}

		UNiagaraComponent* Trail = Entry.Component.Get();
		if (Trail == nullptr)
		{
			Trail = FindOrCreateTrailComponent(Body);
			Entry.Component = Trail;
		}
		if (Trail != nullptr)
		{
			Trail->SetVariablePosition(TEXT("User.TrailPointB"), MountWorld.GetLocation());
			Trail->SetVariablePosition(TEXT("User.TrailPointA"),
				(Entry.TipLocal * MountWorld).GetLocation());
			if (!Trail->IsActive())
			{
				Trail->Activate();
			}
		}
	}

	// Everything the loop did not mark live is over -- the swing ended, the character went inert
	// or died mid-swing, or the body itself is gone. One pass deactivates and drops them all, so
	// no per-character search was needed above and no entry can outlive its swing.
	for (auto It = GActiveTrails.CreateIterator(); It; ++It)
	{
		if (It.Key().IsValid() && Live.Contains(It.Key()))
		{
			continue;
		}
		if (UNiagaraComponent* const Trail = It.Value().Component.Get())
		{
			if (Trail->IsActive())
			{
				Trail->Deactivate();
			}
		}
		It.RemoveCurrent();
	}
}

void ElysiumMeleeTrail::ClearTrail(USkeletalMeshComponent* Body)
{
	GActiveTrails.Remove(Body);
	AActor* const Owner = Body != nullptr ? Body->GetOwner() : nullptr;
	if (Owner != nullptr)
	{
		SweepTrailComponents(Owner, Body);
	}
}
