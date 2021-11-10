// PostProcessing.cpp
TAutoConsoleVariable<int32> CVarPostProcessingAfterDLSSTranslucencyWithTAA(
	TEXT("r.PostProcessing.AfterDLSSTranslucencyWithTAA"),
	0,
	TEXT("Will add TAA pass for after DLSS translucency texture."),
	ECVF_RenderThreadSafe);

// RendererNGX.cpp
TAutoConsoleVariable<int32> CVarNGXDLSSPerfQualitySetting(
	TEXT("r.NGX.DLSS.Quality"),
	1,
	TEXT("DLSS Performance/Quality setting.\n")
	TEXT("0: Performance, 1: Balanced, 2: Quality, 3: Ultra Performance, 4: Ultra Quality."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<bool> CVarNGXDLSSEnablePlanarReflections(
	TEXT("r.NGX.DLSS.PlanarReflections.Enable"),
	false,
	TEXT("Enable DLSS for planar reflections.\n"),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<bool> CVarNGXDLSSEnableReflectionCaptures(
	TEXT("r.NGX.DLSS.ReflectionCaptures.Enable"),
	false,
	TEXT("Enable DLSS for reflection captures.\n"),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<bool> CVarNGXDLSSEnableSceneCaptures(
	TEXT("r.NGX.DLSS.SceneCaptures.Enable"),
	false,
	TEXT("Enable DLSS for scene captures.\n"),
	ECVF_RenderThreadSafe);
	
// PostProcessDLSS.cpp
TAutoConsoleVariable<float> CVarNGXDLSSSharpness(
	TEXT("r.NGX.DLSS.Sharpness"),
	0.0f,
	TEXT("-1 to 1: Sharpening to apply to the DLSS pass"),
	ECVF_RenderThreadSafe);

TAutoConsoleVariable<int32> CVarNGXDLSSReuseRenderTargets(
	TEXT("r.NGX.DLSS.ReuseRenderTargets"),
	1,
	TEXT("0: use DLSS input/output rendertargets allocated each frame by the RDG\n")
	TEXT("1: allocate input/output render targets independently from the RDG and reuse them between frames (if compatible), which can reduce DLSS stutter, but might increase GPU memory usage (default)"),
	ECVF_RenderThreadSafe);

TAutoConsoleVariable<int32> CVarNGXDLSSUseSeparateTranslucency(
	TEXT("r.NGX.DLSS.UseSeparateTranslucency"),
	1,
	TEXT("0: don't pass separate translucency image into DLSS\n")
	TEXT("1: pass separate translucency image into DLSS (default)"),
	ECVF_RenderThreadSafe);

TAutoConsoleVariable<int32> CVarNGXDLSSUseGBuffer(
	TEXT("r.NGX.DLSS.UseGBuffer"),
	1,
	TEXT("0: don't pass GBuffer images into DLSS\n")
	TEXT("1: pass GBuffer images into DLSS (default)"),
	ECVF_RenderThreadSafe);


+ Runtime\RHI\Public\RHINGX.h
+ Runtime\RHI\Private\RHINGX.cpp
+ Runtime\Windows\D3D11RHI\Private\D3D11CommandsNGX.cpp
+ Runtime\D3D12RHI\Private\D3D12CommandsNGX.cpp
+ Runtime\Engine\Public\ScreenPercentageDriverNGX.h
+ Runtime\Engine\Private\ScreenPercentageDriverNGX.cpp
+ Runtime\Renderer\Private\RendererNGX.cpp
+ Runtime\Renderer\Private\PostProcess\PostProcessDLSS.h
+ Runtime\Renderer\Private\PostProcess\PostProcessDLSS.cpp
+ Shaders\Private\PostProcessVelocityCombineNGX.usf
Shaders\Private\PostProcessTemporalAA.usf
Shaders\Private\ComposeSeparateTranslucency.usf
Shaders\Private\DiaphragmDOF\DOFRecombine.usf


1. Copy Engine\Source\ThirdParty\NVIDIA\NGX  
//2. UE4 project properties의 include directory에 Engine\Source\ThirdParty\NVIDIA\NGX\include 추가
2. 솔루션 탐색기의 UE4\Source\ThirdParty\NVIDIA\NGX 추가,
3. E:\PHG\UE4\Engine\Binaries\ThirdParty\NVIDIA\NGX\Win64\nvngx_dlss.dll 복사

4. RHI
Runtime\RHI\Public\RHI.h
[
	extern RHI_API bool GRHISupportsNGXDLSS;
]
+ Runtime\RHI\Public\RHINGX.h
+ Runtime\RHI\Private\RHINGX.cpp
Runtime\RHI\Public\RHICommandList.h
[
	#include "RHICommandListNGXRHICommands.inl"
	class RHI_API FRHICommandList : public FRHIComputeCommandList {
		#include "RHICommandListNGXMethods.inl"	
	}
	class RHI_API FRHICommandListImmediate : public FRHICommandList {
		FORCEINLINE void GetDLSSOptimalSettings(const FDLSSParameters& , FDLSSOptimalSettings& ) {}
	}
]
Runtime\RHI\Public\RHICommandListCommandExecutes.inl
[
	#include "RHICommandListCommandExecutesNGX.inl"
]
Runtime\RHI\Public\RHIContext.h
[
	class IRHICommandContext : public IRHIComputeContext {
		virtual void RHIInitializeDLSS(const FDLSSArguments& InArguments);
		virtual void RHIExecuteDLSS(const FDLSSArguments& InArguments);
	}
]
Runtime\RHI\RHI.Build.cs
[
	PublicDependencyModuleNames.AddRange( new string[] {"NGX"} );
]

5. D3D11RHI
Runtime\Windows\D3D11RHI\D3D11RHI.Build.cs
[
	PublicDependencyModuleNames.AddRange( new string[] {"NGX"} );
]
Runtime\Windows\D3D11RHI\Private\Windows\WindowsD3D11Device.cpp
[
	void FD3D11DynamicRHI::InitD3DDevice()
	{
		NVSDK_NGX_D3D11_Init(NGXAppId, , Direct3DDevice, , NVSDK_NGX_Version_API);

		if (NVSDK_NGX_Result_Success == Status)
		{
			GRHISupportsNGXDLSS = DlssAvailable != 0;
		}
	}
]
// Runtime\Windows\D3D11RHI\Private\Windows\WindowsD3D11Viewport.cpp
Runtime\Windows\D3D11RHI\Private\D3D11RHIPrivate.h
[
	#include "RHINGX.h"
	#include "nvsdk_ngx.h"
	class D3D11RHI_API FD3D11DynamicRHI : public FDynamicRHI, public IRHICommandContextPSOFallback {
		virtual void RHIInitializeDLSS(const FDLSSArguments& InArguments) final override;
		virtual void RHIExecuteDLSS(const FDLSSArguments& InArguments) final override;
		virtual void RHIGetDLSSOptimalSettings(const FDLSSParameters& InParams, FDLSSOptimalSettings& OutSettings)  final override;
	}
]
+ Runtime\Windows\D3D11RHI\Private\D3D11CommandsNGX.cpp
[
	void FD3D11DynamicRHI::RHIGetDLSSOptimalSettings(const FDLSSParameters& InParams, FDLSSOptimalSettings& OutSettings) {}
	void FD3D11DynamicRHI::RHIInitializeDLSS(const FDLSSArguments& InArguments) {}
	void FD3D11DynamicRHI::RHIExecuteDLSS(const FDLSSArguments& InArguments) {}
]
Runtime\Windows\D3D11RHI\Private\D3D11Device.cpp
[
	void FD3D11DynamicRHI::Shutdown()
	{
		if (RHISupportsNGX())
		{
			NVSDK_NGX_D3D11_Shutdown();
		}
	}
]

6. D3D12RHI
Runtime\D3D12RHI\D3D12RHI.Build.cs
[
	PublicDependencyModuleNames.AddRange( new string[] {"NGX"} );
]
Runtime\D3D12RHI\Private\Windows\WindowsD3D12Device.cpp
[
	void FD3D12DynamicRHI::Init() {}
]
Runtime\D3D12RHI\Private\D3D12CommandContext.h
[
	#include "RHINGX.h"
	#include "nvsdk_ngx.h"
	class FD3D12CommandContext : public FD3D12CommandContextBase, public FD3D12DeviceChild {
		virtual void RHIInitializeDLSS(const FDLSSArguments& InArguments) final override;
		virtual void RHIExecuteDLSS(const FDLSSArguments& InArguments) final override;
		virtual void RHIGetDLSSOptimalSettings(const FDLSSParameters& InParams, FDLSSOptimalSettings& OutSettings)  final override;
	}
]
+ Runtime\D3D12RHI\Private\D3D12CommandsNGX.cpp
[
	void FD3D11DynamicRHI::RHIGetDLSSOptimalSettings(const FDLSSParameters& InParams, FDLSSOptimalSettings& OutSettings) {}
	void FD3D11DynamicRHI::RHIInitializeDLSS(const FDLSSArguments& InArguments) {}
	void FD3D11DynamicRHI::RHIExecuteDLSS(const FDLSSArguments& InArguments) {}
]
Runtime\D3D12RHI\Private\D3D12RHI.cpp
[
	void FD3D12DynamicRHI::Shutdown()
	{
		if (RHISupportsNGX())
		{
			NVSDK_NGX_D3D12_Shutdown();
		}
	}
]

6. Engine
Runtime\Engine\Classes\Engine\Scene.h 
[
	enum EAntiAliasingMethod {
		AAM_DLSS
	}
]
Runtime\Engine\Classes\Engine\RendererSettings.h
[
	//UPROPERTY(config, EditAnywhere, Category = NGX, meta = (ConsoleVariable = "r.NGX.AppId"))
	//FString NGXAppId;
	//UPROPERTY(config, EditAnywhere, Category = DefaultSettings, meta = (ConsoleVariable = "r.DefaultFeature.AntiAliasing")
	//TEnumAsByte<EAntiAliasingMethod> DefaultFeatureAntiAliasing;
]
Runtime\Engine\Public\SceneView.h
[
	void SetupViewRectUniformBufferParameters(,,,,, float DLSSScaleFactorAndInvScaleFactor = 1.0f) const;

	class ENGINE_API FSceneViewFamily {
		float DLSSScaleFactor = 1.0f;
	}
]
Runtime\Engine\Private\SceneView.cpp
[
	TAutoConsoleVariable<int32> CVarDefaultAntiAliasing(TEXT("r.DefaultFeature.AntiAliasing"), 2, TEXT(" 4: DLSS"));

	FSceneView::FSceneView(const FSceneViewInitOptions& InitOptions)
	{
		if(AntiAliasingMethod == AAM_DLSS)
		{
			PrimaryScreenPercentageMethod = EPrimaryScreenPercentageMethod::TemporalUpscale;
		}
	}

	void FSceneView::SetupViewRectUniformBufferParameters(,,,,, float DLSSScaleFactor) 
	{}
]
Runtime\Engine\Public\SceneManagement.h
[
	class FSceneViewStateInterface {
		TSharedRef<FPerViewDLSSState> DLSSState = MakeShared<FPerViewDLSSState>();
	}
]
+ Runtime\Engine\Public\ScreenPercentageDriverNGX.h
+ Runtime\Engine\Private\ScreenPercentageDriverNGX.cpp
[
	void FScreenPercentageDriverNGX::ComputePrimaryResolutionFractions_RenderThread(
		TArray<FSceneViewScreenPercentageConfig>& OutViewScreenPercentageConfigs) const
	{
		OutViewScreenPercentageConfigs[i].PrimaryResolutionFraction 
			= ViewFamily.DLSSScaleFactor * (ViewFamily.Views[i]->FinalPostProcessSettings.ScreenPercentage / 100.0f);
	}
]
Runtime\Engine\Private\GameViewportClient.cpp
[
	void UGameViewportClient::Draw(FViewport* InViewport, FCanvas* SceneCanvas)
	{
		ViewFamily.SetScreenPercentageInterface(new FScreenPercentageDriverNGX(
			ViewFamily, AllowPostProcessSettingsScreenPercentage));
	}
]
Editor\UnrealEd\Private\EditorViewportClient.cpp
[
	void FEditorViewportClient::Draw(FViewport* InViewport, FCanvas* Canvas)
	{
		// In editor viewport, we ignore r.ScreenPercentage and FPostProcessSettings::ScreenPercentage by design.
		ViewFamily.SetScreenPercentageInterface(new FLegacyScreenPercentageDriver(
			ViewFamily, GlobalResolutionFraction, false));
	}
]
//Runtime\Engine\Classes\GameFramework\GameUserSettings.h
//Runtime\Engine\Private\GameUserSettingsNGX.cpp

7. PreRender
Runtime\Renderer\Private\DeferredShadingRenderer.cpp
[
	PrepareViewRectsForRendering(RHICmdList);
]
Runtime\Renderer\Private\MobileShadingRenderer.cpp
[
	PrepareViewRectsForRendering(RHICmdList);
]
Runtime\Renderer\Private\SceneHitProxyRendering.cpp
[
	PrepareViewRectsForRendering(RHICmdList);
]
Runtime\Renderer\Private\SceneRendering.h
[
	class FSceneRenderer {
		void PrepareViewRectsForRendering(FRHICommandListImmediate& RHICmdList);
	}

	struct FFastVramConfig {
		uint32 SeparateTranslucencyAfterDLSS;
		uint32 SeparateTranslucencyModulateAfterDLSS;
	}
]
Runtime\Renderer\Private\SceneRendering.cpp
[
	void FSceneRenderer::PrepareViewRectsForRendering(FRHICommandListImmediate& RHICmdList)
	{
		NGXViewProcessing(Scene, Views, ViewFamily, RHICmdList);
	}
]
Runtime\Renderer\Private\ScenePrivate.h
[
	class FSceneViewState : public FSceneViewStateInterface, public FRenderResource {
		void DLSSReleasePreviousRenderTargets();
		FLatentGPUTimer SeparateTranslucencyAfterDLSSTimer;
		FLatentGPUTimer SeparateTranslucencyAfterDLSSModulateTimer;
	}
]
Runtime\Renderer\Private\RendererScene.cpp
[
	FSceneViewState::FSceneViewState():
	, SeparateTranslucencyAfterDLSSTimer(TimerQueryPool)
	, SeparateTranslucencyAfterDLSSModulateTimer(TimerQueryPool) {}
]
+ Runtime\Renderer\Private\RendererNGX.cpp
[
	void NGXViewProcessing()
	{
		if (GDLSSOptimalSettings.Find(DLSSParameters) == nullptr)
		{
			FDLSSOptimalSettings OptimalSettings;
			RHICmdList.GetDLSSOptimalSettings(DLSSParameters, OptimalSettings);
			GDLSSOptimalSettings.FindOrAdd(DLSSParameters) = OptimalSettings;
		}

		float XScale = float(GDLSSOptimalSettings[DLSSParameters].RenderSize.X) / Views[0].UnscaledViewRect.Width();
		float YScale = float(GDLSSOptimalSettings[DLSSParameters].RenderSize.Y) / Views[0].UnscaledViewRect.Height();
		ViewFamily.DLSSScaleFactor = FMath::Max(XScale, YScale);
	}
]

8. TranslucentRendering
Runtime\Renderer\Private\TranslucencyPass.h
[
	namespace ETranslucencyPass {
		enum Type {
			TPT_TranslucencyAfterDLSS,
			TPT_TranslucencyAfterDLSSModulate,
		}
	}
]
Runtime\Renderer\Public\MeshPassProcessor.h
[
	namespace EMeshPass {
		enum Type {
			TranslucencyAfterDLSS,
			TranslucencyAfterDLSSModulate,
		}
	}
]

Runtime\Renderer\Private\TranslucentRendering.cpp
[
]

9. PostProcess
Runtime\Renderer\Private\DeferredShadingRenderer.h
[
]
Runtime\Renderer\Private\DeferredShadingRenderer.cpp
[
	void FDeferredShadingSceneRenderer::Render(FRHICommandListImmediate& RHICmdList)
	{
		PrepareViewRectsForRendering(RHICmdList);
		
		if (bAnyViewUsingDLSS)
			SceneContext.AdjustGBufferRefCount(RHICmdList, +1);

		PostProcessingInputs.SeparateTranslucencyAfterDLSS = RegisterExternalTextureWithFallback(GraphBuilder, SceneContext.SeparateTranslucencyRTAfterDLSS, SceneContext.GetSeparateTranslucencyDummy(), TEXT("SeparateTranslucencyAfterDLSS"));
		PostProcessingInputs.SeparateModulationAfterDLSS = RegisterExternalTextureWithFallback(GraphBuilder, SceneContext.SeparateTranslucencyModulateRTAfterDLSS, SceneContext.GetSeparateTranslucencyModulateDummy(), TEXT("SeparateModulateAfterDLSS"));
		PostProcessingInputs.SeparateTranslucencyDepthAfterDLSS = RegisterExternalTextureWithFallback(GraphBuilder, SceneContext.DownsampledTranslucencyDepthRTAfterDLSS, SceneContext.GetSeparateDepthDummy(), TEXT("UpsampledTranslucencyDepthRTAfterDLSS"));
		
		if (bAnyViewUsingDLSS)
			SceneContext.AdjustGBufferRefCount(RHICmdList, -1);
	}
]
Runtime\Renderer\Private\PostProcess\SceneRendertargets.h
[
	class RENDERER_API FSceneRenderTargets : public FRenderResource {
		TRefCountPtr<IPooledRenderTarget> SeparateTranslucencyRTAfterDLSS;
		TRefCountPtr<IPooledRenderTarget> SeparateTranslucencyModulateRTAfterDLSS;
		TRefCountPtr<IPooledRenderTarget> DownsampledTranslucencyDepthRTAfterDLSS;
		FVector2D SeparateTranslucencyScaleForDLSS = FVector2D(1.0f, 1.0f);
	}
]
Runtime\Renderer\Private\PostProcess\SceneRendertargets.cpp
[
]
Runtime\Renderer\Private\PostProcess\PostProcessing.h
[
	struct FPostProcessingInputs {
		FRDGTextureRef SeparateTranslucencyAfterDLSS = nullptr;
		FRDGTextureRef SeparateModulationAfterDLSS = nullptr;
		FRDGTextureRef SeparateTranslucencyDepthAfterDLSS = nullptr;
	}
]
Runtime\Renderer\Private\PostProcess\PostProcessing.cpp
[
]
Runtime\Renderer\Private\PostProcess\DiaphragmDOF.cpp
Runtime\Renderer\Private\PostProcess\PostProcessTemporalAA.h
Runtime\Renderer\Private\PostProcess\PostProcessTemporalAA.cpp
+ Runtime\Renderer\Private\PostProcess\PostProcessDLSS.h
+ Runtime\Renderer\Private\PostProcess\PostProcessDLSS.cpp
Runtime\Renderer\Private\PostProcess\PostProcessCompositeEditorPrimitives.cpp

10. BasePass
Runtime\Engine\Classes\Particles\ParticleSystemComponent.h
[
	class ENGINE_API UParticleSystemComponent : public UFXSystemComponent {
		UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Rendering)
		uint8 bRenderAfterDLSS : 1;
	}
]
Runtime\Engine\Public\PrimitiveSceneProxy.h
[
	class FPrimitiveSceneProxy {
		/** If true this primitive Renders in the after DLSS pass, only for rendering particles */
		uint8 bRenderInAfterDLSSPass : 1;
	}
]
Runtime\Engine\Public\PrimitiveViewRelevance.h
[
	struct FPrimitiveViewRelevance : public FMaterialRelevance {
		uint8 bRenderInAfterDLSSPass : 1;
	}
]
Runtime\Engine\Private\Particles\ParticleSystemRender.cpp
[
	FParticleSystemSceneProxy::FParticleSystemSceneProxy()
	{
		bRenderInAfterDLSSPass = Component->bRenderAfterDLSS;
	}
	FPrimitiveViewRelevance FParticleSystemSceneProxy::GetViewRelevance()
	{
		Result.bRenderInAfterDLSSPass = bRenderInAfterDLSSPass;
	}
]
Runtime\Renderer\Private\SceneVisibility.cpp
[
]
Runtime\Renderer\Private\BasePassRendering.cpp
[
]
Runtime\Renderer\Private\MeshDrawCommands.cpp
[
	void FParallelMeshDrawCommandPass::DispatchPassSetup()
	{
		switch (PassType)
		{
			case EMeshPass::TranslucencyAfterDLSS: 
				TaskContext.TranslucencyPass = ETranslucencyPass::TPT_TranslucencyAfterDLSS; 
				break;
			case EMeshPass::TranslucencyAfterDLSSModulate: 
				TaskContext.TranslucencyPass = ETranslucencyPass::TPT_TranslucencyAfterDLSSModulate; 
				break;
		}
	}
]

11. Renderer etc
Runtime\Renderer\Private\IndirectLightRendering.cpp
[
	static TAutoConsoleVariable<int32> CVarNGXRTReflectionsEnableTAA(
		TEXT("r.NGX.Raytracing.Reflections.EnableTAABeforeDLSS"),
		1,
		TEXT("Enables TAA after raytraced reflections, before DLSS"),
		ECVF_RenderThreadSafe
	);
]
Runtime\Renderer\Private\VelocityRendering.cpp
[
]





@ PostProcessing Pass
AddPostProcessMaterialChain(BL_BeforeTranslucency)
DiaphragmDOF::AddPasses()
AddSeparateTranslucencyCompositionPass()
AddPostProcessMaterialChain(BL_BeforeTonemapping)
AddDLSSPass() || AddTemporalAAPass()

AddPostProcessMaterialChain(BL_SSRInput)
AddMotionBlurPass()
AddDownsamplePass()
AddBasicEyeAdaptationPass()
AddBloomSetupPass()
AddBloomPass()
AddLensFlaresPass()
AddPostProcessMaterialPass(BL_ReplacingTonemapper) || (AddCombineLUTPass() && AddTonemapPass())







PostProcessing.cpp  
else if (ShouldRenderScreenSpaceReflections(View))
{
}

MeshPassProcessor.h
namespace EMeshPass
{
	enum Type
	{
		//fix by darby84. 20200924. Editing distortion render pass .~Start
		DistortionAfterTranslucency,
		//fix by darby84. 20200924. Editing distortion render pass .~End
	}
}
