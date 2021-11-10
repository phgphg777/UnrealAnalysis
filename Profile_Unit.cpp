int32 FStatUnitData::DrawStat(FViewport* InViewport, FCanvas* InCanvas, int32 InX, int32 InY)
{
	RawFrameTime = DiffTime * 1000.0f;
	FrameTime = 0.9 * FrameTime + 0.1 * RawFrameTime;

	/** Number of milliseconds the gamethread was used last frame. */
	RawGameThreadTime = FPlatformTime::ToMilliseconds(GGameThreadTime);
	GameThreadTime = 0.9 * GameThreadTime + 0.1 * RawGameThreadTime;

	/** Number of milliseconds the renderthread was used last frame. */
	RawRenderThreadTime = FPlatformTime::ToMilliseconds(GRenderThreadTime);
	RenderThreadTime = 0.9 * RenderThreadTime + 0.1 * RawRenderThreadTime;

	RawRHITTime = FPlatformTime::ToMilliseconds(GRHIThreadTime);
	RHITTime = 0.9 * RHITTime + 0.1 * RawRHITTime;

	RawGPUFrameTime[0] = FPlatformTime::ToMilliseconds(GGPUFrameTime);
	GPUFrameTime[0] = 0.9 * GPUFrameTime[0] + 0.1 * RawGPUFrameTime[0];

	...
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void FEngineLoop::Tick()
{
	uint64 CurrentFrameCounter = GFrameCounter;

	{
		SCOPE_CYCLE_COUNTER(STAT_FrameTime);//DECLARE_CYCLE_STAT_EXTERN(TEXT("FrameTime"),STAT_FrameTime,STATGROUP_Engine, CORE_API);

		FSceneInterface* Scene = CurrentWorld->Scene;
		ENQUEUE_RENDER_COMMAND(UpdateScenePrimitives)([Scene](FRHICommandListImmediate& RHICmdList)
		{
			Scene->UpdateAllPrimitiveSceneInfos(RHICmdList);
			{
				SCOPED_NAMED_EVENT(FScene_UpdateAllPrimitiveSceneInfos, FColor::Orange);
				SCOPE_CYCLE_COUNTER(STAT_UpdateScenePrimitiveRenderThreadTime);
				...
			}
		});

		ENQUEUE_RENDER_COMMAND(BeginFrame)([CurrentFrameCounter](FRHICommandListImmediate& RHICmdList)
		{
			BeginFrameRenderThread(RHICmdList, CurrentFrameCounter);
		});
	}

	FStats::AdvanceFrame( false, FStats::FOnAdvanceRenderingThreadStats::CreateStatic( &AdvanceRenderingThreadStatsGT ) );

	{
		SCOPE_CYCLE_COUNTER( STAT_FrameTime );

		CalculateFPSTimings();//GAverageMS, GAverageFPS -> Stat FPS

		ENQUEUE_RENDER_COMMAND(ResetDeferredUpdates)([](FRHICommandList& RHICmdList)// handle some per-frame tasks on the rendering thread
		{
			FDeferredUpdateResource::ResetNeedsUpdate();
			FlushPendingDeleteRHIResources_RenderThread();
		});

		GEngine->Tick(FApp::GetDeltaTime(), bIdleMode);
		{
			SCOPE_CYCLE_COUNTER(STAT_GameEngineTick);
			RedrawViewports();
			{
				SCOPE_CYCLE_COUNTER(STAT_RedrawViewports);
				GameViewport->Viewport->Draw(bShouldPresent);
				{
					ViewportClient->Draw(this, &Canvas);
					{
						GetRendererModule().BeginRenderingViewFamily(SceneCanvas,&ViewFamily);
						{
							ENQUEUE_RENDER_COMMAND(FDrawSceneCommand)([SceneRenderer, DrawSceneEnqueue](FRHICommandListImmediate& RHICmdList)
							{
								RenderViewFamily_RenderThread(RHICmdList, SceneRenderer);
								{
									SCOPE_CYCLE_COUNTER(STAT_TotalSceneRenderingTime);

									SceneRenderer->Render(RHICmdList);
									{
										{
											SCOPE_CYCLE_COUNTER(STAT_PostInitViews_FlushDel);
											RHICmdList.ImmediateFlush(EImmediateFlushType::FlushRHIThreadFlushResources);
											{
												FRHIResource::FlushPendingDeletes(FlushType == EImmediateFlushType::FlushRHIThreadFlushResourcesFlushDeferredDeletes);
											}
										}
									}

									if (bDelayCleanup)
									{
										FSceneRenderer::DelayWaitForTasksClearSnapshotsAndDeleteSceneRenderer(RHICmdList, SceneRenderer);
									}
									else
									{
										FSceneRenderer::WaitForTasksClearSnapshotsAndDeleteSceneRenderer(RHICmdList, SceneRenderer);
									}

									SET_CYCLE_COUNTER(STAT_TotalGPUFrameTime, GGPUFrameTime);
								}
								FlushPendingDeleteRHIResources_RenderThread();
							});
						}
					}
				}
			}
		}

		if (FSlateApplication::IsInitialized() && !bIdleMode)
		{
			FSlateApplication::Get().Tick(ESlateTickType::TimeAndWidgets);
			{
				SCOPE_CYCLE_COUNTER(STAT_SlateTickTime);
				if (EnumHasAnyFlags(TickType, ESlateTickType::Widgets))
				{
					TickAndDrawWidgets(DeltaTime);
					{
						DrawWindows();
						{
							PrivateDrawWindows();
							{
								{
									SCOPE_CYCLE_COUNTER( STAT_SlateDrawWindowTime );
									...
								}

								Renderer->DrawWindows( DrawWindowArgs.OutDrawBuffer );
								{
									DrawWindows_Private(WindowDrawBuffer);
									{
										ENQUEUE_RENDER_COMMAND(SlateDrawWindowsCommand)([Params, ViewInfo](FRHICommandListImmediate& RHICmdList)
										{
											Params.Renderer->DrawWindow_RenderThread(RHICmdList, *ViewInfo, *Params.WindowElementList, Params);
										});
									}
								}
							}
						}
					}
				}
			}
		}

		ENQUEUE_RENDER_COMMAND(WaitForOutstandingTasksOnly_for_DelaySceneRenderCompletion)([](FRHICommandList& RHICmdList)
		{
			QUICK_SCOPE_CYCLE_COUNTER(STAT_DelaySceneRenderCompletion_TaskWait);
			FRHICommandListExecutor::GetImmediateCommandList().ImmediateFlush(EImmediateFlushType::WaitForOutstandingTasksOnly);
		});

		GFrameCounter++;

		{
			SCOPE_CYCLE_COUNTER(STAT_FrameSyncTime);
			static FFrameEndSync FrameEndSync;
			FrameEndSync.Sync( TEXT("r.OneFrameThreadLag") != 0 );
		}

		{
			SCOPE_CYCLE_COUNTER(STAT_DeferredTickTime);
			delete PreviousPendingCleanupObjects;// Delete the objects which were enqueued for deferred cleanup before the previous frame.
			DeleteLoaders(); // destroy all linkers pending delete
			FTicker::GetCoreTicker().Tick(FApp::GetDeltaTime());
			FThreadManager::Get().Tick();
			GEngine->TickDeferredCommands();		
		}

		ENQUEUE_RENDER_COMMAND(EndFrame)([CurrentFrameCounter](FRHICommandListImmediate& RHICmdList)
		{
			EndFrameRenderThread(RHICmdList, CurrentFrameCounter);
		});

	}
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void FEngineLoop::Tick()
{
	ENQUEUE_RENDER_COMMAND(BeginFrame)([CurrentFrameCounter](FRHICommandListImmediate& RHICmdList)
	{
		void BeginFrameRenderThread(FRHICommandListImmediate& RHICmdList, uint64 CurrentFrameCounter)
		{
			void FRHICommandList::BeginFrame()
			{
				void FD3D11DynamicRHI::RHIBeginFrame()
				{
					void FD3DGPUProfiler::BeginFrame(FD3D11DynamicRHI* InRHI) 
					{
						FrameTiming.StartTiming();
					}
				}
			}
		}
	});

	ENQUEUE_RENDER_COMMAND(EndFrame)([CurrentFrameCounter](FRHICommandListImmediate& RHICmdList)
	{
		void EndFrameRenderThread(FRHICommandListImmediate& RHICmdList, uint64 CurrentFrameCounter)
		{
			void FRHICommandList::EndFrame()
			{
				void FD3D11DynamicRHI::RHIEndFrame()
				{
					void FD3DGPUProfiler::EndFrame() 
					{
						FrameTiming.EndTiming();
						GGPUFrameTime = foo(FrameTiming.GetTiming());
					}
				}
			}
		}
	});
}

void FViewport::Draw( bool bShouldPresent /*= true */)
{
	SCOPED_NAMED_EVENT(FViewport_Draw, FColor::Red);

	bool bLockToVsync = TEXT("r.VSync") != 0;
	ULocalPlayer* Player = (GEngine && World) ? GEngine->GetFirstGamePlayer(World) : NULL;
	if ( Player )
	{
		bLockToVsync |= (Player && Player->PlayerController && Player->PlayerController->bCinematicMode);
	}

	void FSceneViewport::EnqueueBeginRenderFrame(const bool bShouldPresent)
	{
		void FViewport::EnqueueBeginRenderFrame(const bool bShouldPresent)
		{
			FViewport* Viewport = this;
			ENQUEUE_RENDER_COMMAND(BeginDrawingCommand)([Viewport](FRHICommandListImmediate& RHICmdList)
			{
				void FSceneViewport::BeginRenderFrame(FRHICommandListImmediate& RHICmdList){}
			});
		}
	}

	// Calculate gamethread time (excluding idle time)
	{
		static uint32 Lastimestamp = FPlatformTime::Cycles();
		uint32 CurrentTime	= FPlatformTime::Cycles();
		FThreadIdleStats& GameThread = FThreadIdleStats::Get();
		GGameThreadTime		= (CurrentTime - Lastimestamp) - GameThread.Waits;
		Lastimestamp		= CurrentTime;
		GameThread.Waits = 0;
	}

	UWorld* ViewportWorld = ViewportClient->GetWorld();
	FCanvas Canvas(this, nullptr, ViewportWorld, ViewportWorld ? ViewportWorld->FeatureLevel.GetValue() : GMaxRHIFeatureLevel, FCanvas::CDM_DeferDrawing, ViewportClient->ShouldDPIScaleSceneCanvas() ? ViewportClient->GetDPIScale() : 1.0f);
	Canvas.SetRenderTargetRect(FIntRect(0, 0, SizeX, SizeY));
	{
		// Make sure the Canvas is not rendered upside down
		Canvas.SetAllowSwitchVerticalAxis(true);
		ViewportClient->Draw(this, &Canvas);
	}
	Canvas.Flush_GameThread();

	// Slate doesn't present immediately. Tag the viewport as requiring vsync so that it happens.
	SetRequiresVsync(bLockToVsync);
	void FSceneViewport::EnqueueEndRenderFrame(const bool bLockToVsync, const bool bShouldPresent)
	{
		void FViewport::EnqueueEndRenderFrame(const bool bLockToVsync, const bool bShouldPresent)
		{
			FEndDrawingCommandParams Params = { this, (uint32)bLockToVsync, (uint32)GInputLatencyTimer.GameThreadTrigger, (uint32)(PresentAndStopMovieDelay > 0 ? 0 : bShouldPresent) };
			ENQUEUE_RENDER_COMMAND(EndDrawingCommand)([Params](FRHICommandListImmediate& RHICmdList)
			{
				GInputLatencyTimer.RenderThreadTrigger = Params.bShouldTriggerTimerEvent;
				void FSceneViewport::EndRenderFrame(FRHICommandListImmediate& RHICmdList, bool bPresent, bool bLockToVsync){}
			});
		}
	}

	GInputLatencyTimer.GameThreadTrigger = false;
}

void FSlateRHIRenderer::DrawWindow_RenderThread(FRHICommandListImmediate& RHICmdList, FViewportInfo& ViewportInfo, FSlateWindowElementList& WindowElementList, const struct FSlateDrawWindowCommandParams& DrawCommandParams)
{
	void FRHICommandList::BeginDrawingViewport(FRHIViewport* Viewport, FRHITexture* RenderTargetRHI)
	{
		void FD3D11DynamicRHI::RHIBeginDrawingViewport(FRHIViewport* ViewportRHI, FRHITexture* RenderTarget) {}
	}

	...

	uint32 JustBrforePresent = FPlatformTime::Cycles();

	void FRHICommandList::EndDrawingViewport(FRHIViewport* Viewport, bool bPresent, bool bLockToVsync)
	{
		void FD3D11DynamicRHI::RHIEndDrawingViewport(FRHIViewport* ViewportRHI,bool bPresent,bool bLockToVsync) {}
	}

	uint32 JustPresent = FPlatformTime::Cycles();

	GSwapBufferTime = JustPresent - JustBrforePresent;
	SET_CYCLE_COUNTER(STAT_PresentTime, GSwapBufferTime);

	uint32 ThreadTime = JustPresent - LastFramePresent;
	LastFramePresent = JustPresent;

	uint32 RenderThreadIdle = 0;

	FThreadIdleStats& RenderThread = FThreadIdleStats::Get();
	GRenderThreadIdle[ERenderThreadIdleTypes::WaitingForAllOtherSleep] = RenderThread.Waits;
	GRenderThreadIdle[ERenderThreadIdleTypes::WaitingForGPUPresent] += GSwapBufferTime;
	GRenderThreadNumIdle[ERenderThreadIdleTypes::WaitingForGPUPresent]++;
	RenderThread.Waits = 0;

	for (int32 Index = 0; Index < ERenderThreadIdleTypes::Num; Index++)
	{
		RenderThreadIdle += GRenderThreadIdle[Index];
		GRenderThreadIdle[Index] = 0;
		GRenderThreadNumIdle[Index] = 0;
	}

	GRenderThreadTime = ThreadTime - RenderThreadIdle;
}


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Not called? 
void FViewport::BeginRenderFrame(FRHICommandListImmediate& RHICmdList)
{
	RHICmdList.BeginDrawingViewport(GetViewportRHI(), FTextureRHIRef());
	UpdateRenderTargetSurfaceRHIToCurrentBackBuffer();
}

void FViewport::EndRenderFrame(FRHICommandListImmediate& RHICmdList, bool bPresent, bool bLockToVsync)
{
	uint32 StartTime = FPlatformTime::Cycles();
	RHICmdList.EndDrawingViewport(GetViewportRHI(), bPresent, bLockToVsync);
	uint32 EndTime = FPlatformTime::Cycles();

	GRenderThreadIdle[ERenderThreadIdleTypes::WaitingForGPUPresent] += EndTime - StartTime;
	GRenderThreadNumIdle[ERenderThreadIdleTypes::WaitingForGPUPresent]++;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////