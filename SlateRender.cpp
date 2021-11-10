///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void FSlateApplication::Tick(ESlateTickType TickType)
{
	SCOPE_CYCLE_COUNTER(STAT_SlateTickTime);

	if (EnumHasAnyFlags(TickType, ESlateTickType::Widgets))
	{
		TickAndDrawWidgets(DeltaTime);
	}
}

void FSlateApplication::TickAndDrawWidgets(float DeltaTime)
{
	...
	DrawWindows();// Draw all windows
}

void FSlateApplication::DrawWindows()
{
	SCOPED_NAMED_EVENT_TEXT("Slate::DrawWindows", FColor::Magenta);
	PrivateDrawWindows();
}

void FSlateApplication::PrivateDrawWindows( TSharedPtr<SWindow> DrawOnlyThisWindow )
{
	{
		SCOPE_CYCLE_COUNTER( STAT_SlateDrawWindowTime );
		...
	}

	Renderer->DrawWindows( DrawWindowArgs.OutDrawBuffer );
}

void FSlateRHIRenderer::DrawWindows(FSlateDrawBuffer& WindowDrawBuffer)
{
	DrawWindows_Private(WindowDrawBuffer);
}

void FSlateRHIRenderer::DrawWindows_Private(FSlateDrawBuffer& WindowDrawBuffer)
{
	...
	ENQUEUE_RENDER_COMMAND(SlateDrawWindowsCommand)([Params, ViewInfo](FRHICommandListImmediate& RHICmdList)
	{
		Params.Renderer->DrawWindow_RenderThread(RHICmdList, *ViewInfo, *Params.WindowElementList, Params);
	});
	...
}

void FSlateRHIRenderer::DrawWindow_RenderThread(FRHICommandListImmediate& RHICmdList, FViewportInfo& ViewportInfo, FSlateWindowElementList& WindowElementList, const struct FSlateDrawWindowCommandParams& DrawCommandParams)
{
	static uint32 LastFramePresent = FPlatformTime::Cycles();
	FMemMark MemMark(FMemStack::Get());

	{
		SCOPED_DRAW_EVENTF(RHICmdList, SlateUI, TEXT("SlateUI Title = %s"), DrawCommandParams.WindowTitle.IsEmpty() ? TEXT("<none>") : *DrawCommandParams.WindowTitle);
		SCOPED_GPU_STAT(RHICmdList, SlateUI);
		SCOPED_NAMED_EVENT_TEXT("Slate::DrawWindow_RenderThread", FColor::Magenta);

		FMaterialRenderProxy::UpdateDeferredCachedUniformExpressions();
		GetRendererModule().InitializeSystemTextures(RHICmdList);

		assume(!bCompositeUI);

		{
			SCOPED_GPU_STAT(RHICmdList, SlateUI);
			SCOPE_CYCLE_COUNTER(STAT_SlateRenderingRTTime);
			CSV_SCOPED_TIMING_STAT_EXCLUSIVE(Slate);

			FSlateBatchData& BatchData = WindowElementList.GetBatchData();

			// Update the vertex and index buffer	
			RenderingPolicy->BuildRenderingBuffers(RHICmdList, BatchData);

			// This must happen after rendering buffers are created
			ViewportInfo.ConditionallyUpdateDepthBuffer(BatchData.IsStencilClippingRequired(), ViewportInfo.DesiredWidth, ViewportInfo.DesiredHeight);

			// should have been created by the game thread
			check(IsValidRef(ViewportInfo.ViewportRHI));

			FTexture2DRHIRef ViewportRT = bRenderedStereo ? nullptr : ViewportInfo.GetRenderTargetTexture();
			FTexture2DRHIRef BackBuffer = (ViewportRT) ? ViewportRT : RHICmdList.GetViewportBackBuffer(ViewportInfo.ViewportRHI);
			FTexture2DRHIRef PostProcessBuffer = BackBuffer;	// If compositing UI then this will be different to the back buffer

			const uint32 ViewportWidth = (ViewportRT) ? ViewportRT->GetSizeX() : ViewportInfo.Width;
			const uint32 ViewportHeight = (ViewportRT) ? ViewportRT->GetSizeY() : ViewportInfo.Height;
	
			FTexture2DRHIRef FinalBuffer = BackBuffer;

			bool bClear = DrawCommandParams.bClear;
			
			RHICmdList.BeginDrawingViewport(ViewportInfo.ViewportRHI, FTextureRHIRef());
			RHICmdList.SetViewport(0, 0, 0, ViewportWidth, ViewportHeight, 0.0f);
			RHICmdList.Transition(FRHITransitionInfo(BackBuffer, ERHIAccess::Unknown, ERHIAccess::RTV));

			{
				FRHIRenderPassInfo RPInfo(BackBuffer, ERenderTargetActions::Load_Store);

				if (ViewportInfo.bRequiresStencilTest)
				{
					ERenderTargetActions StencilAction = IsMemorylessTexture(ViewportInfo.DepthStencil) ? ERenderTargetActions::DontLoad_DontStore : ERenderTargetActions::DontLoad_Store;
					RPInfo.DepthStencilRenderTarget.Action = MakeDepthStencilTargetActions(ERenderTargetActions::DontLoad_DontStore, StencilAction);
					RPInfo.DepthStencilRenderTarget.DepthStencilTarget = ViewportInfo.DepthStencil;
					RPInfo.DepthStencilRenderTarget.ExclusiveDepthStencil = FExclusiveDepthStencil::DepthNop_StencilWrite;
				}

				if (BatchData.GetRenderBatches().Num() > 0)
				{
					RHICmdList.BeginRenderPass(RPInfo, TEXT("SlateBatches"));
					SCOPE_CYCLE_COUNTER(STAT_SlateRTDrawBatches);

					FSlateBackBuffer BackBufferTarget(BackBuffer, FIntPoint(ViewportWidth, ViewportHeight));

					FSlateRenderingParams RenderParams(ViewMatrix * ViewportInfo.ProjectionMatrix, DrawCommandParams.WorldTimeSeconds, DrawCommandParams.DeltaTimeSeconds, DrawCommandParams.RealTimeSeconds);
					
					FTexture2DRHIRef EmptyTarget;

					RenderingPolicy->DrawElements(
						RHICmdList,
						BackBufferTarget,
						BackBuffer,
						PostProcessBuffer,
						ViewportInfo.bRequiresStencilTest ? ViewportInfo.DepthStencil : EmptyTarget,
						BatchData.GetFirstRenderBatchIndex(),
						BatchData.GetRenderBatches(),
						RenderParams
					);
				}

				if (RHICmdList.IsInsideRenderPass()) // ?
				{
					RHICmdList.EndRenderPass();
				}
			}

			RHICmdList.Transition(FRHITransitionInfo(BackBuffer, ERHIAccess::Unknown, ERHIAccess::SRVGraphics));
		}
	}

	// Calculate renderthread time (excluding idle time).	
	uint32 JustBrforePresent = FPlatformTime::Cycles();
	{
		RHICmdList.EndDrawingViewport(ViewportInfo.ViewportRHI, true, DrawCommandParams.bLockToVsync);
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

	SET_CYCLE_COUNTER(STAT_RenderingIdleTime_RenderThreadSleepTime, GRenderThreadIdle[0]);
	SET_CYCLE_COUNTER(STAT_RenderingIdleTime_WaitingForGPUQuery, GRenderThreadIdle[1]);
	SET_CYCLE_COUNTER(STAT_RenderingIdleTime_WaitingForGPUPresent, GRenderThreadIdle[2]);

	for (int32 Index = 0; Index < ERenderThreadIdleTypes::Num; Index++)
	{
		RenderThreadIdle += GRenderThreadIdle[Index];
		GRenderThreadIdle[Index] = 0;
		GRenderThreadNumIdle[Index] = 0;
	}

	SET_CYCLE_COUNTER(STAT_RenderingIdleTime, RenderThreadIdle);
	GRenderThreadTime = ThreadTime - RenderThreadIdle;
	
	if (IsRunningRHIInSeparateThread())
	{
		RHICmdList.EnqueueLambda([](FRHICommandListImmediate&)
		{
			// Restart the RHI thread timer, so we don't count time spent in Present twice when this command list finishes.
			uint32 ThisCycles = FPlatformTime::Cycles();
			GWorkingRHIThreadTime += (ThisCycles - GWorkingRHIThreadStartCycles);
			GWorkingRHIThreadStartCycles = ThisCycles;

			uint32 NewVal = GWorkingRHIThreadTime - GWorkingRHIThreadStallTime;
			FPlatformAtomics::AtomicStore((int32*)&GRHIThreadTime, (int32)NewVal);
			GWorkingRHIThreadTime = 0;
			GWorkingRHIThreadStallTime = 0;
		});
	}
}