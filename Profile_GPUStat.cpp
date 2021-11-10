DECLARE_GPU_STAT(???);
SCOPED_GPU_STAT(RHICmdList, Stat_???);



#define DECLARE_GPU_STAT(StatName) \
DECLARE_FLOAT_COUNTER_STAT(TEXT(#StatName), Stat_GPU_##StatName, STATGROUP_GPU); \
CSV_DEFINE_STAT(GPU,StatName); \
static FDrawCallCategoryName DrawcallCountCategory_##StatName;


#define SCOPED_GPU_STAT(RHICmdList, StatName)  \
FScopedGPUStatEvent PREPROCESSOR_JOIN(GPUStatEvent_##StatName,__LINE__); \
PREPROCESSOR_JOIN(GPUStatEvent_##StatName, __LINE__).Begin( \
	RHICmdList, \
	CSV_STAT_FNAME(StatName), \
	GET_STATID( Stat_GPU_##StatName ).GetName(), \
	&DrawcallCountCategory_##StatName.Counter );



DECLARE_GPU_STAT(GPUSceneUpdate);
=>
DECLARE_FLOAT_COUNTER_STAT(TEXT("GPUSceneUpdate"), Stat_GPU_GPUSceneUpdate, STATGROUP_GPU);
CSV_DEFINE_STAT(GPU, StatName);
static FDrawCallCategoryName DrawcallCountCategory_GPUSceneUpdate;


SCOPED_GPU_STAT(RHICmdList, GPUSceneUpdate);
=>
FScopedGPUStatEvent GPUStatEvent_GPUSceneUpdate__LINE__;
GPUStatEvent_GPUSceneUpdate__LINE__.Begin( 
	RHICmdList, 
	CSV_STAT_FNAME(GPUSceneUpdate), 
	GET_STATID( Stat_GPU_GPUSceneUpdate ).GetName(), 
	&DrawcallCountCategory_GPUSceneUpdate.Counter );


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void FScopedGPUStatEvent::Begin(FRHICommandList& InRHICmdList, const FName& Name, const FName& StatName, int32* InNumDrawCallsPtr)
{
	check(IsInRenderingThread());
	if (!AreGPUStatsEnabled()) return;
	
	if (InRHICmdList.IsImmediate())// Non-immediate command lists are not supported (silently fail)
	{
		NumDrawCallsPtr = InNumDrawCallsPtr;
		RHICmdList = &static_cast<FRHICommandListImmediate&>(InRHICmdList);
		FRealtimeGPUProfiler::Get()->PushStat(*RHICmdList, Name, StatName, InNumDrawCallsPtr);
	}
}

void FRealtimeGPUProfiler::PushStat(FRHICommandListImmediate& RHICmdList, const FName& Name, const FName& StatName, int32* InNumDrawCallsPtr)
{
	if (bStatGatheringPaused || !bInBeginEndBlock) 
		return;
	
	if (WriteBufferIndex >= 0)
	{
		check(Frames.Num() > 0);
		Frames[WriteBufferIndex]->PushEvent(RHICmdList, Name, StatName);
	}

	if (*InNumDrawCallsPtr != -1)
	{
		RHICmdList.EnqueueLambda([InNumDrawCallsPtr](FRHICommandListImmediate&)
		{
			GCurrentNumDrawCallsRHIPtr = InNumDrawCallsPtr;
		});
	}
}

void FRealtimeGPUProfilerFrame::PushEvent(FRHICommandListImmediate& RHICmdList, const FName& Name, const FName& StatName)
{
	if (NextEventIdx >= GpuProfilerEvents.Num())
	{
		const int32 MaxNumQueries = CVarGPUStatsMaxQueriesPerFrame.GetValueOnRenderThread();

		if (MaxNumQueries < 0 || QueryCount < (uint32)MaxNumQueries)
		{
			new (GpuProfilerEvents) FRealtimeGPUProfilerEvent(*RenderQueryPool);
			QueryCount += FRealtimeGPUProfilerEvent::GetNumRHIQueriesPerEvent();
		}
		else
		{
			++OverflowEventCount;
			return;
		}
	}

	const int32 EventIdx = NextEventIdx++;

	GpuProfilerEventParentIndices.Add(EventStack.Last());
	EventStack.Push(EventIdx);
	GpuProfilerEvents[EventIdx].Begin(RHICmdList, Name, StatName);
}

void FRealtimeGPUProfilerEvent::Begin(FRHICommandListImmediate& RHICmdList, const FName& NewName, const FName& NewStatName)
{
	check(StartQuery.IsValid());
	GPUMask = RHICmdList.GetGPUMask();
	RHICmdList.EndRenderQuery(StartQuery.GetQuery());

	Name = NewName;
	StatName = NewStatName;
	StartResultMicroseconds = TStaticArray<uint64, MAX_NUM_GPUS>(InvalidQueryResult);
	EndResultMicroseconds = TStaticArray<uint64, MAX_NUM_GPUS>(InvalidQueryResult);
	FrameNumber = GFrameNumberRenderThread;
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
FScopedGPUStatEvent::~FScopedGPUStatEvent()
{
	if (RHICmdList)
	{
		End();
	}
}

void FScopedGPUStatEvent::End()
{
	check(IsInRenderingThread());
	if (!AreGPUStatsEnabled()) return;

	if (RHICmdList != nullptr)
	{
		FRealtimeGPUProfiler::Get()->PopStat(*RHICmdList, NumDrawCallsPtr);
	}
}

void FRealtimeGPUProfiler::PopStat(FRHICommandListImmediate& RHICmdList, int32* InNumDrawCallsPtr)
{
	if (bStatGatheringPaused || !bInBeginEndBlock) 
		return;

	if (WriteBufferIndex >= 0)
	{
		check(Frames.Num() > 0);
		Frames[WriteBufferIndex]->PopEvent(RHICmdList);
	}

	if (*InNumDrawCallsPtr != -1)
	{
		RHICmdList.EnqueueLambda([](FRHICommandListImmediate&)
		{
			GCurrentNumDrawCallsRHIPtr = &GCurrentNumDrawCallsRHI;
		});
	}
}

void FRealtimeGPUProfilerFrame::PopEvent(FRHICommandListImmediate& RHICmdList)
{
	if (OverflowEventCount)
	{
		--OverflowEventCount;
		return;
	}

	const int32 EventIdx = EventStack.Pop(false);

	GpuProfilerEvents[EventIdx].End(RHICmdList);
}

void FRealtimeGPUProfilerEvent::End(FRHICommandListImmediate& RHICmdList)
{
	check(EndQuery.IsValid());
	SCOPED_GPU_MASK(RHICmdList, GPUMask);
	RHICmdList.EndRenderQuery(EndQuery.GetQuery());
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void FEngineLoop::Tick()
{
	ENQUEUE_RENDER_COMMAND(BeginFrame)([CurrentFrameCounter](FRHICommandListImmediate& RHICmdList)
	{
		BeginFrameRenderThread(RHICmdList, CurrentFrameCounter);
		{
			FRealtimeGPUProfiler::Get()->BeginFrame(RHICmdList);
		}
	});

	ENQUEUE_RENDER_COMMAND(EndFrame)([CurrentFrameCounter](FRHICommandListImmediate& RHICmdList)
	{
		EndFrameRenderThread(RHICmdList, CurrentFrameCounter);
		{
			FRealtimeGPUProfiler::Get()->EndFrame(RHICmdList);
		}
	});
}

void FRealtimeGPUProfiler::BeginFrame(FRHICommandListImmediate& RHICmdList)
{
	if (!AreGPUStatsEnabled())
	{
		return;
	}

	check(bInBeginEndBlock == false);
	bInBeginEndBlock = true;

	Frames[WriteBufferIndex]->TimestampCalibrationQuery = new FRHITimestampCalibrationQuery();
	RHICmdList.CalibrateTimers(Frames[WriteBufferIndex]->TimestampCalibrationQuery);
	Frames[WriteBufferIndex]->CPUFrameStartTimestamp = FPlatformTime::Cycles64();
}

void FRealtimeGPUProfiler::EndFrame(FRHICommandListImmediate& RHICmdList)
{
	if (!AreGPUStatsEnabled())
	{
		return;
	}

	// This is called at the end of the renderthread frame. Note that the RHI thread may still be processing commands for the frame at this point, however
	// The read buffer index is always 3 frames beind the write buffer index in order to prevent us reading from the frame the RHI thread is still processing. 
	// This should also ensure the GPU is done with the queries before we try to read them
	check(Frames.Num() > 0);
	check(IsInRenderingThread());
	check(bInBeginEndBlock == true);
	bInBeginEndBlock = false;

	if (Frames[ReadBufferIndex]->UpdateStats(RHICmdList))
	{
		// On a successful read, advance the ReadBufferIndex and WriteBufferIndex and clear the frame we just read
		Frames[ReadBufferIndex]->Clear(&RHICmdList);
		WriteFrameNumber = GFrameNumberRenderThread;
		WriteBufferIndex = (WriteBufferIndex + 1) % Frames.Num();
		ReadBufferIndex = (ReadBufferIndex + 1) % Frames.Num();
		bStatGatheringPaused = false;
	}
	else
	{
		// The stats weren't ready; skip the next frame and don't advance the indices. We'll try to read the stats again next frame
		bStatGatheringPaused = true;
	}
}