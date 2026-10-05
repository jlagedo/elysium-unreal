#include "Debug/ElysiumArenaV6Adapters.h"
#if !UE_BUILD_SHIPPING
#include "Debug/ElysiumArenaStage.h"
#include "ElysiumClassRegistry.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumPlayer.h"
#include "ElysiumSaveArchive.h"
#include "ElysiumVariant.h"
#include "Substrate/ElysiumAttackCoordinator.h"
#include "Substrate/ElysiumInterestingPlace.h"
#include "Substrate/ElysiumItemTable.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumWeaponClasses.h"
#include "Visual/ElysiumNpcClips.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
namespace ElysiumArenaV6Detail
{
    bool Refuse(const FString& Name, FString& Error)
    {
        Error = FString::Printf(TEXT("V6 adapter: %s has no reader yet"), *Name);
        return false;
    }
    FElysiumEntity* FindEntity(FElysiumEntityWorld& World, const FString& Who)
    {
        if (Who.Equals(TEXT("player"), ESearchCase::IgnoreCase)) return World.FindPlayer();
        if (Who.StartsWith(TEXT("#")) && Who.Mid(1).IsNumeric())
        {
            const int32 Index = FCString::Atoi(*Who.Mid(1));
            const auto& Entities = World.Entities();
            return Entities.IsValidIndex(Index) ? Entities[Index].Get() : nullptr;
        }
        FElysiumEntity* Dead = nullptr;
        for (const auto& Entity : World.Entities())
        {
            if (!Entity || !Entity->TargetName.Equals(Who, ESearchCase::IgnoreCase)) continue;
            if (!Entity->IsDead()) return Entity.Get();
            if (!Dead) Dead = Entity.Get();
        }
        return Dead;
    }
    FElysiumWeapon* Weapon(FElysiumEntityWorld& World, FElysiumEntity* Entity)
    {
        if (!Entity) return nullptr;
        if (Entity->AsItem()) return Entity->AsItem()->AsWeapon();
        const FElysiumCombatCharacter* Character = Entity->AsCombatCharacter();
        FElysiumEntity* Held = Character ? World.Resolve(Character->Inventory.ActiveWeapon) : nullptr;
        return Held && Held->AsItem() ? Held->AsItem()->AsWeapon() : nullptr;
    }
    double Axis(const FVector& Vector, const FString& Field)
    {
        return Field.EndsWith(TEXT(".x")) ? Vector.X : Field.EndsWith(TEXT(".y")) ? Vector.Y : Vector.Z;
    }
    bool ReadExtra(FElysiumEntityWorld& World, const FString& Who, const FString& Field,
        FElysiumArenaValue& Out, FString& Error)
    {
        FElysiumEntity* Entity = FindEntity(World, Who);
        FElysiumNpc* Npc = Entity ? Entity->AsNpc() : nullptr;
        if (Field.StartsWith(TEXT("origin.")) && Entity) { Out.Number = Axis(Entity->Origin, Field); return true; }
        if (Field == TEXT("life.dead") && Entity) { Out.bBool = Entity->IsDead(); return true; }
        if (Field == TEXT("life.state") && Entity) { Out.Number = Entity->LifeState; return true; }
        if (Field == TEXT("solid.type") && Entity) { Out.Number = Entity->GetSolid(); return true; }
        if (Field == TEXT("solid.flags") && Entity) { Out.Number = Entity->GetSolidFlags(); return true; }
        if (Field == TEXT("effects") && Npc) { Out.Number = Npc->EffectsWord; return true; }
        if (Field == TEXT("render.alpha") && Npc) { Out.Number = Npc->RenderAlphaByte; return true; }
        if (Field == TEXT("floor_drop.performed") && Npc) { Out.Number = Npc->FloorDropPerformed; return true; }
        if (Field == TEXT("floor_drop.skipped") && Npc) { Out.Number = Npc->FloorDropSkipped; return true; }
        if (Field == TEXT("outputs.remaining") && Entity)
        {
            if (Entity->OutputTimesRemaining.IsEmpty()) return Refuse(Field + TEXT(" (no output rows)"), Error);
            Out.Number = 0; for (int32 Remaining : Entity->OutputTimesRemaining) Out.Number += Remaining; return true;
        }
        if (Field == TEXT("senses.pass") && Npc) { Out.Number = static_cast<double>(Npc->Senses.CompletedPasses()); return true; }
        if (Field == TEXT("task.id") && Npc)
        {
            int32 TaskNumber;
            if (!Npc->CurrentRetailTaskNumber(TaskNumber)) return Refuse(Field + TEXT(" (no current retail task)"), Error);
            Out.Number = TaskNumber; return true;
        }
        if (Field.StartsWith(TEXT("weapon.")))
        {
            FElysiumWeapon* Held = Weapon(World, Entity);
            if (!Held) return Refuse(Field + TEXT(" (no equipped weapon)"), Error);
            if (Field == TEXT("weapon.reload")) Out.bBool = Held->bInReload;
            else if (Field == TEXT("weapon.clip")) Out.Number = Held->MagazineCount;
            else if (Field == TEXT("weapon.reserve"))
            {
                FElysiumEntity* Owner = World.Resolve(Held->Owner);
                if (!Held->Data() || !Owner || !Owner->AsCombatCharacter()) return Refuse(Field + TEXT(" (no reserve store)"), Error);
                Out.Number = Owner->AsCombatCharacter()->Inventory.Reserve(Held->Data()->AmmoType);
            }
            else if (Field == TEXT("weapon.jam")) Out.bBool = Held->bIsJammed;
            else if (Field == TEXT("weapon.interrupt")) Out.bBool = Held->bInterruptReload;
            else if (Field == TEXT("weapon.next_primary")) Out.Number = Held->NextPrimaryAttackTime;
            else if (Field == TEXT("weapon.next_secondary")) Out.Number = Held->NextSecondaryAttackTime;
            else if (Field == TEXT("weapon.idle")) Out.Number = Held->WeaponIdleTime;
            else if (Field == TEXT("weapon.owner")) Out.String = Held->Owner.ToString();
            else return Refuse(Field, Error);
            return true;
        }
        if (Field.StartsWith(TEXT("nav.")) && Npc)
        {
            const FElysiumNpcNavigator& Nav = Npc->Navigator; // 0x102ee1e0 saved semantic operands
            if (Field == TEXT("nav.type")) Out.Number = Nav.GetNavType();
            else if (Field == TEXT("nav.flags")) Out.Number = Nav.GetGoalFlags();
            else if (Field == TEXT("nav.arrival")) Out.Number = Nav.ArrivalActivity;
            else if (Field == TEXT("nav.retry_interval")) Out.Number = Nav.RouteRetryInterval;
            else if (Field == TEXT("nav.retry_duration")) Out.Number = Nav.RouteSearchTime;
            else if (Field == TEXT("nav.retry_next")) Out.Number = Nav.RouteRetryTime;
            else if (Field == TEXT("nav.timeout")) Out.Number = Nav.RouteGiveUpTime;
            else if (Field == TEXT("nav.target")) Out.String = Nav.GetTarget().ToString();
            else if (Field.StartsWith(TEXT("nav.goal."))) Out.Number = Axis(Nav.GetGoalPos(), Field);
            else return Refuse(Field, Error);
            return true;
        }
        if (Field.StartsWith(TEXT("place.destination.")) && Npc)
        { Out.Number = Axis(Npc->InterestingPlacePosition, Field); return true; }
        // No downcast based on a guessed classname: consult the real registered descriptor.
        FElysiumInterestingPlace* Place = Entity && Entity->Class && Entity->Class->ClassName == FName(TEXT("intersting_place"))
            ? static_cast<FElysiumInterestingPlace*>(Entity) : Npc ? Npc->CurrentAmbientSpot() : nullptr;
        if (Place)
        {
            if (Field == TEXT("place.identity")) Out.String = Place->Handle.ToString();
            else if (Field == TEXT("place.reservations")) Out.Number = static_cast<double>(Place->ReservationsObserved);
            else if (Field == TEXT("place.releases")) Out.Number = static_cast<double>(Place->ReleasesObserved);
            else if (Field == TEXT("place.capacity")) Out.Number = Place->MarkersAllocated;
            else if (Field == TEXT("place.count")) Out.Number = Place->MarkersUsed;
            else if (Field == TEXT("place.failed_attempts")) Out.Number = Place->FailedAttempts;
            else if (Field == TEXT("place.in_use")) Out.Number = Place->InUse;
            else if (Field == TEXT("place.ring_index")) Out.Number = Place->FailedBoxCursor;
            else if (Field.StartsWith(TEXT("place.bounds_min."))) Out.Number = Axis(Place->MinBoundsUnits, Field);
            else if (Field.StartsWith(TEXT("place.bounds_max."))) Out.Number = Axis(Place->MaxBoundsUnits, Field);
            else
            {
                TArray<FString> Parts; Field.ParseIntoArray(Parts, TEXT("."));
                if (Parts.Num() >= 4 && Parts[1] == TEXT("marker"))
                {
                    const int32 Row = FCString::Atoi(*Parts[2]);
                    if (Row < 0 || Row >= Place->MarkersUsed || !Place->Markers.IsValidIndex(Row)) return Refuse(Field + TEXT(" (no used marker row)"), Error);
                    if (Parts.Num() == 4 && Parts[3] == TEXT("occupant")) Out.String = Place->MarkerOccupant(Row).ToString();
                    else if (Parts.Num() == 5 && Parts[3] == TEXT("min")) Out.Number = Axis(Place->Markers[Row].MinBoundsCm, Field);
                    else if (Parts.Num() == 5 && Parts[3] == TEXT("max")) Out.Number = Axis(Place->Markers[Row].MaxBoundsCm, Field);
                    else return Refuse(Field, Error);
                }
                else if (Parts.Num() >= 4 && Parts[1] == TEXT("ring"))
                {
                    const int32 Row = FCString::Atoi(*Parts[2]);
                    if (Row < 0 || Row >= 4) return Refuse(Field, Error);
                    if (Parts.Num() == 4 && Parts[3] == TEXT("time")) Out.Number = Place->FailedBoxes[Row].Until;
                    else if (Parts.Num() == 5 && Parts[3] == TEXT("min")) Out.Number = Axis(Place->FailedBoxes[Row].MinBoundsCm, Field);
                    else if (Parts.Num() == 5 && Parts[3] == TEXT("max")) Out.Number = Axis(Place->FailedBoxes[Row].MaxBoundsCm, Field);
                    else return Refuse(Field, Error);
                }
                else return Refuse(Field, Error);
            }
            return true;
        }
        return Refuse(Field, Error);
    }
}
bool ElysiumArenaReadV6Witness(FElysiumEntityWorld& InWorld, const FString& Who, const FString& Field,
	FElysiumArenaValue& Out, FString& OutError)
{
	FElysiumEntityWorld* World = &InWorld;
	FElysiumArenaValue::EType RequiredType;
	if (!World || !ElysiumArenaScenario::WitnessType(Field, RequiredType))
	{ OutError = TEXT("no live world or unknown witness"); return false; }
	Out.Type = RequiredType;
	if (Field == TEXT("clock")) Out.Number = World->NowSeconds();
	else if (Field == TEXT("world_generation")) Out.Number = World->GetEpoch();
	else if (Field == TEXT("map")) Out.String = World->MapName();
	else if (Field.StartsWith(TEXT("coordinator.")))
	{
		const int32 CoordinatorIndex = Field.StartsWith(TEXT("coordinator.normal.")) ? 1 : Field.StartsWith(TEXT("coordinator.player.")) ? 2 : 3;
		const FElysiumAttackCoordinator* Coordinator = World->AttackCoordinator(CoordinatorIndex);
		if (!Coordinator) { OutError = TEXT("coordinator unavailable"); return false; }
		if (Field.EndsWith(TEXT(".count"))) Out.Number = Coordinator->Num();
		else if (Field.EndsWith(TEXT(".cap"))) Out.Number = Coordinator->Cap();
		else
		{
			TArray<int32> Members;
			for (const FElysiumEntityHandle& Member : Coordinator->Handles()) Members.Add(Member.Index);
			Members.Sort();
			TArray<FString> Identities;
			for (int32 MemberIndex : Members) Identities.Add(FString::Printf(TEXT("#%d"), MemberIndex));
			Out.String = FString::Join(Identities, TEXT(","));
		}
	}
	else
	{
		FElysiumEntity* Entity = ElysiumArenaV6Detail::FindEntity(*World, Who);
		FElysiumNpc* Npc = Entity ? Entity->AsNpc() : nullptr;
		if (Field == TEXT("identity") && Entity) Out.String = Entity->Handle.ToString();
		else if (Field == TEXT("hidden") && Entity) Out.bBool = Entity->IsHidden();
		else if (Field == TEXT("callback") && Entity) Out.String = Entity->ThinkCallback.ToString();
		else if (Field == TEXT("callback.saved") && Entity) Out.String = Entity->SavedThinkCallback.ToString();
		else if (Field == TEXT("think.next") && Entity) Out.Number = Entity->NextThink;
		else if (Field == TEXT("task.index") && Npc) Out.Number = Npc->Schedule.TaskIndex;
		else if (Field == TEXT("task.status") && Npc) Out.Number = static_cast<int32>(Npc->Schedule.TaskStatus);
		else if (Field == TEXT("task.started") && Npc) Out.Number = Npc->Schedule.ScheduleStartedAt;
		else if (Field == TEXT("task.task_started") && Npc) Out.Number = Npc->Schedule.TaskStartedAt;
		else if (Field == TEXT("task.failure") && Npc) Out.Number = Npc->BaseScheduleHost.FailureReason;
		else if (Field == TEXT("task.wait") && Npc) Out.Number = Npc->BaseScheduleHost.WaitFinished;
		else if (Field == TEXT("task.move_wait") && Npc) Out.Number = Npc->BaseScheduleHost.MoveWaitFinished;
		else if (Field == TEXT("senses.gathered") && Npc) Out.bBool = Npc->Cognition.GatheredAt >= 0.0;
		else if (Field == TEXT("script.owner") && Entity) Out.String = Entity->ScriptOwner.ToString();
		else if (Field == TEXT("anim.sequence") && Npc) Out.Number = Npc->SequenceNumber;
		else if (Field == TEXT("anim.cycle") && Npc) Out.Number = Npc->SequenceCycle;
		else if (Field == TEXT("anim.rate") && Npc) Out.Number = Npc->SequencePlaybackRate;
		else if (Field == TEXT("anim.time") && Npc) Out.Number = Npc->AnimTime;
		else if (Field == TEXT("anim.previous_time") && Npc) Out.Number = Npc->PrevAnimTime;
		else if (Field == TEXT("anim.last_event") && Npc) Out.Number = Npc->LastEventCheck;
		else if (Field == TEXT("anim.ground_speed") && Npc) Out.Number = Npc->GroundSpeed;
		else if (Field == TEXT("anim.yaw_speed") && Npc) Out.Number = Npc->YawSpeed;
		else if (Field == TEXT("anim.finished") && Npc) Out.bBool = Npc->bSequenceFinished;
		else if (Field == TEXT("anim.past_half") && Npc) Out.bBool = Npc->SequencePastHalf;
		else if (Field == TEXT("move_shoot.active") && Npc) Out.bBool = Npc->MoveAndShootOverlay.bMovingAndShooting;
		else if (Field == TEXT("move_shoot.next") && Npc) Out.Number = Npc->MoveAndShootOverlay.NextShotTime;
		else if (Field == TEXT("move_shoot.burst") && Npc) Out.Number = Npc->MoveAndShootOverlay.MoveShots;
		else if (Field == TEXT("move_shoot.min_burst") && Npc) Out.Number = Npc->MoveAndShootOverlay.MinBurst;
		else if (Field == TEXT("move_shoot.max_burst") && Npc) Out.Number = Npc->MoveAndShootOverlay.MaxBurst;
		else if (Field == TEXT("move_shoot.pause_min") && Npc) Out.Number = Npc->MoveAndShootOverlay.PauseMin;
		else if (Field == TEXT("move_shoot.pause_max") && Npc) Out.Number = Npc->MoveAndShootOverlay.PauseMax;
		else if (Field == TEXT("move_shoot.initial_delay") && Npc) Out.Number = Npc->MoveAndShootOverlay.InitialDelay;
		else if (Field == TEXT("damage.attacker") && Npc) Out.String = Npc->BaseMemory.LastDamageAttacker.ToString();
		else if (Field == TEXT("damage.sum") && Npc) Out.Number = Npc->BaseMemory.RepeatedDamageAccumulated;
		else if (Field == TEXT("damage.time") && Npc) Out.Number = Npc->BaseMemory.RepeatedDamageWindowStart;
		else if (Field.StartsWith(TEXT("damage.position.")) && Npc)
		{
			const FVector& Position = Npc->BaseMemory.LastDamageAttackPosition;
			Out.Number = Field.EndsWith(TEXT(".x")) ? Position.X : Field.EndsWith(TEXT(".y")) ? Position.Y : Position.Z;
		}
		else if (Field == TEXT("senses.can_sense") && Npc) Out.bBool = Npc->Senses.bCanPerformSenses;
		else if (Field == TEXT("senses.sighted") && Npc)
		{
			TArray<FString> Identities;
			for (const FElysiumEntityHandle& Sighted : Npc->Senses.Sighted()) Identities.Add(Sighted.ToString());
			Out.String = FString::Join(Identities, TEXT(","));
		}
		else if (Field == TEXT("los.player") && Npc) Out.bBool = Npc->Senses.Memory.bPlayerLos;
		else if (Field == TEXT("los.pvs") && Npc) Out.bBool = Npc->Senses.Memory.bPlayerInPvs;
		else if (Field == TEXT("los.cache") && Npc) Out.Number = Npc->Senses.Memory.PlayerLosNextUpdateTime;
		else if (Field == TEXT("los.last_clear") && Npc) Out.Number = Npc->Senses.Memory.PlayerLosLastClearTime;
		else if (Field == TEXT("dialog.partner") && Entity && Entity->AsCombatCharacter()) Out.String = Entity->AsCombatCharacter()->GetDialogPartner().ToString();
		else if (Field == TEXT("dialog.partner_live") && Entity && Entity->AsCombatCharacter()) Out.bBool = World->Resolve(Entity->AsCombatCharacter()->GetDialogPartner()) != nullptr;
		else if (Field.StartsWith(TEXT("memory.")) && Npc)
		{
			const FElysiumEntity* Enemy = Npc->GetEnemy();
			const FElysiumNpcEnemyMemoryRecord* Memory = Enemy ? Npc->EnemyMemory.Find(Enemy->Handle) : nullptr;
			if (Field == TEXT("memory.enemy")) Out.String = Enemy ? Enemy->Handle.ToString() : TEXT("#<null>");
			else if (!Memory) { OutError = TEXT("committed enemy has no memory record"); return false; }
			else if (Field == TEXT("memory.last_seen")) Out.Number = Memory->LastSeenTime;
			else Out.Number = Field.EndsWith(TEXT(".x")) ? Memory->LastPosition.X : Field.EndsWith(TEXT(".y")) ? Memory->LastPosition.Y : Memory->LastPosition.Z;
		}
		else if (Field.StartsWith(TEXT("layer.")) && Npc)
		{
			TArray<FString> Parts; Field.ParseIntoArray(Parts, TEXT("."));
			const int32 LayerIndex = FCString::Atoi(*Parts[1]);
			const FElysiumAnimatingOverlay::FAnimOverlayLayer& Layer = Npc->AnimOverlay[LayerIndex]; // four native records, 0x10098c80
			const FString& Word = Parts[2];
			if (Word == TEXT("flags")) Out.Number = Layer.Flags;
			else if (Word == TEXT("finished")) Out.Number = Layer.SequenceFinished;
			else if (Word == TEXT("sequence")) Out.Number = Layer.Sequence;
			else if (Word == TEXT("cycle")) Out.Number = Layer.Cycle;
			else if (Word == TEXT("rate")) Out.Number = Layer.PlaybackRate;
			else if (Word == TEXT("weight")) Out.Number = Layer.Weight;
			else if (Word == TEXT("weight_max")) Out.Number = Layer.WeightMax;
			else if (Word == TEXT("blend_in")) Out.Number = Layer.BlendIn;
			else if (Word == TEXT("blend_out")) Out.Number = Layer.BlendOut;
			else if (Word == TEXT("activity")) Out.Number = Layer.Activity;
			else if (Word == TEXT("auto_kill")) Out.bBool = Layer.bAutoKillWhenFinished;
			else if (Word == TEXT("last_event")) Out.Number = Layer.LastEventCheck;
		}
		else if (Field == TEXT("dialog.open")) Out.bBool = World->GetOpenDialog() != nullptr;
		else if (!ElysiumArenaV6Detail::ReadExtra(*World, Who, Field, Out, OutError))
		{ if (OutError.IsEmpty()) OutError = FString::Printf(TEXT("unavailable retail witness %s.%s"), *Who, *Field); return false; }
	}
	if (Out.Type != RequiredType || (Out.Type == FElysiumArenaValue::EType::Number && !FMath::IsFinite(Out.Number)))
	{ OutError = TEXT("witness adapter returned an incompatible type/value"); return false; }
	if (Field.StartsWith(TEXT("origin.")))
		if (FElysiumEntity* Observed = ElysiumArenaV6Detail::FindEntity(*World, Who))
			World->EmitAiTrace(*Observed, FName(TEXT("script")), FString::Printf(TEXT("witness %s=%s"), *Field, *Out.Describe()));
	return true;
}

// Narrow fixture setup, followed by the ordinary retail consumer. No fake successful source.
bool ElysiumArenaRunV6Fixture(FElysiumEntityWorld& World, const FElysiumArenaAction& Action, FString& Error)
{
    using namespace ElysiumArenaV6Detail;
    FElysiumEntity* Target = FindEntity(World, Action.Target);
    FElysiumNpc* Npc = Target ? Target->AsNpc() : nullptr;
    const FString Name = ElysiumArenaScenario::ActionName(Action.Do);
    if (Action.Do == EElysiumArenaAction::InvalidMarker)
        return Npc && Npc->StageInvalidMarker(Action.Control) ? true : Refuse(Name + TEXT(" (no real reservation)"), Error);
    if (Action.Do == EElysiumArenaAction::NoRagdollDeath)
    {
        if (!Npc || !Npc->IsAlive()) return Refuse(Name + TEXT(" (live NPC)"), Error);
        Npc->MiscFlags |= 0x80000u; // disclosed fixture, real HasMiscFlag consumer 0x1032c288
        return true;
    }
    if (Action.Do == EElysiumArenaAction::NpcSingleRoundFinishReload)
    {
        FElysiumWeapon* Held = Weapon(World, Target);
        FElysiumEntity* Owner = Held ? World.Resolve(Held->Owner) : nullptr;
        if (!Held || !Held->Data() || !Held->Data()->bReloadSingle || !Owner || !Owner->AsNpc())
            return Refuse(Name + TEXT(" (equipped NPC single-round weapon)"), Error);
        Held->bInReload = Held->bIsJammed = Held->bInterruptReload = true; // disclosed native-byte setup; slot322 NPC gate retains all three
        Held->FinishReload(); // 0x10255050: real retained-byte single-round arm
        return true;
    }
    if (Action.Do == EElysiumArenaAction::DamagePacket || Action.Do == EElysiumArenaAction::DamageMemory)
    {
        if (!Npc) return Refuse(Name + TEXT(" (NPC victim)"), Error);
        FElysiumEntity* Attacker = Action.Attacker.IsEmpty() || Action.Attacker == TEXT("none") ? nullptr : FindEntity(World, Action.Attacker);
        FElysiumEntity* Inflictor = Action.Inflictor.IsEmpty() || Action.Inflictor == TEXT("none") ? nullptr : FindEntity(World, Action.Inflictor);
        if ((!Action.Attacker.IsEmpty() && Action.Attacker != TEXT("none") && !Attacker)
            || (!Action.Inflictor.IsEmpty() && Action.Inflictor != TEXT("none") && !Inflictor))
            return Refuse(Name + TEXT(" (named attacker/inflictor)"), Error);
        if (Action.Param.Type != FElysiumArenaValue::EType::Number || Action.Param.Number < 0.0)
            return Refuse(Name + TEXT(".damage"), Error);
        if (Action.Do == EElysiumArenaAction::DamageMemory)
        {
            if (!Attacker) return Refuse(Name + TEXT(".attacker"), Error);
            if (Action.Control == TEXT("unknown_no_see"))
            {
                Npc->EnemyMemory.ClearMemory(Attacker->Handle);
                Npc->Cognition.Conditions.Clear(EElysiumNpcCond::SeeEnemy);
            }
            else if (Action.Control == TEXT("known"))
            {
                Npc->EnemyMemory.UpdateAtPosition(*Npc, Attacker->Handle, Attacker->Origin, World.NowSeconds());
                Npc->Cognition.Conditions.Clear(EElysiumNpcCond::SeeEnemy);
            }
            else if (Action.Control == TEXT("see_enemy")) Npc->Cognition.Conditions.Set(EElysiumNpcCond::SeeEnemy);
            else return Refuse(Name + TEXT(".") + Action.Control, Error);
        }
        FElysiumNpcBase::FElysiumTakeDamageInfo Packet;
        Packet.Attacker = Attacker ? Attacker->Handle : FElysiumEntityHandle::Invalid();
        Packet.Inflictor = Inflictor ? Inflictor->Handle : FElysiumEntityHandle::Invalid();
        Packet.Damage = static_cast<float>(Action.Param.Number);
        Npc->OnTakeDamage(&Packet); // scalar +0x28/+0x2c, 0x10265ed0
        World.EmitAiTrace(*Npc, FName(TEXT("script")), FString::Printf(TEXT("damage_record amount=%.6f light=%d heavy=%d repeated=%d sum=%.6f time=%.6f attacker=%s position=%s"),
            Packet.Damage, Npc->Cognition.Conditions.Has(EElysiumNpcCond::LightDamage) ? 1 : 0,
            Npc->Cognition.Conditions.Has(EElysiumNpcCond::HeavyDamage) ? 1 : 0, Npc->Cognition.Conditions.Has(EElysiumNpcCond::RepeatedDamage) ? 1 : 0,
            Npc->BaseMemory.RepeatedDamageAccumulated, Npc->BaseMemory.RepeatedDamageWindowStart, *Npc->BaseMemory.LastDamageAttacker.ToString(), *Npc->BaseMemory.LastDamageAttackPosition.ToString()));
        return true;
    }
    if (Action.Do == EElysiumArenaAction::ReserveSpot)
    {
        FElysiumEntity* Visitor = FindEntity(World, Action.Attacker);
        FElysiumNpc* VisitorNpc = Visitor ? Visitor->AsNpc() : nullptr;
        if (!Target || !Target->Class || Target->Class->ClassName != FName(TEXT("intersting_place")) || !VisitorNpc)
            return Refuse(Name + TEXT(" (place/NPC)"), Error);
        auto* Place = static_cast<FElysiumInterestingPlace*>(Target);
        const int32 Capacity = Place->MarkersAllocated;
        const int32 PriorFailed = Place->FailedAttempts, PriorUsed = Place->MarkersUsed, PriorRing = Place->FailedBoxCursor;
        if (Action.Control == TEXT("full")) Place->MarkersAllocated = Place->MarkersUsed;
        FVector Position;
        const bool bPicked = Place->PickSpotFor(*VisitorNpc, Position); // 0x102da0d0 real sampler
        Place->MarkersAllocated = Capacity;
        World.EmitAiTrace(*Place, FName(TEXT("script")), FString::Printf(TEXT("reserve_spot control=%s picked=%d count=%d failed=%d ring=%d destination=%s"),
            *Action.Control, bPicked ? 1 : 0, Place->MarkersUsed, Place->FailedAttempts, Place->FailedBoxCursor, *Position.ToString()));
        if (Action.Control == TEXT("occupied") && (bPicked || Place->FailedAttempts != PriorFailed + 1 || Place->MarkersUsed != PriorUsed))
            return Refuse(Name + TEXT(".occupied (staged region did not exhaust real overlap samples)"), Error);
        if (Action.Control == TEXT("exhausted_clearance") && (!bPicked || Place->MarkersUsed != PriorUsed + 1 || Place->FailedBoxCursor != PriorRing + 2))
            return Refuse(Name + TEXT(".exhausted_clearance (live stationary hull did not fail twice)"), Error);
        if (bPicked) Place->Claim(VisitorNpc->Handle); // 0x102da7c0 existing sampled row only
        return true; // the real consumer's refusal is the fixture observation, not adapter absence
    }
    if (Action.Do == EElysiumArenaAction::StartNpcGroundGate)
    {
        if (!Npc) return Refuse(Name + TEXT(" (NPC)"), Error);
        if (Action.Control == TEXT("fly")) Npc->SetMoveType(5, Npc->GetMoveCollide());
        else if (Action.Control == TEXT("swim")) Npc->SetMoveType(6, Npc->GetMoveCollide());
        else if (Action.Control == TEXT("capability4")) Npc->CapabilityWord |= 4;
        else if (Action.Control != TEXT("normal")) return Refuse(Name + TEXT(".") + Action.Control, Error);
        Npc->StartNPC(); // 0x10273ad0 real ground gate
        return true;
    }
    return Refuse(Name, Error);
}

bool ElysiumArenaValidateV6Admission(FElysiumEntityWorld& World, const FElysiumArenaScenario& Record, FString& Error)
{
    TSet<FString> Required;
    bool bNeedsSeek = false;
    for (const FElysiumArenaAction& Action : Record.Script)
    {
        for (const FElysiumArenaWitness& Word : Action.Fields)
        {
            if (Word.Field == TEXT("clock") || Word.Field == TEXT("map") || Word.Field == TEXT("world_generation") || Word.Field.StartsWith(TEXT("coordinator."))) continue;
            Required.Add(Word.Who);
            bNeedsSeek |= Word.Field.StartsWith(TEXT("anim.")) || Word.Field.StartsWith(TEXT("layer.")) || Word.Field.StartsWith(TEXT("move_shoot."));
        }
    }
    for (const auto& Initial : Record.InitialWeaponState) Required.Add(Initial.Who);
    for (const FString& Who : Required)
    {
        FElysiumEntity* Entity = ElysiumArenaV6Detail::FindEntity(World, Who);
        if (!Entity) return ElysiumArenaV6Detail::Refuse(TEXT("admission missing actor ") + Who, Error);
        FElysiumNpc* Npc = Entity->AsNpc();
        if (!Npc) continue; // place and relay need their real class, no skeletal requirement
        IElysiumEmbodiment* Embodiment = World.Embodiment();
        FElysiumNpcClip Clip; FString Label;
        const int32 NativeSequence = Npc->SequenceNumber == 0 ? 0 : Npc->SequenceRows.IsValidIndex(Npc->SequenceNumber) ? Npc->SequenceRows[Npc->SequenceNumber].RawIndex : INDEX_NONE;
        bool bClip = Embodiment && Npc->Visual && Embodiment->GetBodyClipByRawIndex(Npc->Visual, Npc->ModelStem(), NativeSequence, Label, Clip);
        if (!bClip && Embodiment && Npc->SequenceRows.IsValidIndex(Npc->SequenceNumber))
        {
            const auto& BridgeRow = Npc->SequenceRows[Npc->SequenceNumber];
            FElysiumSequenceDescriptor Descriptor;
            Label = BridgeRow.Label; Clip.Owner = BridgeRow.OwnerStem;
            bClip = !Label.IsEmpty() && Embodiment->GetNpcSequenceDescriptor(Npc->ModelStem(), BridgeRow.OwnerStem, Label, Descriptor);
        }
        World.EmitAiTrace(*Npc, FName(TEXT("script")), FString::Printf(TEXT("admission class=%s model=%s body=%s mesh=%s sequence=%d label=%s bank=%s frames=%d fps=%.3f rig=%d motor=%d seek=unavailable owner=0017/35"),
            *Npc->Def->Classname, *Npc->ModelStem(), Npc->Visual ? *Npc->Visual->GetPathName() : TEXT("absent"),
            Npc->Visual && Npc->Visual->GetSkeletalMeshAsset() ? *Npc->Visual->GetSkeletalMeshAsset()->GetPathName() : TEXT("absent"),
            NativeSequence, *Label, *Clip.Owner, Clip.Frames, Clip.Fps, Npc->HasClientRagdollRig() ? 1 : 0, Npc->GetNpcMotor() ? 1 : 0));
        if (!Npc->Visual || !Npc->GetNpcMotor() || (bNeedsSeek && !bClip)) return ElysiumArenaV6Detail::Refuse(TEXT("admission body/clip/motor ") + Who, Error);
    }
    if (bNeedsSeek && !FElysiumNpcBase::RestoreNativeAnimationAdapter)
        return ElysiumArenaV6Detail::Refuse(TEXT("native presentation continuation (event-free body seek; 0017/35)"), Error);
    return true;
}

bool ElysiumArenaCorruptV6Header(FElysiumMapSnapshot& Snapshot, const FString& Control, FString& Error)
{
    using namespace ElysiumArenaV6Detail;
    if (Control != TEXT("crc") && Control != TEXT("missing_cine") && Control != TEXT("missing_target") && Control != TEXT("missing_path"))
        return Refuse(TEXT("corrupt_checkpoint.") + Control, Error);
    bool bChanged = false;
    auto& Registry = FElysiumClassRegistry::Get();
    for (FElysiumEntityState& Record : Snapshot.Entities)
    {
        const FElysiumClassDesc* Desc = Registry.Find(Record.ClassName);
        while (Desc && Desc->ClassName != FName(TEXT("CAI_BaseNPC"))) Desc = Registry.Find(Desc->BaseName);
        if (!Desc || Record.LeafState.IsEmpty()) continue;
        FMemoryReader Reader(Record.LeafState, true);
        FElysiumSaveArchive Ar(Reader, Snapshot.SchemaVersion, Snapshot.SaveBase, Snapshot.SaveBase);
        int16 Version = 0; uint32 Flags = 0; FString ScheduleName;
        Ar << Version << Flags << ScheduleName;
        const int64 CrcOffset = Reader.Tell();
        uint32 Crc = 0; Ar << Crc;
        if (Ar.IsError() || Version != 1) return Refuse(TEXT("corrupt_checkpoint.crc (invalid native header)"), Error);
        if (ScheduleName.IsEmpty()) continue;
        const int64 OldHeaderEnd = Reader.Tell();
        if (Control != TEXT("crc"))
        {
            // Decode the exact native leaf with its real codec; retain the original header flags/CRC.
            // Setup changes only a prerequisite. The common applier and 0x1027bf50 choose give-up.
            FElysiumEntityDef FixtureDef; FixtureDef.Classname = Record.ClassName.ToString();
            FixtureDef.TargetName = Record.TargetName;
            auto Decoded = Registry.Create(FixtureDef, FElysiumEntityHandle(Record.Index, 0));
            FElysiumNpcBase* Base = Decoded ? Decoded->AsNpcBase() : nullptr;
            if (!Base) continue;
            FMemoryReader LeafReader(Record.LeafState, true);
            FElysiumSaveArchive LeafRead(LeafReader, Snapshot.SchemaVersion, Snapshot.SaveBase, Snapshot.SaveBase);
            Base->Serialize(LeafRead);
            if (LeafRead.IsError()) return Refuse(TEXT("corrupt_checkpoint native leaf decode"), Error);
            if (Control == TEXT("missing_cine"))
            {
                if (!Base->ScriptOwner.IsSet()) continue;
                Snapshot.AbsentEntities.AddUnique(Base->ScriptOwner.Index); // normal missing-identity fixup fence
                Base->ScriptOwner = FElysiumEntityHandle::Invalid(); // SCRIPT/live cine prerequisite, 0x1027bf50
            }
            else if (Control == TEXT("missing_target"))
            {
                if ((Flags & 2u) == 0) continue;
                Base->SetTarget(FElysiumEntityHandle::Invalid()); // header still requires +0x5ce4
            }
            else
            {
                if ((Flags & 4u) == 0) continue;
                Base->Navigator.GoalType = MAX_int32; // no semantic dispatch arm, real 0x102f2330 returns false
            }
            TArray<uint8> Written;
            FMemoryWriter LeafWriter(Written, true);
            FElysiumSaveArchive LeafWrite(LeafWriter, Snapshot.SchemaVersion, Snapshot.SaveBase, Snapshot.SaveBase);
            Base->Serialize(LeafWrite);
            if (LeafWrite.IsError()) return Refuse(TEXT("corrupt_checkpoint native leaf encode"), Error);
            FMemoryReader NewHeaderReader(Written, true);
            FElysiumSaveArchive NewHeader(NewHeaderReader, Snapshot.SchemaVersion, Snapshot.SaveBase, Snapshot.SaveBase);
            int16 NewVersion = 0; uint32 NewFlags = 0, NewCrc = 0; FString NewName;
            NewHeader << NewVersion << NewFlags << NewName << NewCrc;
            if (NewHeader.IsError()) return Refuse(TEXT("corrupt_checkpoint encoded header"), Error);
            TArray<uint8> Mutated;
            Mutated.Append(Record.LeafState.GetData(), static_cast<int32>(OldHeaderEnd));
            Mutated.Append(Written.GetData() + NewHeaderReader.Tell(), Written.Num() - static_cast<int32>(NewHeaderReader.Tell()));
            Record.LeafState = MoveTemp(Mutated);
            bChanged = true;
            continue;
        }
        FMemoryWriter Writer(Record.LeafState, true);
        Writer.Seek(CrcOffset);
        FElysiumSaveArchive Mutation(Writer, Snapshot.SchemaVersion, Snapshot.SaveBase, Snapshot.SaveBase);
        Crc ^= 1u; Mutation << Crc; // 0x1027bf50 normal CRC consumer, decoded snapshot only
        if (Mutation.IsError()) return Refuse(TEXT("corrupt_checkpoint.crc (header writer)"), Error);
        bChanged = true;
    }
    return bChanged ? true : Refuse(TEXT("corrupt_checkpoint.") + Control + TEXT(" (no saved required prerequisite)"), Error);
}

void ElysiumArenaInstallV6Adapters()
{
    ElysiumArenaStage::SetHostAdapter([](ElysiumArenaStage::FHost& Host)
    {
        Host.ReadWitness = ElysiumArenaReadV6Witness;
        Host.RunFixture = ElysiumArenaRunV6Fixture;
        Host.ValidateWitnessAdmission = ElysiumArenaValidateV6Admission;
        Host.Transport->MutateCheckpoint = ElysiumArenaCorruptV6Header;
    });
}
#endif
