
void UGameEngine::Tick( float DeltaSeconds, bool bIdleMode )
{
	for (int32 WorldIdx = 0; WorldIdx < WorldList.Num(); ++WorldIdx)
	{
		FWorldContext &Context = WorldList[WorldIdx];
		Context.World()->Tick( LEVELTICK_All, DeltaSeconds );
	}
}

void UWorld::Tick( ELevelTick TickType, float DeltaSeconds )
{
	assume(NetDriver == nullptr || NetDriver->ServerConnection == nullptr);
	assume(IsPaused() == false);
	assume(TickType == LEVELTICK_All);

	FDrawEvent* TickDrawEvent = BeginTickDrawEvent();

	FWorldDelegates::OnWorldTickStart.Broadcast(this, TickType, DeltaSeconds);

	SCOPE_CYCLE_COUNTER(STAT_WorldTickTime);

	AWorldSettings* Info = GetWorldSettings();
	FMemMark Mark(FMemStack::Get());
	GInitRunaway();
	bInTick=true;

	RealTimeSeconds += DeltaSeconds;
	AudioTimeSeconds += DeltaSeconds;
	float RealDeltaSeconds = DeltaSeconds;
	DeltaSeconds *= Info->GetEffectiveTimeDilation();

	const float GameDeltaSeconds = Info->FixupDeltaSeconds(DeltaSeconds, RealDeltaSeconds);
	check(GameDeltaSeconds >= 0.0f);

	DeltaSeconds = GameDeltaSeconds;
	DeltaTimeSeconds = DeltaSeconds;

	UnpausedTimeSeconds += DeltaSeconds;

	TimeSeconds += DeltaSeconds;

	if( bPlayersOnly )
	{
		TickType = LEVELTICK_ViewportsOnly;
	}
	
	// update world's subsystems (NavigationSystem for now)
	if (NavigationSystem != nullptr)
	{
		SCOPE_CYCLE_COUNTER(STAT_NavWorldTickTime);
		NavigationSystem->Tick(DeltaSeconds);
	}

	bool bDoingActorTicks = TickType!=LEVELTICK_TimeOnly;

	FLatentActionManager& CurrentLatentActionManager = GetLatentActionManager();

	CurrentLatentActionManager.BeginFrame();
	
	if (bDoingActorTicks)
	{
		{
			SCOPE_CYCLE_COUNTER(STAT_ResetAsyncTraceTickTime);
			ResetAsyncTrace();
		}
		{
			SCOPE_CYCLE_COUNTER(STAT_TickTime);
			FWorldDelegates::OnWorldPreActorTick.Broadcast(this, TickType, DeltaSeconds);
		}

		// Tick level sequence actors first
		MovieSceneSequenceTick.Broadcast(DeltaSeconds);
	}

	for (int32 i = 0; i < LevelCollections.Num(); ++i)
	{
		// Build a list of levels from the collection that are also in the world's Levels array.
		// Collections may contain levels that aren't loaded in the world at the moment.
		TArray<ULevel*> LevelsToTick;
		for (ULevel* CollectionLevel : LevelCollections[i].GetLevels())
		{
			if (Levels.Contains(CollectionLevel))
			{
				LevelsToTick.Add(CollectionLevel);
			}
		}

		FScopedLevelCollectionContextSwitch LevelContext(i, this);

		if (bDoingActorTicks)
		{
			SCOPE_CYCLE_COUNTER(STAT_TickTime);
			
			SetupPhysicsTickFunctions(DeltaSeconds);
			TickGroup = TG_PrePhysics; // reset this to the start tick group
			FTickTaskManagerInterface::Get().StartFrame(this, DeltaSeconds, TickType, LevelsToTick);

			{
				SCOPE_CYCLE_COUNTER(STAT_TG_PrePhysics);
				RunTickGroup(TG_PrePhysics);
			}
			
			bInTick = false;
			EnsureCollisionTreeIsBuilt();
			bInTick = true;
			
			{
				SCOPE_CYCLE_COUNTER(STAT_TG_StartPhysics);
				RunTickGroup(TG_StartPhysics);
			}
			{
				SCOPE_CYCLE_COUNTER(STAT_TG_DuringPhysics);
				RunTickGroup(TG_DuringPhysics, false); // No wait here, we should run until idle though. We don't care if all of the async ticks are done before we start running post-phys stuff
			}
			
			TickGroup = TG_EndPhysics; // set this here so the current tick group is correct during collision notifies, though I am not sure it matters. 'cause of the false up there^^^
			
			{
				SCOPE_CYCLE_COUNTER(STAT_TG_EndPhysics);
				RunTickGroup(TG_EndPhysics);
			}
			{
				SCOPE_CYCLE_COUNTER(STAT_TG_PostPhysics);
				RunTickGroup(TG_PostPhysics);
			}
	
		}
	
		// We only want to run the following once, so only run it for the source level collection.
		if (LevelCollections[i].GetType() == ELevelCollectionType::DynamicSourceLevels)
		{
			// Process any remaining latent actions
			CurrentLatentActionManager.ProcessLatentActions(nullptr, DeltaSeconds);
			
			{
				SCOPE_CYCLE_COUNTER(STAT_TickableTickTime);
				GetTimerManager().Tick(DeltaSeconds);
				FTickableGameObject::TickObjects(this, TickType, bIsPaused, DeltaSeconds);
			}

			// Update cameras and streaming volumes
			{
				SCOPE_CYCLE_COUNTER(STAT_UpdateCameraTime);
				// Update cameras last. This needs to be done before NetUpdates, and after all actors have been ticked.
				for( FConstPlayerControllerIterator Iterator = GetPlayerControllerIterator(); Iterator; ++Iterator )
				{
					if (APlayerController* PlayerController = Iterator->Get())
					{
						PlayerController->UpdateCameraManager(DeltaSeconds);
					}
				}

				// Issues level streaming load/unload requests based on local players being inside/outside level streaming volumes.
				if (IsGameWorld())
				{
					ProcessLevelStreamingVolumes();

					if (WorldComposition)
					{
						WorldComposition->UpdateStreamingState();
					}
				}
			}
		}

		if (bDoingActorTicks)
		{
			SCOPE_CYCLE_COUNTER(STAT_TickTime);
			{
				SCOPE_CYCLE_COUNTER(STAT_TG_PostUpdateWork);
				RunTickGroup(TG_PostUpdateWork);
			}
			{
				SCOPE_CYCLE_COUNTER(STAT_TG_LastDemotable);
				RunTickGroup(TG_LastDemotable);
			}

			FTickTaskManagerInterface::Get().EndFrame();
		}
	}

	if (bDoingActorTicks)
	{
		SCOPE_CYCLE_COUNTER(STAT_TickTime);
		FWorldDelegates::OnWorldPostActorTick.Broadcast(this, TickType, DeltaSeconds);
		
		if ( PhysicsScene != nullptr )
		{
			GPhysCommandHandler->Flush();
		}
		
		{
			SCOPE_CYCLE_COUNTER(STAT_FinishAsyncTraceTickTime);
			FinishAsyncTrace();
		}
	}

	bInTick = false;
	Mark.Pop();

	GEngine->ConditionalCollectGarbage();
	ViewLocationsRenderedLastFrame.Reset();
	EndTickDrawEvent(TickDrawEvent);
}


class FTickTaskManager : public FTickTaskManagerInterface {
	FTickTaskSequencer&	TickTaskSequencer;/** Global Sequencer */
	FTickContext Context;/** tick context **/
	bool bTickNewlySpawned;/** true during the tick phase, when true, tick function adds also go to the newly spawned list. **/
	
	TArray<FTickTaskLevel*>	LevelList;/** List of current levels **/
};

void FTickTaskManager::StartFrame(UWorld* InWorld, float InDeltaSeconds, ELevelTick InTickType, const TArray<ULevel*>& LevelsToTick) override
{
	Context.TickGroup = ETickingGroup(0); // reset this to the start tick group
	Context.DeltaSeconds = InDeltaSeconds;
	Context.TickType = InTickType;
	Context.Thread = ENamedThreads::GameThread;
	Context.World = InWorld;
	bTickNewlySpawned = true;
	TickTaskSequencer.StartFrame();
	FillLevelList(LevelsToTick);

	for( int32 LevelIndex = 0; LevelIndex < LevelList.Num(); LevelIndex++ )
	{
		LevelList[LevelIndex]->StartFrame(Context);
	}
	for( int32 LevelIndex = 0; LevelIndex < LevelList.Num(); LevelIndex++ )
	{
		LevelList[LevelIndex]->QueueAllTicks();
	}
}

void FTickTaskManager::FillLevelList(const TArray<ULevel*>& Levels)
{
	check(LevelList.Num() == 0);

	if (!Context.World->GetActiveLevelCollection() || Context.World->GetActiveLevelCollection()->GetType() == ELevelCollectionType::DynamicSourceLevels)
	{
		LevelList.Add(Context.World->TickTaskLevel);
	}

	for( ULevel* Level : Levels )
	{
		if (Level->bIsVisible)
		{
			LevelList.Add(Level->TickTaskLevel);
		}
	}
}

class ENGINE_API UWorld final : public UObject, public FNetworkNotify {
	/** Data structures for holding the tick functions that are associated with the world (line batcher, etc) **/
	FTickTaskLevel* TickTaskLevel;
}
class ULevel : public UObject, public IInterface_AssetUserData {
	/** Data structures for holding the tick functions **/
	FTickTaskLevel* TickTaskLevel;
}

class FTickTaskLevel {
	FTickTaskSequencer& TickTaskSequencer;/** Global Sequencer */
	FTickContext Context;/** tick context **/
	bool bTickNewlySpawned;/** true during the tick phase, when true, tick function adds also go to the newly spawned list. **/
	
	TSet<FTickFunction*> AllEnabledTickFunctions;/** Master list of enabled tick functions **/
	FCoolingDownTickFunctionList AllCoolingDownTickFunctions;/** Master list of enabled tick functions **/
	TSet<FTickFunction*> AllDisabledTickFunctions;/** Master list of disabled tick functions **/	
	TArrayWithThreadsafeAdd<FTickScheduleDetails> TickFunctionsToReschedule;/** Utility array to avoid memory reallocations when collecting functions to reschedule **/	
	TSet<FTickFunction*> NewlySpawnedTickFunctions;/** List of tick functions added during a tick phase; these items are also duplicated in AllLiveTickFunctions for future frames **/
};

void FTickTaskLevel::StartFrame(const FTickContext& InContext)
{
	check(NewlySpawnedTickFunctions.Num() == 0); // There shouldn't be any in here at this point in the frame
	Context.TickGroup = ETickingGroup(0); // reset this to the start tick group
	Context.DeltaSeconds = InContext.DeltaSeconds;
	Context.TickType = InContext.TickType;
	Context.Thread = ENamedThreads::GameThread;
	Context.World = InContext.World;
	bTickNewlySpawned = true;

	// Make sure all scheduled Tick Functions that are ready are put into the cooling down state
	ScheduleTickFunctionCooldowns();

	// Determine which cooled down ticks will be enabled this frame
	float CumulativeCooldown = 0.f;
	FTickFunction* TickFunction = AllCoolingDownTickFunctions.Head;
	while (TickFunction)
	{
		if (CumulativeCooldown + TickFunction->InternalData->RelativeTickCooldown >= Context.DeltaSeconds)
		{
			TickFunction->InternalData->RelativeTickCooldown -= (Context.DeltaSeconds - CumulativeCooldown);
			break;
		}
		CumulativeCooldown += TickFunction->InternalData->RelativeTickCooldown;

		TickFunction->TickState = FTickFunction::ETickState::Enabled;
		TickFunction = TickFunction->InternalData->Next;
	}
}


void UWorld::RunTickGroup(ETickingGroup Group, bool bBlockTillComplete = true)
{
	FTickTaskManagerInterface::Get().RunTickGroup(Group, bBlockTillComplete);
	TickGroup = ETickingGroup(TickGroup + 1);
}

void FTickTaskManager::RunTickGroup(ETickingGroup Group, bool bBlockTillComplete )
{
	check(Context.TickGroup == Group);
	check(bTickNewlySpawned); // we should be in the middle of ticking
	TickTaskSequencer.ReleaseTickGroup(Group, bBlockTillComplete);
	Context.TickGroup = ETickingGroup(Context.TickGroup + 1); // new actors go into the next tick group because this one is already gone
	if (bBlockTillComplete) // we don't deal with newly spawned ticks within the async tick group, they wait until after the async stuff
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_TickTask_RunTickGroup_BlockTillComplete);

		bool bFinished = false;
		for (int32 Iterations = 0;Iterations < 101; Iterations++)
		{
			int32 Num = 0;
			for( int32 LevelIndex = 0; LevelIndex < LevelList.Num(); LevelIndex++ )
			{
				Num += LevelList[LevelIndex]->QueueNewlySpawned(Context.TickGroup);
			}
			if (Num && Context.TickGroup == TG_NewlySpawned)
			{
				SCOPE_CYCLE_COUNTER(STAT_TG_NewlySpawned);
				TickTaskSequencer.ReleaseTickGroup(TG_NewlySpawned, true);
			}
			else
			{
				bFinished = true;
				break;
			}
		}
		if (!bFinished)
		{
			// this is runaway recursive spawning.
			for( int32 LevelIndex = 0; LevelIndex < LevelList.Num(); LevelIndex++ )
			{
				LevelList[LevelIndex]->LogAndDiscardRunawayNewlySpawned(Context.TickGroup);
			}
		}
	}
}
