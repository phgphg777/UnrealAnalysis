// Ctrl + C, Ctrl + V
UObject* ULevelFactory::FactoryCreateText(...)
{
	AActor* NewActor = World->SpawnActor( TempClass, nullptr, nullptr, SpawnInfo );
	{
		AActor* const Actor = NewObject<AActor>(LevelToSpawnIn, Class, NewActorName, SpawnParameters.ObjectFlags, Template);
		Actor->PostSpawnInitialize(UserTransform, SpawnParameters.Owner, SpawnParameters.Instigator, SpawnParameters.IsRemoteOwned(), SpawnParameters.bNoFail, SpawnParameters.bDeferConstruction);
		{
			RegisterAllComponents();
		}
	}

	if ( Actor->ShouldImport(&PropText, bIsMoveToStreamingLevel) )
	{
		Actor->PreEditChange(nullptr);
		ImportObjectProperties( (uint8*)Actor, *PropText, Actor->GetClass(), Actor, Actor, Warn, 0, INDEX_NONE, NULL, &ExistingToNewMap );
		{
			RegisterAllComponents();
		}
		bActorChanged = true;
	}

	if( bActorChanged )
	{
		Actor->PostEditChange();
		{
			RegisterAllComponents();
		}
	}
}

void AActor::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	if (CurrentTransactionAnnotation.IsValid())
	{
		...
	}
	else
	{
		UnregisterAllComponents();
		RerunConstructionScripts();
		ReregisterAllComponents();
		{
			RegisterAllComponents();
		}
	}
}

void UActorComponent::ExecuteUnregisterEvents()
{
	DestroyPhysicsState();

	if(bRenderStateCreated)
	{
		SCOPE_CYCLE_COUNTER(STAT_ComponentDestroyRenderState);
		DestroyRenderState_Concurrent();
		checkf(!bRenderStateCreated);
	}

	if(bRegistered)
	{
		SCOPE_CYCLE_COUNTER(STAT_ComponentOnUnregister);
		OnUnregister();
		checkf(!bRegistered);
	}
}

void UParticleSystemComponent::DestroyRenderState_Concurrent()
{
	SCOPE_CYCLE_COUNTER(STAT_ParticleSystemComponent_DestroyRenderState_Concurrent);
	SCOPE_CYCLE_COUNTER(STAT_ParticlesOverview_GT_CNC);
	ForceAsyncWorkCompletion(ENSURE_AND_STALL, false, true);
	if (bResetOnDetach) ResetParticles();// Empty the EmitterInstance array.
	if (bRenderStateCreated) Super::DestroyRenderState_Concurrent(); {SceneProxy = NULL;}
}

void UParticleSystemComponent::OnUnregister()
{
	ForceAsyncWorkCompletion(STALL);
	bWasActive = IsActive() && !bWasDeactivated;
	SetComponentTickEnabled(false);
	ResetParticles(!bAllowRecycling);
	FXSystem = NULL;
	Super::OnUnregister();
}

void AActor::RegisterAllComponents()
{
	IncrementalRegisterComponents();
	{
		for (UActorComponent* Component : GetComponents())
		{
			if (!Component->IsRegistered() && Component->bAutoRegister && !Component->IsPendingKill())
			{
				Component->Modify(false);// Before we register our component, save it to our transaction buffer so if "undone" it will return to an unregistered state.
				Component->RegisterComponentWithWorld(World, Context);
				{
					ExecuteRegisterEvents(Context);
					{
						if(!bRegistered)
						{
							SCOPE_CYCLE_COUNTER(STAT_ComponentOnRegister);
							OnRegister();
							check(bRegistered);
						}

						if(!bRenderStateCreated && WorldPrivate->Scene)
						{
							SCOPE_CYCLE_COUNTER(STAT_ComponentCreateRenderState);
							CreateRenderState_Concurrent(Context);
							check(bRenderStateCreated);
						}

						CreatePhysicsState();
					}
				}
			}
		}
	}
}
//////////////////////////////////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////////////////////////////////
class ENGINE_API UParticleSystemComponent : public UFXSystemComponent {
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=Particles)
	UParticleSystem* Template;

	FFXSystem* FXSystem;
	TArray<FParticleEmitterInstance*> EmitterInstances;
}


void UParticleSystemComponent::OnRegister()
{
	ForceAsyncWorkCompletion(STALL);
	check(FXSystem == nullptr);

	UWorld* World = GetWorld();
	if (World->Scene)
	{
		FFXSystemInterface*  FXSystemInterface = World->Scene->GetFXSystem();
		if (FXSystemInterface)
		{
			FXSystem = static_cast<FFXSystem*>(FXSystemInterface->GetInterface(FFXSystem::Name));
		}
	}

	Super::OnRegister();
	{
		bRegistered = true;
		if (bAutoActivate) Activate(true);
	}

	if (bWasActive && !IsActive())// If we were active before but are not now, activate us
	{
		Activate(true);
	}
}


void UParticleSystemComponent::Activate(bool bReset) 
{
	if (Template)
	{
		bDeactivateTriggered = false;

		if (bReset || ShouldActivate()==true)
		{
			ActivateSystem(bReset);
		}
	}
}

void UParticleSystemComponent::ActivateSystem(bool bFlagAsJustAttached)
{
	SCOPE_CYCLE_COUNTER(STAT_ParticleActivateTime);
	SCOPE_CYCLE_COUNTER(STAT_ParticlesOverview_GT);
	ForceAsyncWorkCompletion(STALL);

	bOldPositionValid = false;
	OldPosition = FVector::ZeroVector;
	PartSysVelocity = FVector::ZeroVector;

	UWorld* World = GetWorld();
	const bool bIsGameWorld = World->IsGameWorld();

	assume(Template);
	{
		const bool bWasAutoAttached = bDidAutoAttach;
		bDidAutoAttach = false;
		assume(!bAutoManageAttachment);
		AccumTickTime = 0.0;

		if (!IsActive())
		{
			LastSignificantTime = World->GetTimeSeconds();
			RequiredSignificance = EParticleSignificanceLevel::Low;
			OnSystemPreActivationChange.Broadcast(this, true);//Call this now after any attachment has happened.
		}

		if (bFlagAsJustAttached)
		{
			bJustRegistered = true;
		}
	
		bSuppressSpawning = false;// Stop suppressing particle spawning.
		bool bNeedToUpdateTransform = bWasDeactivated;// Set the system as active
		bWasCompleted = false;
		bWasDeactivated = false;
		SetActiveFlag(true);
		bWasActive = false; // Set to false now, it may get set to true when it's deactivated due to unregister
		SetComponentTickEnabled(true);

		// if no instances, or recycling
		if (EmitterInstances.Num() == 0 || (bIsGameWorld && (!bAutoActivate || bHasBeenActivated)))
		{
			InitializeSystem();
		}
		else if (EmitterInstances.Num() > 0 && !bIsGameWorld)
		{
			...
		}

		bHasBeenActivated = true;// Flag the system as having been activated at least once
		TimeSinceLastTick = 0;// Clear tick time

		int32 DesiredLODLevel = 0;
		bool bCalculateLODLevel = 
			(bOverrideLODMethod == true) ? (LODMethod != PARTICLESYSTEMLODMETHOD_DirectSet) : 
				(Template ? (Template->LODMethod != PARTICLESYSTEMLODMETHOD_DirectSet) : false);

		if (bCalculateLODLevel)
		{
			FVector EffectPosition = GetComponentLocation();
			DesiredLODLevel = DetermineLODLevelForLocation(EffectPosition);
			if (DesiredLODLevel != LODLevel)
			{
				SetActiveFlag(true);
				SetComponentTickEnabled(true);
			}
			SetLODLevel(DesiredLODLevel);
		}

		assume(WarmupTime == 0.0f);
		assume(!bIsManagingSignificance);
	}

	MarkRenderStateDirty();// Mark render state dirty to ensure the scene proxy is added and registered with the scene.

	if(!bWasDeactivated && !bWasCompleted)
	{
		SetLastRenderTime(GetWorld()->GetTimeSeconds());
	}
}

void UParticleSystemComponent::InitializeSystem()
{
	SCOPE_CYCLE_COUNTER(STAT_ParticleInitializeTime);
	ForceAsyncWorkCompletion(STALL);

	assume(IsRegistered() && FXSystem != NULL);
	assume(!IsTemplate()); // ?
	assume(Template);

	EmitterDelay = !Template->bUseDelayRange ? Template->Delay
		: Template->DelayLow + ((Template->Delay - Template->DelayLow) * RandomStream.FRand());

	InitParticles();// Allocate the emitter instances and particle data

	if (IsRegistered())
	{
		AccumTickTime = 0.0;
		if (!IsActive() && bAutoActivate && !bWasDeactivated)
		{
			SetActive(true);
		}
	}
}

void UParticleSystemComponent::InitParticles()
{
	SCOPE_CYCLE_COUNTER(STAT_ParticleSystemComponent_InitParticles);

	ForceAsyncWorkCompletion(ENSURE_AND_STALL);
	
	WarmupTime = Template->WarmupTime;
	WarmupTickRate = Template->WarmupTickRate;
	bIsViewRelevanceDirty = true;

	//simplified version.
	int32 NumInstances = EmitterInstances.Num();
	int32 NumEmitters = Template->Emitters.Num();
	const bool bIsFirstCreate = NumInstances == 0;
	EmitterInstances.SetNumZeroed(NumEmitters);

	bWasCompleted = bIsFirstCreate ? false : bWasCompleted;

	for (int32 Idx = 0; Idx < NumEmitters; Idx++)
	{
		UParticleEmitter* Emitter = Template->Emitters[Idx];
		if (Emitter)
		{
			FParticleEmitterInstance* Instance = EmitterInstances[Idx];
			check(GlobalDetailMode < NUM_DETAILMODE_FLAGS);
			assume(bShouldCreateAndOrInit);
		
			if (Instance)
			{
				Instance->SetHaltSpawning(false);
				Instance->SetHaltSpawningExternal(false);
			}
			else
			{
				Instance = Emitter->CreateInstance(this);
				EmitterInstances[Idx] = Instance;
			}
			assume(Instance);

			Instance->bEnabled = true;
			Instance->InitParameters(Emitter, this);// repeated call!
			Instance->Init(); 						// repeated call!
		}
	}

	assume(!bSetLodLevels);
}

class UParticleEmitter : public UObject {
}
class UParticleSpriteEmitter : public UParticleEmitter {
};

struct ENGINE_API FParticleEmitterInstance {
}
struct FParticleSpriteEmitterInstance : public FParticleEmitterInstance {
};

FParticleEmitterInstance* UParticleSpriteEmitter::CreateInstance(UParticleSystemComponent* InComponent)
{
	assume(!bCookedOut && LODLevels.Num() > 0);
	assume(!GetLODLevel(0)->TypeDataModule);

	FParticleEmitterInstance* Instance = new FParticleSpriteEmitterInstance();
	Instance->InitParameters(this, InComponent);
	Instance->CurrentLODLevelIndex = 0;
	Instance->CurrentLODLevel = LODLevels[0];
	Instance->Init();

	return Instance;
}

void FParticleEmitterInstance::InitParameters(UParticleEmitter* InTemplate, UParticleSystemComponent* InComponent)
{
	SCOPE_CYCLE_COUNTER(STAT_ParticleEmitterInstance_InitParameters);
	SpriteTemplate = InTemplate;
	Component = InComponent;
	SetupEmitterDuration();
}

void FParticleEmitterInstance::SetupEmitterDuration()
{
	assume(SpriteTemplate);

	if (EmitterDurations.Num() != SpriteTemplate->LODLevels.Num())
	{
		EmitterDurations.Empty();
		EmitterDurations.InsertUninitialized(0, SpriteTemplate->LODLevels.Num());
	}

	// Calculate the duration for each LOD level
	for (UParticleLODLevel* TempLOD : SpriteTemplate->LODLevels)
	{
		UParticleModuleRequired* RequiredModule = TempLOD->RequiredModule;

		FRandomStream& RandomStream = RequiredModule->GetRandomStream(this);

		CurrentDelay = RequiredModule->EmitterDelay + Component->EmitterDelay;

		assume(!RequiredModule->bEmitterDelayUseRange);
		assume(!RequiredModule->bEmitterDurationUseRange);
		
		EmitterDurations[TempLOD->Level] = RequiredModule->EmitterDuration + CurrentDelay;
		
		if ((LoopCount == 1) && (RequiredModule->bDelayFirstLoopOnly == true) && 
			((RequiredModule->EmitterLoops == 0) || (RequiredModule->EmitterLoops > 1)))
		{
			EmitterDurations[TempLOD->Level] -= CurrentDelay;
		}
	}

	EmitterDuration	= EmitterDurations[CurrentLODLevelIndex];// Set the current duration
}

void FParticleEmitterInstance::Init()
{
	SCOPE_CYCLE_COUNTER(STAT_ParticleEmitterInstance_Init);

	UParticleLODLevel* HighLODLevel = SpriteTemplate->LODLevels[0];// Use highest LOD level for init'ing data, will contain all module types.
	CurrentMaterial = HighLODLevel->RequiredModule->Material;// Set the current material
	bool bNeedsInit = (ParticleSize == 0);// If we already have a non-zero ParticleSize, don't need to do most allocation work again

	if(bNeedsInit)
	{
		SCOPE_CYCLE_COUNTER(STAT_ParticleEmitterInstance_InitSize);

		// Copy pre-calculated info
		...

	    if ((InstanceData == NULL) || (SpriteTemplate->ReqInstanceBytes > InstancePayloadSize))
	    {
		    InstanceData = (uint8*)(FMemory::Realloc(InstanceData, SpriteTemplate->ReqInstanceBytes));
		    InstancePayloadSize = SpriteTemplate->ReqInstanceBytes;
	    }
	    FMemory::Memzero(InstanceData, InstancePayloadSize);
    
	    for (UParticleModule* ParticleModule : SpriteTemplate->ModulesNeedingInstanceData)
	    {
			uint8* PrepInstData = GetModuleInstanceData(ParticleModule);
			ParticleModule->PrepPerInstanceBlock(this, (void*)PrepInstData);
	    }

		for (UParticleModule* ParticleModule : SpriteTemplate->ModulesNeedingRandomSeedInstanceData)
		{
			FParticleRandomSeedInstancePayload* SeedInstancePayload = GetModuleRandomSeedInstanceData(ParticleModule);
			FParticleRandomSeedInfo* RandomSeedInfo = ParticleModule->GetRandomSeedInfo();
			ParticleModule->PrepRandomSeedInstancePayload(this, SeedInstancePayload, RandomSeedInfo ? *RandomSeedInfo : FParticleRandomSeedInfo());
		}
	    
	    PayloadOffset = ParticleSize;// Offset into emitter specific payload (e.g. TrailComponent requires extra bytes).
	    ParticleSize += RequiredBytes();// Update size with emitter specific size requirements.
	    ParticleSize = Align(ParticleSize, 16);// Make sure everything is at least 16 byte aligned so we can use SSE for FVector.
	    ParticleStride = CalculateParticleStride(ParticleSize);// E.g. trail emitters store trailing particles directly after leading one.
	}
	
	SetMeshMaterials(SpriteTemplate->MeshMaterials);// Setup the emitter instance material array...

	// Set initial values.
	SpawnFraction			= 0;
	SecondsSinceCreation	= 0;
	EmitterTime				= 0;
	ParticleCounter			= 0;

	UpdateTransforms();	
	Location				= Component->GetComponentLocation();
	OldLocation				= Location;
	
	TrianglesToRender		= 0;
	MaxVertexIndex			= 0;

	if (ParticleData == NULL)
	{
		MaxActiveParticles	= 0;
		ActiveParticles		= 0;
	}

	ParticleBoundingBox.Init();
	if (HighLODLevel->RequiredModule->RandomImageChanges == 0)
	{
		HighLODLevel->RequiredModule->RandomImageTime	= 1.0f;
	}
	else
	{
		HighLODLevel->RequiredModule->RandomImageTime	= 0.99f / (HighLODLevel->RequiredModule->RandomImageChanges + 1);
	}

	// Resize to sensible default.
	if (bNeedsInit && 
		Component->GetWorld()->IsGameWorld() == true &&
		// Only presize if any particles will be spawned 
		SpriteTemplate->QualityLevelSpawnRateScale > 0)
	{
		if ((HighLODLevel->PeakActiveParticles > 0) || (SpriteTemplate->InitialAllocationCount > 0))
		{
			// In-game... we assume the editor has set this properly, but still clamp at 100 to avoid wasting
			// memory.
			if (SpriteTemplate->InitialAllocationCount > 0)
			{
				Resize(FMath::Min( SpriteTemplate->InitialAllocationCount, 100 ));
			}
			else
			{
				Resize(FMath::Min( HighLODLevel->PeakActiveParticles, 100 ));
			}
		}
		else
		{
			// This is to force the editor to 'select' a value
			Resize(10);
		}
	}

	LoopCount = 0;

	if(bNeedsInit)
	{
		//QUICK_SCOPE_CYCLE_COUNTER(STAT_AllocateBurstLists);
		bKillOnDeactivate = HighLODLevel->RequiredModule->bKillOnDeactivate;
		bKillOnCompleted = HighLODLevel->RequiredModule->bKillOnCompleted;
		SortMode = HighLODLevel->RequiredModule->SortMode;

		// Reset the burst lists
		if (BurstFired.Num() < SpriteTemplate->LODLevels.Num())
		{
			BurstFired.AddZeroed(SpriteTemplate->LODLevels.Num() - BurstFired.Num());
		}

		for (int32 LODIndex = 0; LODIndex < SpriteTemplate->LODLevels.Num(); LODIndex++)
		{
			UParticleLODLevel* LODLevel = SpriteTemplate->LODLevels[LODIndex];
			FLODBurstFired& LocalBurstFired = BurstFired[LODIndex];
			if (LocalBurstFired.Fired.Num() < LODLevel->SpawnModule->BurstList.Num())
			{
				LocalBurstFired.Fired.AddZeroed(LODLevel->SpawnModule->BurstList.Num() - LocalBurstFired.Fired.Num());
			}
		}
	}

	ResetBurstList();

	IsRenderDataDirty = 1;
	bEmitterIsDone = false;
}








void UParticleSystemComponent::CreateRenderState_Concurrent(FRegisterComponentContext* Context)
{
	SCOPE_CYCLE_COUNTER(STAT_ParticleSystemComponent_CreateRenderState_Concurrent);
	SCOPE_CYCLE_COUNTER(STAT_ParticlesOverview_GT_CNC);

	ForceAsyncWorkCompletion(ENSURE_AND_STALL, false, true);

	if (Template && Template->bHasPhysics)
	{
		PrimaryComponentTick.TickGroup = TG_PrePhysics;
			
		AEmitter* EmitterOwner = Cast<AEmitter>(GetOwner());
		if (EmitterOwner)
		{
			EmitterOwner->PrimaryActorTick.TickGroup = TG_PrePhysics;
		}
	}

	Super::CreateRenderState_Concurrent(Context);
	{
		Primitive->SceneProxy = Primitive->CreateSceneProxy();
	}

	bJustRegistered = true;
}

FPrimitiveSceneProxy* UParticleSystemComponent::CreateSceneProxy()
{
	SCOPE_CYCLE_COUNTER(STAT_FParticleSystemSceneProxy_Create);
	SCOPE_CYCLE_COUNTER(STAT_ParticlesOverview_GT);

	FParticleSystemSceneProxy* NewProxy = NULL;

	if (IsActive() == true && Template)
	{
		if (EmitterInstances.Num() > 0)
		{
			CacheViewRelevanceFlags(Template);
		}

		// Create the dynamic data for rendering this particle system.
		FParticleDynamicData* ParticleDynamicData = CreateDynamicData(GetScene()->GetFeatureLevel());

		if (CanBeOccluded())
		{
			Template->CustomOcclusionBounds.IsValid = true;
			NewProxy = ::new FParticleSystemSceneProxy(this,ParticleDynamicData, true);
		}
		else
		{
			NewProxy = ::new FParticleSystemSceneProxy(this,ParticleDynamicData, false);
		}

		if (ParticleDynamicData)
		{
			for (int32 Index = 0; Index < ParticleDynamicData->DynamicEmitterDataArray.Num(); Index++)
			{
				NewProxy->QueueVertexFactoryCreation(ParticleDynamicData->DynamicEmitterDataArray[Index]);
			}
		}
	}
	
	return NewProxy;
}

