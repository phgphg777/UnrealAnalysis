
// SceneRendering.cpp
#define FASTVRAM_CVAR(Name,DefaultValue)  \
static TAutoConsoleVariable<int32> CVarFastVRam_##Name(
	TEXT("r.FastVRam."#Name), 
	DefaultValue, 
	TEXT(""))

FASTVRAM_CVAR(ShadowCSM, 0); \
static TAutoConsoleVariable<int32> CVarFastVRam_ShadowCSM(
	TEXT("r.FastVRam.ShadowCSM"), 
	0, 
	TEXT(""))

struct FFastVramConfig {
	uint32 ShadowCSM;
	bool bDirty;
}
FFastVramConfig GFastVRamConfig;

FFastVramConfig::FFastVramConfig()
{
	FMemory::Memset(*this, 0);
}

void FRendererModule::BeginRenderingViewFamily(FCanvas* Canvas, FSceneViewFamily* ViewFamily)
{
	ENQUEUE_RENDER_COMMAND(UpdateFastVRamConfig)(
		[](FRHICommandList& RHICmdList)
		{
			GFastVRamConfig.Update();
		});
}

void FFastVramConfig::Update()
{
	bDirty = false;
	// ShadowCSM = TEXT("r.FastVRam.ShadowCSM") ? TexCreate_FastVRAM : TexCreate_None
	bDirty |= UpdateTextureFlagFromCVar(CVarFastVRam_ShadowCSM, ShadowCSM); 
}

bool FFastVramConfig::UpdateTextureFlagFromCVar(
	TAutoConsoleVariable<int32>& CVar, 
	uint32& InOutValue)
{
	uint32 OldValue = InOutValue;
	int32 CVarValue = CVar.GetValueOnRenderThread();
	InOutValue = TexCreate_None;
	if (CVarValue == 1)
	{
		InOutValue = TexCreate_FastVRAM;
	}
	else if (CVarValue == 2)
	{
		InOutValue = TexCreate_FastVRAM | TexCreate_FastVRAMPartialAlloc;
	}
	return OldValue != InOutValue;
}

// ShadowSetup.cpp
void FSceneRenderer::AllocateCSMDepthTargets(FRHICommandListImmediate& RHICmdList, const TArray<FProjectedShadowInfo*, SceneRenderingAllocator>& WholeSceneDirectionalShadows)
{
	FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(WholeSceneAtlasSize, PF_ShadowDepth, FClearValueBinding::DepthOne, TexCreate_None, TexCreate_DepthStencilTargetable | TexCreate_ShaderResource, false));
	Desc.Flags |= GFastVRamConfig.ShadowCSM; // TexCreate_FastVRAM or TexCreate_None
}