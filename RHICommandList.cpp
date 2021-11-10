/* In UE 4.25
class RHI_API FRHICommandListBase : public FNoncopyable {}
class RHI_API FRHIComputeCommandList : public FRHICommandListBase {}
class RHI_API FRHICommandList : public FRHIComputeCommandList {}
class RHI_API FRHICommandListImmediate : public FRHICommandList {}
class RHI_API FRHIAsyncComputeCommandListImmediate : public FRHIComputeCommandList {}
*/

struct FRHICommandBase {
	FRHICommandBase* Next = nullptr;
	virtual void ExecuteAndDestruct(FRHICommandListBase& CmdList, FRHICommandListDebugContext& DebugContext) = 0;
};
template<typename TCmd>
struct FRHICommand : public FRHICommandBase {
	void ExecuteAndDestruct(FRHICommandListBase& CmdList, FRHICommandListDebugContext& Context) override final
	{
		TCmd *ThisCmd = static_cast<TCmd*>(this);
		ThisCmd->Execute(CmdList);
		ThisCmd->~TCmd();
	}
};
#define FRHICOMMAND_MACRO(CommandType) struct CommandType : public FRHICommand<CommandType>
#define INTERNAL_DECORATOR(Method) CmdList.GetContext().Method

class RHI_API FRHICommandListBase : public FNoncopyable {
	FRHICommandBase* Root;
	FRHICommandBase** CommandLink;
	IRHICommandContext* Context;
	IRHIComputeContext* ComputeContext;

	FORCEINLINE_DEBUGGABLE void* AllocCommand(int32 AllocSize, int32 Alignment)
	{
		FRHICommandBase* Result = (FRHICommandBase*) MemManager.Alloc(AllocSize, Alignment);
		++NumCommands;
		*CommandLink = Result;
		CommandLink = &Result->Next;
		return Result;
	}
	FORCEINLINE template <typename TCmd> void* AllocCommand() {return AllocCommand(sizeof(TCmd), alignof(TCmd));}
}
#define ALLOC_COMMAND(CommandType) new ( AllocCommand(sizeof(CommandType), alignof(CommandType)) ) CommandType 	// new CommandType

class RHI_API FRHIComputeCommandList : public FRHICommandListBase {};
class RHI_API FRHICommandList : public FRHIComputeCommandList {};
class RHI_API FRHICommandListImmediate : public FRHICommandList {};



RHICmdList.DrawPrimitive(0, 2, 1); 
=>
class RHI_API FRHICommandList : public FRHIComputeCommandList {
	FORCEINLINE_DEBUGGABLE void DrawPrimitive(uint32 BaseVertexIndex, uint32 NumPrimitives, uint32 NumInstances)
	{
		if (Bypass())
		{
			GetContext().RHIDrawPrimitive(BaseVertexIndex, NumPrimitives, NumInstances);
			return;
		}

		// ALLOC_COMMAND(FRHICommandDrawPrimitive)(BaseVertexIndex, NumPrimitives, NumInstances);
		void* placement = AllocCommand(sizeof(FRHICommandDrawPrimitive), alignof(FRHICommandDrawPrimitive));
		new (placement) FRHICommandDrawPrimitive(BaseVertexIndex, NumPrimitives, NumInstances);
	}
}

// FRHICOMMAND_MACRO(FRHICommandDrawPrimitive)												
struct FRHICommandDrawPrimitive : public FRHICommand<FRHICommandDrawPrimitive>
{
	uint32 BaseVertexIndex;
	uint32 NumPrimitives;
	uint32 NumInstances;
	FORCEINLINE_DEBUGGABLE FRHICommandDrawPrimitive(uint32 InBaseVertexIndex, uint32 InNumPrimitives, uint32 InNumInstances)
		: BaseVertexIndex(InBaseVertexIndex)
		, NumPrimitives(InNumPrimitives)
		, NumInstances(InNumInstances)
	{
	}
	RHI_API void Execute(FRHICommandListBase& CmdList);
	{
		// INTERNAL_DECORATOR(RHIDrawPrimitive)(BaseVertexIndex, NumPrimitives, NumInstances);
		CmdList.GetContext().RHIDrawPrimitive(BaseVertexIndex, NumPrimitives, NumInstances);	
	}
};

void FD3D11DynamicRHI::RHIDrawPrimitive(uint32 BaseVertexIndex,uint32 NumPrimitives,uint32 NumInstances)
{
	...
	Direct3DDeviceIMContext->DrawInstanced(VertexCount,NumInstances,BaseVertexIndex,0);
}





class RHI_API FRHICommandListExecutor
{
	FRHICommandListImmediate CommandListImmediate;
	FRHIAsyncComputeCommandListImmediate AsyncComputeCmdListImmediate;
};
FRHICommandListExecutor GRHICommandList;

class IRHIComputeContext {};
class IRHICommandContext : public IRHIComputeContext {}

class IRHICommandContextPSOFallback : public IRHICommandContext {};
class FD3D12CommandContextBase 		: public IRHICommandContext, public FD3D12AdapterChild {};
class FD3D12CommandContext 			: public FD3D12CommandContextBase {};

class RHI_API FDynamicRHI {};

class D3D11RHI_API FD3D11DynamicRHI : public FDynamicRHI, public IRHICommandContextPSOFallback {};
class FD3D12DynamicRHI 				   : public FDynamicRHI {};

FDynamicRHI* GDynamicRHI = NULL;
