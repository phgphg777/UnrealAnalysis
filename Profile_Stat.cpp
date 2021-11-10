DECLARE_STAT_GROUP(DisplayName, Group, GroupCategory, InDefaultEnable, InCompileTimeEnable, InSortByName) 
=>
struct FStatGroup_##Group
{ 
	enum { DefaultEnable = InDefaultEnable, CompileTimeEnable = InCompileTimeEnable, SortByName = InSortByName }; 
	static const char* GetGroupName() { return #Group; } 
	static const char* GetGroupCategory() { return #GroupCategory; } 
	static const TCHAR* GetDescription() { return DisplayName; } 
	static bool IsDefaultEnabled() { return (bool)DefaultEnable; } 
	static bool IsCompileTimeEnable() { return (bool)CompileTimeEnable; } 
	static bool GetSortByName() { return (bool)SortByName; } 
};
DECLARE_STATS_GROUP(DisplayName, Group, GroupCategory) 
=> DECLARE_STAT_GROUP(DisplayName, Group, GroupCategory, true, true, false);

DECLARE_STAT(DisplayName, Stat, Group, StatType, bShouldClearEveryFrame, bCycleStat, MemoryRegion) 
=>
struct FStat_##Stat
{ 
	typedef FStatGroup_##Group TGroup; 
	static const char* GetStatName() { return #Stat; }
	static const TCHAR* GetDescription() { return DisplayName; } 
	static EStatDataType::Type GetStatType() { return StatType; } 
	static bool IsClearEveryFrame() { return bShouldClearEveryFrame; } 
	static bool IsCycleStat() { return bCycleStat; } 
	static FPlatformMemory::EMemoryCounterRegion GetMemoryRegion() { return MemoryRegion; } 
};

DEFINE_STAT(Stat)
=> struct FThreadSafeStaticStat<FStat_##Stat> StatPtr_##Stat;

GET_STATID(Stat) 
=> StatPtr_##Stat.GetStatId()

SCOPE_CYCLE_COUNTER(Stat)
=> FScopeCycleCounter CycleCount_##Stat(GET_STATID(Stat));
=> FScopeCycleCounter CycleCount_##Stat(StatPtr_##Stat.GetStatId());
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
CYCLE_COMMON
=> EStatDataType::ST_int64, true, true, FPlatformMemory::MCR_Invalid

DECLARE_CYCLE_STAT_EXTERN(DisplayName, Stat, Group, APIX)
=>
DECLARE_STAT(DisplayName, Stat, Group,  CYCLE_COMMON); 
extern APIX DEFINE_STAT(Stat);

DECLARE_CYCLE_STAT(DisplayName, Stat, Group) 
=>
DECLARE_STAT(DisplayName, Stat, Group,  CYCLE_COMMON); 
static DEFINE_STAT(Stat);

DECLARE_SCOPE_CYCLE_COUNTER(DisplayName, Stat, Group) 
=>
DECLARE_CYCLE_STAT(DisplayName, Stat, Group); 
SCOPE_CYCLE_COUNTER(Stat);

RETURN_QUICK_DECLARE_CYCLE_STAT(Stat, Group) 
=>	
DECLARE_CYCLE_STAT(TEXT(#Stat), Stat, Group);
return GET_STATID(Stat);
=>
struct FStat_##Stat {};
static struct FThreadSafeStaticStat<FStat_##Stat> StatPtr_##Stat;
return StatPtr_##Stat.GetStatId();
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
DECLARE_STATS_GROUP(TEXT("Quick"), STATGROUP_Quick, STATCAT_Advanced);

QUICK_SCOPE_CYCLE_COUNTER(Stat)
=> DECLARE_SCOPE_CYCLE_COUNTER(TEXT(#Stat), Stat, STATGROUP_Quick)
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
DECLARE_STATS_GROUP(TEXT("Render Thread Commands"), STATGROUP_RenderThreadCommands, STATCAT_Advanced);
=> struct FStatGroup_STATGROUP_RenderThreadCommands {};

template<typename RenderCommandTitleName, typename LAMBDA>
class TEnqueueUniqueRenderCommandType : public FRenderCommand {
	struct FStat_ThisRenderCommand
	{
		typedef FStatGroup_STATGROUP_RenderThreadCommands TGroup;
		static const char* GetStatName() {return RenderCommandTitleName::CStr();}
		static const TCHAR* GetDescription() {return RenderCommandTitleName::TStr();}
		static EStatDataType::Type GetStatType() {return EStatDataType::ST_int64;}
		static bool IsClearEveryFrame() {return true;}
		static bool IsCycleStat() {return true;}
		static FPlatformMemory::EMemoryCounterRegion GetMemoryRegion() {return FPlatformMemory::MCR_Invalid;}
	};

public:
	void DoTask(ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
	{
		Lambda(GetImmediateCommandList_ForRenderCommand());
	}
	TStatId GetStatId() const
	{
		static struct FThreadSafeStaticStat<FStat_ThisRenderCommand> StatPtr_ThisRenderCommand;
		return StatPtr_ThisRenderCommand.GetStatId();
	}
}

template<typename RenderCommandTitleName, typename LAMBDA>
void EnqueueUniqueRenderCommand(LAMBDA&& Lambda)
{
	typedef TEnqueueUniqueRenderCommandType<RenderCommandTitleName, LAMBDA> EURCType;

	if (IsInRenderingThread())
	{
		FRHICommandListImmediate& RHICmdList = GetImmediateCommandList_ForRenderCommand();
		Lambda(RHICmdList);
	}
	else
	{
		check(ShouldExecuteOnRenderThread());
		CheckNotBlockedOnRenderThread();
		TGraphTask<EURCType>::CreateTask().ConstructAndDispatchWhenReady(Forward<LAMBDA>(Lambda));
	}
}

template<typename TTask>
class TGraphTask final : public FBaseGraphTask {
	void ExecuteTask(TArray<FBaseGraphTask*>& NewTasks, ENamedThreads::Type CurrentThread) override
	{
		TTask& Task = *(TTask*)&TaskStorage;
		{
			FScopeCycleCounter Scope(Task.GetStatId(), true); 
			Task.DoTask(CurrentThread, Subsequents);
			Task.~TTask();
		}
	}
}

ENQUEUE_RENDER_COMMAND(RenderCommandTitle) ([&](FRHICommandListImmediate& RHICmdList){});
=>
struct RenderCommandTitleName 
{  
	static const char* CStr() { return #RenderCommandTitle; } 
	static const TCHAR* TStr() { return TEXT(#RenderCommandTitle); } 
}; 
EnqueueUniqueRenderCommand<RenderCommandTitleName> ([&](FRHICommandListImmediate& RHICmdList){});