CREATE PIE

UEditorEngine::PlayInEditor(UWorld* InWorld)
{
	EditorWorld = InWorld;

	UEditorEngine::CreatePIEGameInstance()
	{
		UGameInstance* GameInstance = NewObject<UGameInstance>(this, GameInstanceClass);
		GameInstance->InitializeForPlayInEditor(InPIEInstance, GameInstanceParams);
		{
			WorldContext = GetEngine()->CreateNewWorldContext(EWorldType::PIE);
			{
				FWorldContext* NewWorldContext = new FWorldContext;
				WorldList.Add(NewWorldContext);
				NewWorldContext->WorldType = WorldType;
				return *NewWorldContext;
			}
			WorldContext->PIEInstance = PIEInstanceIndex;
			WorldContext->OwningGameInstance = this;
			UWorld* NewWorld = EditorEngine->CreatePIEWorldByDuplication(*WorldContext, EditorEngine->EditorWorld, PIEMapName); 
			{
				UWorld* NewPIEWorld = CastChecked<UWorld>( StaticDuplicateObject(InWorld, PlayWorldPackage,	InWorld->GetFName(), RF_AllFlags, NULL,	EDuplicateMode::PIE) );
				GPlayInEditorID = -1;
				PostCreatePIEWorld(NewPIEWorld);
				{
					NewPIEWorld->WorldType = EWorldType::PIE;
					NewPIEWorld->InitWorld(InitializationValues());
					{
						GetRendererModule().AllocateScene(this, bRequiresHitProxies, IVS.bCreateFXSystem, FeatureLevel);
						{
							FScene* NewScene = new FScene(World, bInRequiresHitProxies, GIsEditor && (!World || !World->IsGameWorld()), bCreateFXSystem, InFeatureLevel);
							{
								World->Scene = this;
							}
							AllocatedScenes.Add(NewScene);
							return NewScene;
						}

						bIsWorldInitialized = true;
					}	
				}
				return NewPIEWorld;
			}
			NewWorld->SetGameInstance(this);
			WorldContext->SetCurrentWorld(NewWorld);
		}
		FWorldContext* const PieWorldContext = GameInstance->GetWorldContext();
		GWorld = PlayWorld = PieWorldContext->World();
		// UP TO HERE, END WORLD..

		UGameViewportClient* ViewportClient = NewObject<UGameViewportClient>(this, GameViewportClientClass);
		ViewportClient->Init(*PieWorldContext, GameInstance, bCreateNewAudioDevice);
		{
			this->GameInstance = OwningGameInstance;
		}
		GameViewport = PieWorldContext->GameViewport = ViewportClient;

		FSlatePlayInEditorInfo& SlatePlayInEditorSession = SlatePlayInEditorMap.Add(PieWorldContext->ContextHandle, FSlatePlayInEditorInfo());

		ULocalPlayer *NewLocalPlayer = ViewportClient->SetupInitialLocalPlayer()
		{
			UGameInstance * ViewportGameInstance = this->GameInstance;
			return ViewportGameInstance->CreateInitialPlayer();
			{
				return CreateLocalPlayer( 0, OutError, false );
				{
					ULocalPlayer* NewPlayer = NewObject<ULocalPlayer>(GetEngine(), GetEngine()->LocalPlayerClass);
					AddLocalPlayer(NewPlayer, ControllerId);
					{
						LocalPlayers.AddUnique(NewPlayer);
						NewPlayer->PlayerAdded(GetGameViewportClient(), ControllerId);
						{
							ViewportClient = InViewportClient;
						}
					}
					return NewPlayer;
				}
			}
		}

		SlatePlayInEditorSession.EditorPlayer = NewLocalPlayer;
		TSharedPtr<ILevelViewport> LevelViewportRef = SlatePlayInEditorSession.DestinationSlateViewport.Pin();
		LevelViewportRef->StartPlayInEditorSession( ViewportClient, bInSimulateInEditor );
		{
			ActiveViewport = MakeShareable( new FSceneViewport( ViewportClient, ViewportWidget) );
			GameLayerManager->SetSceneViewport(ActiveViewport.Get());
			ViewportClient->Viewport = ActiveViewport.Get();
			FSlateApplication::Get().RegisterGameViewport(ViewportWidget.ToSharedRef() );
		}

		GameViewport->Viewport->SetPlayInEditorViewport( true );
		GPlayInEditorID = InPIEInstance;
		GameInstance->StartPlayInEditorGameInstance(NewLocalPlayer, GameInstanceParams);
		{
			UEditorEngine* const EditorEngine = CastChecked<UEditorEngine>(GetEngine());
			AActor* PlayerStart;
			EditorEngine->SpawnPlayFromHereStart(PlayWorld, PlayerStart, EditorEngine->PlayWorldLocation, EditorEngine->PlayWorldRotation)
			{
				PlayerStart = World->SpawnActor<AActor>(PlayFromHerePlayerStartClass, StartLocation, StartRotation, SpawnParameters);
			}
			PlayWorld->InitializeActorsForPlay(URL);
			{
				// In below..
			}
			LocalPlayer->SpawnPlayActor(URL.ToString(1), Error, PlayWorld);
			{
				PlayerController = InWorld->SpawnPlayActor(this, ROLE_SimulatedProxy, PlayerURL, UniqueId, OutError, GEngine->GetGamePlayers(InWorld).Find(this));
			}

			PlayWorld->BeginPlay();

			StartPIEGameInstance(LocalPlayer, Params.bSimulateInEditor, Params.bAnyBlueprintErrors, Params.bStartInSpectatorMode);
		}

		GWorld = EditorWorld;
		GPlayInEditorID = -1;

		return GameInstance;
	}	
}

UWorld::InitializeActorsForPlay(const FURL& InURL, bool bResetTime)
{
	UpdateWorldComponents( bRerunConstructionScript, true );
	{
		LineBatcher->RegisterComponentWithWorld(this);
		PersistentLineBatcher->RegisterComponentWithWorld(this);
		ForegroundLineBatcher->RegisterComponentWithWorld(this);

		CurrentLevel->UpdateLevelComponents(bRerunConstructionScripts);
		{
			IncrementalUpdateComponents( 0, bRerunConstructionScripts );
			{
				UpdateModelComponents();
				{
				}

				Actors[i]->PreRegisterAllComponents();
				Actors[i]->IncrementalRegisterComponents(0);
				{
					GetComponents()[j]->RegisterComponentWithWorld(GetWorld());
					{
						ExecuteRegisterEvents();
						{
							OnRegister();
							{
								bRegistered = true;
								UpdateComponentToWorld();
							}
							CreateRenderState_Concurrent();
							{
								// In below..
							}
						}
					}
					PostRegisterAllComponents();
				}
			}
		}
		
		UpdateCullDistanceVolumes();
	}

	Levels[i]->InitializeNetworkActors();
	bActorsInitialized = true;
	AuthorityGameMode->InitGame( FPaths::GetBaseFilename(InURL.Map), Options, Error );
	{
		GameSession = World->SpawnActor<AGameSession>(GetGameSessionClass(), SpawnInfo);
	}
	
	Levels[i]->RouteActorInitialize();
	{
		Actors[j]->PreInitializeComponents();
		Actors[j]->InitializeComponents();
		Actors[j]->PostInitializeComponents();
		{
			bActorInitialized = true;
		}
	}
}


UPrimitiveComponent::CreateRenderState_Concurrent()
{
	UPrimitiveComponent* PrimitiveComp = this;

	AActor::CreateRenderState_Concurrent();
	{
		bRenderStateCreated = true;
	}

	UpdateBounds();
	ShouldComponentAddToScene() ? GetWorld()->Scene->AddPrimitive(PrimitiveComp) : 0;
	{
		FScene* Scene = this;
		FPrimitiveSceneProxy* PrimitiveSceneProxy = PrimitiveComp->CreateSceneProxy();
		PrimitiveComp->SceneProxy = PrimitiveSceneProxy;
		FPrimitiveSceneInfo* PrimitiveSceneInfo = new FPrimitiveSceneInfo(PrimitiveComp, Scene);
		PrimitiveSceneProxy->PrimitiveSceneInfo = PrimitiveSceneInfo;
		ENQUEUE_RENDER_COMMAND()
		{
			Params.PrimitiveSceneProxy->SetTransform(Params.RenderMatrix, Params.WorldBounds, Params.LocalBounds, Params.AttachmentRootPosition);
			Params.PrimitiveSceneProxy->CreateRenderThreadResources();
			Scene->AddPrimitiveSceneInfo_RenderThread(RHICmdList, PrimitiveSceneInfo);
		}
	}
}