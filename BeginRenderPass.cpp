
enum class ERenderTargetLoadAction : uint8 {
	ENoAction,
	ELoad,
	EClear, // meaningful
}
enum class ERenderTargetStoreAction : uint8 {
	ENoAction,
	EStore,
	EMultisampleResolve,
}

enum class ERenderTargetActions : uint8 {
	RTACTION_MAKE_MASK(ENoAction, ENoAction),
	RTACTION_MAKE_MASK(ENoAction, EStore),
	RTACTION_MAKE_MASK(ELoad, ENoAction),
	RTACTION_MAKE_MASK(ELoad, EStore),
	RTACTION_MAKE_MASK(ELoad, EMultisampleResolve),
	RTACTION_MAKE_MASK(EClear, ENoAction),
	RTACTION_MAKE_MASK(EClear, EStore),
	RTACTION_MAKE_MASK(EClear, EMultisampleResolve),
};

enum class EDepthStencilTargetActions : uint8 {
	RTACTION_MAKE_MASK(ENoAction, ENoAction, ENoAction, ENoAction),
	RTACTION_MAKE_MASK(ENoAction, ENoAction, ENoAction, EStore),
	RTACTION_MAKE_MASK(ENoAction, ENoAction, EClear   , EStore),
	RTACTION_MAKE_MASK(ENoAction, EStore   , ENoAction, EStore),
	RTACTION_MAKE_MASK(ELoad    , ENoAction, ENoAction, ENoAction),
	RTACTION_MAKE_MASK(ELoad    , ENoAction, ELoad    , ENoAction),
	RTACTION_MAKE_MASK(ELoad    , ENoAction, ELoad    , EStore),
	RTACTION_MAKE_MASK(ELoad    , EStore   , ELoad    , EStore),
	RTACTION_MAKE_MASK(ELoad    , EStore   , EClear   , EStore),
	RTACTION_MAKE_MASK(EClear   , ENoAction, EClear   , ENoAction),
	RTACTION_MAKE_MASK(EClear   , ENoAction, EClear   , EStore),
	RTACTION_MAKE_MASK(EClear   , EStore   , EClear   , ENoAction),
	RTACTION_MAKE_MASK(EClear   , EStore   , EClear   , EStore),
	RTACTION_MAKE_MASK(EClear   , EMultisampleResolve, EClear, ENoAction),
	RTACTION_MAKE_MASK(EClear   , ENoAction          , EClear, EMultisampleResolve),
};

class FExclusiveDepthStencil {
	enum Type {
		DepthNop =		0x00,
		DepthRead =		0x01,
		DepthWrite =	0x02, // may be meaningful
		DepthMask =		0x0f, 
		StencilNop =	0x00,
		StencilRead =	0x10,
		StencilWrite =	0x20,
		StencilMask =	0xf0,

		DepthNop_StencilNop = DepthNop + StencilNop,
		DepthNop_StencilRead = DepthNop + StencilRead,
		DepthNop_StencilWrite = DepthNop + StencilWrite,
		DepthRead_StencilNop = DepthRead + StencilNop,
		DepthRead_StencilRead = DepthRead + StencilRead,
		DepthRead_StencilWrite = DepthRead + StencilWrite,
		DepthWrite_StencilNop = DepthWrite + StencilNop,
		DepthWrite_StencilRead = DepthWrite + StencilRead,
		DepthWrite_StencilWrite = DepthWrite + StencilWrite,
	};
}

struct FRHIRenderPassInfo {
	struct FColorEntry
	{
		FRHITexture* RenderTarget;
		FRHITexture* ResolveTarget;
		int32 ArraySlice;
		uint8 MipIndex;
		ERenderTargetActions Action;
	};
	FColorEntry ColorRenderTargets[8];

	struct FDepthStencilEntry
	{
		FRHITexture* DepthStencilTarget;
		FRHITexture* ResolveTarget;
		EDepthStencilTargetActions Action;
		FExclusiveDepthStencil ExclusiveDepthStencil;
	};
	FDepthStencilEntry DepthStencilRenderTarget;
}

void IRHICommandContext::RHIBeginRenderPass(const FRHIRenderPassInfo& InInfo, const TCHAR* InName)
{
	FRHISetRenderTargetsInfo RTInfo;
	InInfo.ConvertToRenderTargetsInfo(RTInfo);
	RHISetRenderTargetsAndClear(RTInfo);
	
	RenderPassInfo = InInfo;
}

class FRHIRenderTargetView {
	FRHITexture* Texture;
	uint32 MipIndex;
	uint32 ArraySliceIndex;
	ERenderTargetLoadAction LoadAction;
	ERenderTargetStoreAction StoreAction;
}
class FRHIDepthRenderTargetView {
	FRHITexture* Texture;
	ERenderTargetLoadAction		DepthLoadAction;
	ERenderTargetStoreAction	DepthStoreAction;
	ERenderTargetLoadAction		StencilLoadAction;
	ERenderTargetStoreAction	StencilStoreAction;
	FExclusiveDepthStencil		DepthStencilAccess;
}

class FRHISetRenderTargetsInfo {
	FRHIRenderTargetView ColorRenderTarget[8];	
	int32 NumColorRenderTargets;
	bool bClearColor;

	FRHIRenderTargetView ColorResolveRenderTarget[8];	
	bool bHasResolveAttachments;

	FRHIDepthRenderTargetView DepthStencilRenderTarget;	
	bool bClearDepth;
	bool bClearStencil;
}

void FRHIRenderPassInfo::ConvertToRenderTargetsInfo(FRHISetRenderTargetsInfo& OutRTInfo) const
{
	for (int32 Index = 0; Index < 8; ++Index)
	{
		if (!ColorRenderTargets[Index].RenderTarget)
		{
			break;
		}

		ERenderTargetLoadAction LoadAction = GetLoadAction(ColorRenderTargets[Index].Action);
		ERenderTargetStoreAction StoreAction = GetStoreAction(ColorRenderTargets[Index].Action);
		++OutRTInfo.NumColorRenderTargets;
		OutRTInfo.ColorRenderTarget[Index].Texture = ColorRenderTargets[Index].RenderTarget;
		OutRTInfo.ColorRenderTarget[Index].LoadAction = LoadAction;
		OutRTInfo.ColorRenderTarget[Index].StoreAction = StoreAction;
		OutRTInfo.ColorRenderTarget[Index].ArraySliceIndex = ColorRenderTargets[Index].ArraySlice;
		OutRTInfo.ColorRenderTarget[Index].MipIndex = ColorRenderTargets[Index].MipIndex;
		OutRTInfo.bClearColor |= (LoadAction == ERenderTargetLoadAction::EClear);

		if (ColorRenderTargets[Index].ResolveTarget)
		{
			OutRTInfo.bHasResolveAttachments = true;
			OutRTInfo.ColorResolveRenderTarget[Index] = OutRTInfo.ColorRenderTarget[Index];
			OutRTInfo.ColorResolveRenderTarget[Index].Texture = ColorRenderTargets[Index].ResolveTarget;
		}
	}

	ERenderTargetActions DepthActions = GetDepthActions(DepthStencilRenderTarget.Action);
	ERenderTargetActions StencilActions = GetStencilActions(DepthStencilRenderTarget.Action);
	ERenderTargetLoadAction DepthLoadAction = GetLoadAction(DepthActions);
	ERenderTargetStoreAction DepthStoreAction = GetStoreAction(DepthActions);
	ERenderTargetLoadAction StencilLoadAction = GetLoadAction(StencilActions);
	ERenderTargetStoreAction StencilStoreAction = GetStoreAction(StencilActions);

	OutRTInfo.DepthStencilRenderTarget = FRHIDepthRenderTargetView(
		epthStencilRenderTarget.DepthStencilTarget,
		DepthLoadAction,
		DepthStoreAction,
		StencilLoadAction,
		StencilStoreAction,
		DepthStencilRenderTarget.ExclusiveDepthStencil);
	OutRTInfo.bClearDepth = (DepthLoadAction == ERenderTargetLoadAction::EClear);
	OutRTInfo.bClearStencil = (StencilLoadAction == ERenderTargetLoadAction::EClear);
}

void FD3D11DynamicRHI::RHISetRenderTargetsAndClear(const FRHISetRenderTargetsInfo& RenderTargetsInfo)
{
	RHISetRenderTargets(RenderTargetsInfo.NumColorRenderTargets,
		RenderTargetsInfo.ColorRenderTarget,
		&RenderTargetsInfo.DepthStencilRenderTarget);

	if (RenderTargetsInfo.bClearColor || RenderTargetsInfo.bClearStencil || RenderTargetsInfo.bClearDepth)
	{
		...
		RHIClearMRTImpl(RenderTargetsInfo.bClearColor, RenderTargetsInfo.NumColorRenderTargets, ClearColors, RenderTargetsInfo.bClearDepth, DepthClear, RenderTargetsInfo.bClearStencil, StencilClear);
	}
}

void FD3D11DynamicRHI::RHISetRenderTargets(
	uint32 NewNumSimultaneousRenderTargets,
	const FRHIRenderTargetView* NewRenderTargetsRHI,
	const FRHIDepthRenderTargetView* NewDepthStencilTargetRHI)
{
	bool bTargetChanged = false;
	FD3D11TextureBase* NewDepthStencilTarget = GetD3D11TextureFromRHITexture(NewDepthStencilTargetRHI ? NewDepthStencilTargetRHI->Texture : nullptr);
	ID3D11DepthStencilView* DepthStencilView = NULL;
	assume(NewDepthStencilTarget);

	CurrentDSVAccessType = NewDepthStencilTargetRHI->DepthStencilAccess;
	DepthStencilView = NewDepthStencilTarget->GetDepthStencilView(CurrentDSVAccessType);
	ConditionalClearShaderResource(NewDepthStencilTarget, false);
	
	if(CurrentDepthStencilTarget != DepthStencilView)
	{
		CurrentDepthTexture = NewDepthStencilTarget;
		CurrentDepthStencilTarget = DepthStencilView;
		bTargetChanged = true;
	}

	uint32 CurrentFrame = PresentCounter;
	const EResourceTransitionAccess CurrentAccess = NewDepthStencilTarget->GetCurrentGPUAccess();
	const uint32 LastFrameWritten = NewDepthStencilTarget->GetLastFrameWritten();
	const bool bReadable = CurrentAccess == EResourceTransitionAccess::EReadable;
	const bool bDepthWrite = NewDepthStencilTargetRHI->DepthStencilAccess.IsDepthWrite();
	
	if (bDepthWrite)
	{
		if (bReadable)
		{
			NewDepthStencilTarget->SetCurrentGPUAccess(EResourceTransitionAccess::EWritable);
		}
		NewDepthStencilTarget->SetDirty(true, CurrentFrame);
	}

	...

	if(bTargetChanged)
	{
		CommitRenderTargets(true);
		CurrentUAVMask = 0;
	}
}




{
	RHICmdList.BeginRenderPass(RPInfo, );
	RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
	GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<...>::GetRHI();
	SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);
}

void FRHICommandList::BeginRenderPass(const FRHIRenderPassInfo& InInfo, const TCHAR* Name)
{
	GetContext().RHIBeginRenderPass(InInfo, Name);
	CacheActiveRenderTargets(InInfo);
	ResetSubpass(InInfo.SubpassHint);
	Data.bInsideRenderPass = true;
}

void FRHICommandListBase::CacheActiveRenderTargets(const FRHIRenderPassInfo& Info)
{
	FRHISetRenderTargetsInfo RTInfo;
	Info.ConvertToRenderTargetsInfo(RTInfo);

	PSOContext.CachedNumSimultanousRenderTargets = RTInfo.NumColorRenderTargets;
	for (uint32 RTIdx = 0; RTIdx < PSOContext.CachedNumSimultanousRenderTargets; ++RTIdx)
	{
		PSOContext.CachedRenderTargets[RTIdx] = RTInfo.ColorRenderTarget[RTIdx];
	}
	PSOContext.CachedDepthStencilTarget = RTInfo.DepthStencilRenderTarget;
	PSOContext.HasFragmentDensityAttachment = RTInfo.FoveationTexture != nullptr;	
}

void FRHICommandList::ApplyCachedRenderTargets(FGraphicsPipelineStateInitializer& GraphicsPSOInit)
{
	GraphicsPSOInit.RenderTargetsEnabled = PSOContext.CachedNumSimultanousRenderTargets;

	for (uint32 i = 0; i < GraphicsPSOInit.RenderTargetsEnabled; ++i)
	if (PSOContext.CachedRenderTargets[i].Texture)
	{
		...
	}

	if (PSOContext.CachedDepthStencilTarget.Texture)
	{
		...
	}

	GraphicsPSOInit.DepthTargetLoadAction = PSOContext.CachedDepthStencilTarget.DepthLoadAction;
	GraphicsPSOInit.DepthTargetStoreAction = PSOContext.CachedDepthStencilTarget.DepthStoreAction;
	GraphicsPSOInit.StencilTargetLoadAction = PSOContext.CachedDepthStencilTarget.StencilLoadAction;
	GraphicsPSOInit.StencilTargetStoreAction = PSOContext.CachedDepthStencilTarget.StencilStoreAction;
	GraphicsPSOInit.DepthStencilAccess = PSOContext.CachedDepthStencilTarget.DepthStencilAccess;

	...
}

void SetGraphicsPipelineState(
	FRHICommandList& RHICmdList, 
	const FGraphicsPipelineStateInitializer& Initializer, 
	EApplyRendertargetOption ApplyFlags = EApplyRendertargetOption::CheckApply)
{
	FGraphicsPipelineState* PipelineState = PipelineStateCache::GetAndOrCreateGraphicsPipelineState(RHICmdList, Initializer, ApplyFlags);
	
	if (PipelineState && (PipelineState->RHIPipeline || !Initializer.bFromPSOFileCache))
	{
		RHICmdList.SetGraphicsPipelineState(PipelineState, Initializer.BoundShaderState);
	}
}
