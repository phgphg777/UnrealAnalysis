struct FD3DGPUProfiler : public FGPUProfiler {
	FD3D11BufferedGPUTiming FrameTiming;/** Used to measure GPU time per frame. */
}

class D3D11RHI_API FD3D11DynamicRHI : public FDynamicRHI, public IRHICommandContextPSOFallback {
	FD3DGPUProfiler GPUProfilingData;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
bool UEngine::Exec( UWorld* InWorld, const TCHAR* Cmd, FOutputDevice& Ar )
{
#if WITH_PROFILEGPU
	else if( FParse::Command(&Cmd,TEXT("PROFILEGPU")) )
	{
		return HandleProfileGPUCommand( Cmd, Ar );
	}	
#endif // #if !UE_BUILD_SHIPPING
}

#if WITH_PROFILEGPU
bool UEngine::HandleProfileGPUCommand( const TCHAR* Cmd, FOutputDevice& Ar )
{
	if (!FApp::CanEverRender())
	{
		return true;
	}

	if (FParse::Command(&Cmd, TEXT("TRACE")))
	{
		FString Filename = CreateProfileDirectoryAndFilename(TEXT(""), TEXT(".rtt"));
		//FPaths::MakePlatformFilename(Filename);
		GGPUTraceFileName = Filename;
		Ar.Logf(TEXT("Tracing the next GPU frame"));
	}
	else
	{
		if (!GTriggerGPUHitchProfile)
		{
			GTriggerGPUProfile = true;
			Ar.Logf(TEXT("Profiling the next GPU frame"));
		}
		else
		{
			Ar.Logf(TEXT("Can't do a gpu profile during a hitch profile!"));
		}
	}

	return true;
}
#endif // WITH_PROFILEGPU

{
	static inline void BeginFrameRenderThread(FRHICommandListImmediate& RHICmdList, uint64 CurrentFrameCounter)
	{
		...
		RHICmdList.BeginFrame();
		...
	}

	void FD3D11DynamicRHI::RHIBeginFrame()
	{
		...
		GPUProfilingData.BeginFrame(this);
		...
	}

	void FD3DGPUProfiler::BeginFrame(FD3D11DynamicRHI* InRHI)
	{
		...
		bLatchedGProfilingGPU = GTriggerGPUProfile;
		if(bLatchedGProfilingGPU)
		{
			SetEmitDrawEvents(true);
			bTrackingEvents = true;
		}
		...
	}

	void CORE_API SetEmitDrawEvents(bool EmitDrawEvents)
	{
		GEmitDrawEvents = EmitDrawEvents;
		GCommandListOnlyDrawEvents = !GEmitDrawEvents;
	}
}
{
	static inline void EndFrameRenderThread(FRHICommandListImmediate& RHICmdList, uint64 CurrentFrameCounter)
	{
		...
		RHICmdList.EndFrame();
		...
	}

	void FD3D11DynamicRHI::RHIEndFrame()
	{
		...
		GPUProfilingData.EndFrame();
		...
	}

	void FD3DGPUProfiler::EndFrame()
	{
		if (bLatchedGProfilingGPU && bTrackingEvents)
		{
			SetEmitDrawEvents(bOriginalGEmitDrawEvents);
			GTriggerGPUProfile = false;
			bLatchedGProfilingGPU = false;
		}
	}
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#define SCOPED_DRAW_EVENT(RHICmdList, Name) FDrawEvent PREPROCESSOR_JOIN(Event_##Name,__LINE__); if(GetEmitDrawEvents()) PREPROCESSOR_JOIN(Event_##Name,__LINE__).Start(RHICmdList, FColor(0), TEXT(#Name));
->
[
	FDrawEvent Event_Name__LINE__;
	if(GetEmitDrawEvents()) 
		Event_Name__LINE__.Start(RHICmdList, FColor(0), TEXT("Name"));
]

bool GetEmitDrawEvents() {return GEmitDrawEvents;}

struct RENDERCORE_API FDrawEvent {
	FRHIComputeCommandList* RHICmdList;
	FDrawEvent() : RHICmdList(nullptr) {}
	~FDrawEvent() { if (RHICmdList) Stop(); }
};

{
	void FDrawEvent::Start(FRHIComputeCommandList& InRHICmdList, FColor Color, const TCHAR* Fmt, ...)
	{
		check(IsInParallelRenderingThread() || IsInRHIThread());
		{
			va_list ptr;
			va_start(ptr, Fmt);
			TCHAR TempStr[256];
			// Build the string in the temp buffer
			FCString::GetVarArgs(TempStr, UE_ARRAY_COUNT(TempStr), Fmt, ptr);
			InRHICmdList.PushEvent(TempStr, Color);
			RHICmdList = &InRHICmdList;
			va_end(ptr);
		}
	}

	void FRHIComputeCommandList::PushEvent(const TCHAR* Name, FColor Color)
	{
		if (Bypass())
		{
			GetComputeContext().RHIPushEvent(Name, Color);
			return;
		}
		TCHAR* NameCopy = AllocString(Name);
		ALLOC_COMMAND(FRHICommandPushEvent)(NameCopy, Color);
	}

	void FD3D11DynamicRHI::RHIPushEvent(const TCHAR* Name, FColor Color)
	{ 
		GPUProfilingData.PushEvent(Name, Color);
	}

	void FD3DGPUProfiler::PushEvent(const TCHAR* Name, FColor Color)
	{
	#if WITH_DX_PERF
		D3DPERF_BeginEvent(Color.DWColor(),Name);
	#endif
		FGPUProfiler::PushEvent(Name, Color);
	}

	void FGPUProfiler::PushEvent(const TCHAR* Name, FColor Color)
	{
		if (bTrackingEvents)
		{
			...
			CurrentEventNode->StartTiming();
		}
	}

	void FD3D11EventNode::StartTiming() override
	{
		Timing.StartTiming();
	}

	void FD3D11BufferedGPUTiming::StartTiming()
	{
		...
		D3DRHI->GetDeviceContext()->End(StartTimestamps[NewTimestampIndex]);// Issue a timestamp query for the 'start' time.
	}
}

{
	void FDrawEvent::Stop()
	{
		if (RHICmdList)
		{
			RHICmdList->PopEvent();
			RHICmdList = NULL;
		}
	}

	void FRHIComputeCommandList::PopEvent()
	{
		if (Bypass())
		{
			GetComputeContext().RHIPopEvent();
			return;
		}
		ALLOC_COMMAND(FRHICommandPopEvent)();
	}

	void FD3D11DynamicRHI::RHIPopEvent()
	{ 
		GPUProfilingData.PopEvent(); 
	}

	...

	void FD3D11BufferedGPUTiming::EndTiming()
	{
		...
		D3DRHI->GetDeviceContext()->End(EndTimestamps[CurrentTimestamp]);// Issue a timestamp query for the 'end' time.
	}
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class RENDERCORE_API FRDGBuilder {
	//FRDGCPUScopeStacks CPUScopeStacks;
	FRDGGPUScopeStacksByPipeline GPUScopeStacks;
public:	
	FRDGBuilder(FRHICommandListImmediate& InRHICmdList, FRDGEventName InName={}, const char* UnaccountedCSVStat=kDefaultUnaccountedCSVStat)
	: RHICmdList(InRHICmdList)
	, RHICmdListAsyncCompute(FRHICommandListExecutor::GetImmediateAsyncComputeCommandList())
	, BuilderName(InName)
	//, CPUScopeStacks(RHICmdList, UnaccountedCSVStat)
	, GPUScopeStacks(RHICmdList, RHICmdListAsyncCompute) {...}
}

struct FRDGGPUScopeStacksByPipeline {
	FRDGGPUScopeStacks Graphics;
	FRDGGPUScopeStacks AsyncCompute;
public:	
	FRDGGPUScopeStacksByPipeline(FRHICommandListImmediate& RHICmdListGraphics, FRHIComputeCommandList& RHICmdListAsyncCompute)
	: Graphics(RHICmdListGraphics), AsyncCompute(RHICmdListAsyncCompute) {}
};

struct FRDGGPUScopeStacks {
	FRDGEventScopeStack Event;
	FRDGGPUStatScopeStack Stat;
public:
	FRDGGPUScopeStacks(FRHIComputeCommandList& RHICmdList)
	: Event(RHICmdList), Stat(RHICmdList) {}
};

[
	class FRDGEventScope {
		const FRDGEventScope* const ParentScope;
		const FRDGEventName Name;
		const FRHIGPUMask GPUMask;
	public:
		FRDGEventScope(const FRDGEventScope* InParentScope, FRDGEventName&& InName, FRHIGPUMask InGPUMask)
		: ParentScope(InParentScope)
		, Name(Forward<FRDGEventName&&>(InName))
		, GPUMask(InGPUMask) {}
	};

	class RENDERCORE_API FRDGEventScopeStack {
		TRDGScopeStack<FRDGEventScope> ScopeStack;
		bool bEventPushed = false;
	public:
		bool IsEnabled() {return GetEmitRDGEvents();}
		FRDGEventScopeStack(FRHIComputeCommandList& InRHICmdList)
		: ScopeStack(InRHICmdList, &OnPushEvent, &OnPopEvent) {}
	};

	static void OnPushEvent(FRHIComputeCommandList& RHICmdList, const FRDGEventScope* Scope)
	{
		SCOPED_GPU_MASK(RHICmdList, Scope->GPUMask);
		RHICmdList.PushEvent(Scope->Name.GetTCHAR(), FColor(0));
	}
	static void OnPopEvent(FRHIComputeCommandList& RHICmdList, const FRDGEventScope* Scope)
	{
		SCOPED_GPU_MASK(RHICmdList, Scope->GPUMask);
		RHICmdList.PopEvent();
	}
	
]
[
	class FRDGGPUStatScope {
		const FRDGGPUStatScope* const ParentScope;
		const FName Name;
		const FName StatName;
		int32* DrawCallCounter;
	public:
		FRDGGPUStatScope(const FRDGGPUStatScope* InParentScope, const FName& InName, const FName& InStatName, int32* InDrawCallCounter)
		: ParentScope(InParentScope)
		, Name(InName)
		, StatName(InStatName)
		, DrawCallCounter(InDrawCallCounter) {}
	};
	
	class RENDERCORE_API FRDGGPUStatScopeStack {
		TRDGScopeStack<FRDGGPUStatScope> ScopeStack;
	public:
		FRDGGPUStatScopeStack(FRHIComputeCommandList& InRHICmdList)
		: ScopeStack(InRHICmdList, &OnPushGPUStat, &OnPopGPUStat) {}
	};

	static void OnPushGPUStat(FRHIComputeCommandList& RHICmdList, const FRDGGPUStatScope* Scope)
	{
	#if HAS_GPU_STATS
		if (RHICmdList.IsImmediate())// GPU stats are currently only supported on the immediate command list.
		{
			FRealtimeGPUProfiler::Get()->PushStat(static_cast<FRHICommandListImmediate&>(RHICmdList), Scope->Name, Scope->StatName, Scope->DrawCallCounter);
		}
	#endif
	}

	static void OnPopGPUStat(FRHIComputeCommandList& RHICmdList, const FRDGGPUStatScope* Scope)
	{
	#if HAS_GPU_STATS
		if (RHICmdList.IsImmediate())// GPU stats are currently only supported on the immediate command list.
		{
			FRealtimeGPUProfiler::Get()->PopStat(static_cast<FRHICommandListImmediate&>(RHICmdList), Scope->DrawCallCounter);
		}
	#endif
	}
]

template <typename TScopeType>
class TRDGScopeStack final
{
	static constexpr uint32 kScopeStackDepthMax = 8;
	using FPushFunction = void(*)(FRHIComputeCommandList&, const TScopeType*);
	using FPopFunction = void(*)(FRHIComputeCommandList&, const TScopeType*);
	const TScopeType* CurrentScope = nullptr;/** The top of the scope stack during setup. */
	TArray<TScopeType*> Scopes;/** Tracks scopes allocated through MemStack for destruction. */
	TStaticArray<const TScopeType*, kScopeStackDepthMax> ScopeStack;/** Stacks of scopes pushed to the RHI command list during execution. */
	FRHIComputeCommandList& RHICmdList;
	FMemStackBase& MemStack;
	FPushFunction PushFunction;
	FPopFunction PopFunction;
public:
	TRDGScopeStack(FRHIComputeCommandList& InRHICmdList, FPushFunction InPushFunction, FPopFunction InPopFunction)
	: RHICmdList(InRHICmdList)
	, MemStack(FMemStack::Get())
	, PushFunction(InPushFunction)
	, PopFunction(InPopFunction)
	, ScopeStack(MakeUniformStaticArray<const TScopeType*, kScopeStackDepthMax>(nullptr)) {}
};
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#define RDG_EVENT_NAME(Format, ...) FRDGEventName(TEXT(Format), ##__VA_ARGS__)
#define RDG_EVENT_SCOPE(GraphBuilder, Format, ...) FRDGEventScopeGuard PREPROCESSOR_JOIN(__RDG_ScopeRef_,__LINE__) ((GraphBuilder), RDG_EVENT_NAME(Format, ##__VA_ARGS__))
->
[
	FRDGEventScopeGuard __RDG_ScopeRef___LINE__ (GraphBuilder, FRDGEventName(TEXT(Format), ...));
]

int32 GRDGDebug = 0;
FAutoConsoleVariableRef CVarRDGDebug(
	TEXT("r.RDG.Debug"), 
	GRDGDebug,
	TEXT("Allow to output warnings for inefficiencies found during wiring and execution of the passes.\n")
	TEXT(" 0: disabled;\n")
	TEXT(" 1: emit warning once (default);\n")
	TEXT(" 2: emit warning everytime issue is detected."),
	ECVF_RenderThreadSafe);

RENDERCORE_API bool GetEmitRDGEvents()
{
	check(IsInRenderingThread());
#if RDG_EVENTS != RDG_EVENTS_NONE
	return GetEmitDrawEvents() || GRDGDebug;
#else
	return false;
#endif
}

class RENDERCORE_API FRDGEventName {
#if RDG_EVENTS == RDG_EVENTS_STRING_REF || RDG_EVENTS == RDG_EVENTS_STRING_COPY
	const TCHAR* EventFormat = TEXT("");// Event format kept around to still have a clue what error might be causing the problem in error messages.
	#if RDG_EVENTS == RDG_EVENTS_STRING_COPY
	FString FormatedEventName;// Formated event name if GetEmitRDGEvents() == true.
	#endif
#endif
};

FRDGEventName::FRDGEventName(const TCHAR* InEventFormat, ...)
: EventFormat(InEventFormat)
{
	if (GetEmitRDGEvents())
	{
		va_list VAList;
		va_start(VAList, InEventFormat);
		TCHAR TempStr[256];
		// Build the string in the temp buffer
		FCString::GetVarArgs(TempStr, UE_ARRAY_COUNT(TempStr), InEventFormat, VAList);
		va_end(VAList);
		FormatedEventName = TempStr;
	}
}

class RENDERCORE_API FRDGEventScopeGuard {	
	FRDGBuilder& GraphBuilder;
	bool bCondition = true;
};

{
	FRDGEventScopeGuard::FRDGEventScopeGuard(FRDGBuilder& InGraphBuilder, FRDGEventName&& ScopeName, bool InbCondition=true)
	: GraphBuilder(InGraphBuilder)
	, bCondition(InbCondition)
	{
		if (bCondition)
		{
			GraphBuilder.GPUScopeStacks.BeginEventScope(MoveTemp(ScopeName));
		}
	}

	void FRDGGPUScopeStacksByPipeline::BeginEventScope(FRDGEventName&& ScopeName)
	{
		FRDGEventName ScopeNameCopy = ScopeName;
		Graphics.Event.BeginScope(MoveTemp(ScopeNameCopy));
		AsyncCompute.Event.BeginScope(MoveTemp(ScopeName));
	}

	void FRDGEventScopeStack::BeginScope(FRDGEventName&& EventName)
	{
		if (IsEnabled())
		{
			ScopeStack.BeginScope(Forward<FRDGEventName&&>(EventName), ScopeStack.RHICmdList.GetGPUMask());
		}
	}

	template <typename TScopeType>
	template <typename... TScopeConstructArgs>
	void TRDGScopeStack<TScopeType>::BeginScope(TScopeConstructArgs... ScopeConstructArgs)
	{
		auto Scope = new(MemStack) TScopeType(CurrentScope, Forward<TScopeConstructArgs>(ScopeConstructArgs)...);
		Scopes.Add(Scope);
		CurrentScope = Scope;
	}
}

{
	FRDGEventScopeGuard::~FRDGEventScopeGuard()
	{
		if (bCondition)
		{
			GraphBuilder.GPUScopeStacks.EndEventScope();
		}
	}

	void FRDGGPUScopeStacksByPipeline::EndEventScope()
	{
		Graphics.Event.EndScope();
		AsyncCompute.Event.EndScope();
	}


	void FRDGEventScopeStack::EndScope()
	{
		if (IsEnabled())
		{
			ScopeStack.EndScope();
		}
	}

	template <typename TScopeType>
	void TRDGScopeStack<TScopeType>::EndScope()
	{
		checkf(CurrentScope != nullptr, TEXT("Current scope is null."));
		CurrentScope = CurrentScope->ParentScope;
	}
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
struct FRDGGPUScopes {
	const FRDGEventScope* Event = nullptr;
	const FRDGGPUStatScope* Stat = nullptr;
};

using FRDGPassHandle = TRDGHandle<FRDGPass, uint16>;
using FRDGPassRegistry = TRDGHandleRegistry<FRDGPassHandle>;

class RENDERCORE_API FRDGPass {
	FRDGPassHandle Handle;
	//FRDGCPUScopes CPUScopes;
	FRDGGPUScopes GPUScopes;
}

template <typename LocalObjectType, typename LocalIndexType>
class TRDGHandle {
	using ObjectType = LocalObjectType;
	using IndexType = LocalIndexType;
	static const TRDGHandle Null;
	static const IndexType kNullIndex = TNumericLimits<IndexType>::Max();
	IndexType Index = kNullIndex;
}

template <typename LocalHandleType>
class TRDGHandleRegistry {
	using HandleType = LocalHandleType;
	using ObjectType = typename HandleType::ObjectType;
	using IndexType = typename HandleType::IndexType;
	TArray<ObjectType*> Array;
public:
	HandleType Begin() const {return HandleType(0);}
	HandleType Last() const {return HandleType(Array.Num()-1);}
	void Insert(ObjectType* Object)
	{
		Array.Emplace(Object);
		Object->Handle = Last();
	}
}

class RENDERCORE_API FRDGBuilder {
	//FRDGPassRegistry Passes;
	TRDGHandleRegistry< TRDGHandle<FRDGPass, uint16> >		Passes;
}

template <typename ParameterStructType, typename ExecuteLambdaType>
FRDGPassRef FRDGBuilder::AddPass(
	FRDGEventName&& Name,
	const ParameterStructType* ParameterStruct,
	ERDGPassFlags Flags,
	ExecuteLambdaType&& ExecuteLambda)
{
	using LambdaPassType = TRDGLambdaPass<ParameterStructType, ExecuteLambdaType>;

	FRDGPass* Pass = Allocator.AllocObject<LambdaPassType>(
		MoveTemp(Name),
		ParameterStruct,
		OverridePassFlags(Name.GetTCHAR(), Flags, LambdaPassType::kSupportsAsyncCompute),
		MoveTemp(ExecuteLambda));

	Passes.Insert(Pass);
	SetupPass(Pass);
	return Pass;
}

void FRDGBuilder::SetupPass(FRDGPass* Pass)
{
	...
	//Pass->CPUScopes = CPUScopeStacks.GetCurrentScopes();
	Pass->GPUScopes = GPUScopeStacks.GetCurrentScopes(PassPipeline);
}

{
	FRDGGPUScopes FRDGGPUScopeStacksByPipeline::GetCurrentScopes(ERHIPipeline Pipeline) const
	{
		return GetScopeStacks(Pipeline).GetCurrentScopes();
	}

	FRDGGPUScopes FRDGGPUScopeStacks::GetCurrentScopes() const
	{
		FRDGGPUScopes Scopes;
		Scopes.Event = Event.ScopeStack.CurrentScope;
		Scopes.Stat = Stat.ScopeStack.CurrentScope;
		return Scopes;
	}
}

void FRDGBuilder::Execute()
{
	SCOPED_NAMED_EVENT(FRDGBuilder_Execute, FColor::Emerald);

	...

	//IF_RDG_CPU_SCOPES(CPUScopeStacks.BeginExecute());
	IF_RDG_GPU_SCOPES(GPUScopeStacks.BeginExecute());

	if (!GRDGImmediateMode)
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_FRDGBuilder_Execute_Passes);

		for (FRDGPassHandle PassHandle = Passes.Begin(); PassHandle != Passes.End(); ++PassHandle)
		{
			if (!PassesToCull[PassHandle])
			{
				ExecutePass(Passes[PassHandle]);
			}
		}
	}
	else
	{
		ExecutePass(EpiloguePass);
	}

	RHICmdList.SetGlobalUniformBuffers({});

	for (const auto& Query : ExtractedTextures)
	{
		*Query.Value = Query.Key->PooledRenderTarget;
	}

	for (const auto& Query : ExtractedBuffers)
	{
		*Query.Value = Query.Key->PooledBuffer;
	}

	IF_RDG_GPU_SCOPES(GPUScopeStacks.Graphics.EndExecute());
	//IF_RDG_CPU_SCOPES(CPUScopeStacks.EndExecute());

	Clear();
}

void FRDGBuilder::ExecutePass(FRDGPass* Pass)
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_FRDGBuilder_ExecutePass);
	//IF_RDG_CPU_SCOPES(CPUScopeStacks.BeginExecutePass(Pass));

	assume(Pass->GetPipeline() != ERHIPipeline::AsyncCompute);
	const bool bUsePassEventScope = Pass != EpiloguePass && Pass != ProloguePass;

#if RDG_GPU_SCOPES
	if (bUsePassEventScope)
	{
		GPUScopeStacks.BeginExecutePass(Pass);
	}
#endif

	// Execute the pass by invoking the prologue, then the pass body, then the epilogue.
	ExecutePassPrologue(RHICmdList, Pass);
	Pass->Execute(RHICmdList);
	ExecutePassEpilogue(RHICmdList, Pass);

#if RDG_GPU_SCOPES
	if (bUsePassEventScope)
	{
		GPUScopeStacks.EndExecutePass(Pass);
	}
#endif
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
	void FRDGGPUScopeStacksByPipeline::BeginExecute()
	{
		Graphics.BeginExecute();
		AsyncCompute.BeginExecute();
	}

	void FRDGGPUScopeStacks::BeginExecute()
	{
		Event.BeginExecute();
		Stat.BeginExecute();
	}

	void FRDGEventScopeStack::BeginExecute()
	{
		if (IsEnabled())
		{
			ScopeStack.BeginExecute();
		}
	}

	template <typename TScopeType>
	void TRDGScopeStack<TScopeType>::BeginExecute()
	{
		checkf(CurrentScope == nullptr, TEXT("Render graph needs to have all scopes ended to execute."));
	}
}

{
	void FRDGGPUScopeStacksByPipeline::BeginExecutePass(const FRDGPass* Pass)
	{
		GetScopeStacks(Pass->GetPipeline()).BeginExecutePass(Pass);
	}

	void FRDGGPUScopeStacks::BeginExecutePass(const FRDGPass* Pass)
	{
		Event.BeginExecutePass(Pass);
		Stat.BeginExecutePass(Pass);
	}

	template <typename TScopeType>
	void TRDGScopeStack<TScopeType>::BeginExecutePass(const TScopeType* ParentScope)
	{
		// Find out how many scopes needs to be popped.
		TStaticArray<const TScopeType*, kScopeStackDepthMax> TraversedScopes;
		int32 CommonScopeId = -1;
		int32 TraversedScopeCount = 0;

		// Find common ancestor between current stack and requested scope.
		while (ParentScope)
		{
			TraversedScopes[TraversedScopeCount] = ParentScope;

			for (int32 i = 0; i < ScopeStack.Num(); i++)
			{
				if (ScopeStack[i] == ParentScope)
				{
					CommonScopeId = i;
					break;
				}
			}

			if (CommonScopeId != -1)
			{
				break;
			}

			TraversedScopeCount++;
			ParentScope = ParentScope->ParentScope;
		}

		// Pop no longer used scopes.
		for (int32 i = CommonScopeId + 1; i < kScopeStackDepthMax; i++)
		{
			if (!ScopeStack[i])
			{
				break;
			}

			PopFunction(RHICmdList, ScopeStack[i]);
			ScopeStack[i] = nullptr;
		}

		// Push new scopes.
		for (int32 i = TraversedScopeCount - 1; i >= 0; i--)
		{
			PushFunction(RHICmdList, TraversedScopes[i]);
			CommonScopeId++;
			ScopeStack[CommonScopeId] = TraversedScopes[i];
		}
	}

	void FRDGEventScopeStack::BeginExecutePass(const FRDGPass* Pass)
	{
		if (IsEnabled())
		{
			ScopeStack.BeginExecutePass(Pass->GetGPUScopes().Event);
			const TCHAR* Name = Pass->GetEventName().GetTCHAR();

			if (Name && *Name)
			{
				ScopeStack.RHICmdList.PushEvent(Name, Color(255, 255, 255));
				bEventPushed = true;
			}
		}
	}
}

{
	void FRDGGPUScopeStacksByPipeline::EndExecutePass(const FRDGPass* Pass)
	{
		GetScopeStacks(Pass->GetPipeline()).EndExecutePass();
	}

	void FRDGGPUScopeStacks::EndExecutePass()
	{
		Event.EndExecutePass();
	}

	void FRDGEventScopeStack::EndExecutePass()
	{
		if (IsEnabled() && bEventPushed)
		{
			ScopeStack.RHICmdList.PopEvent();
			bEventPushed = false;
		}
	}
}

{
	void FRDGGPUScopeStacksByPipeline::EndExecute()
	{
		Graphics.EndExecute();
		AsyncCompute.EndExecute();
	}

	void FRDGGPUScopeStacks::EndExecute()
	{
		Event.EndExecute();
		Stat.EndExecute();
	}

	void FRDGEventScopeStack::EndExecute()
	{
		if (IsEnabled())
		{
			ScopeStack.EndExecute();
		}
	}

	template <typename TScopeType>
	void TRDGScopeStack<TScopeType>::EndExecute()
	{
		for (uint32 ScopeIndex = 0; ScopeIndex < kScopeStackDepthMax; ++ScopeIndex)
		{
			if (!ScopeStack[ScopeIndex])
			{
				break;
			}

			PopFunction(RHICmdList, ScopeStack[ScopeIndex]);
		}
		ClearScopes();
	}
}