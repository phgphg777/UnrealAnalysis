TEXT("r.VolumetricRenderTarget"), 1,
TEXT("r.VolumetricRenderTarget.UvNoiseScale"), 0.5f,
TEXT("r.VolumetricRenderTarget.Mode"), 0,
TEXT("r.VolumetricRenderTarget.UpsamplingMode"), 4,

class FSceneViewState : public FSceneViewStateInterface, public FRenderResource {
	FVolumetricRenderTargetViewStateData VolumetricCloudRenderTarget;
}

class FVolumetricRenderTargetViewStateData {
	bool bFirstTimeUsed;
	uint32 CurrentRT;
	bool bHistoryValid;
	
	int32 FrameId;
	FIntPoint CurrentPixelOffset;
	
	uint32 NoiseFrameIndex;
	uint32 NoiseFrameIndexModPattern;

	float UvNoiseScale;
	int32 Mode;
	int32 UpsamplingMode;

	FIntPoint FullResolution;

	uint32 VolumetricReconstructRTDownsampleFactor;
	FIntPoint VolumetricReconstructRTResolution;
	TRefCountPtr<IPooledRenderTarget> VolumetricReconstructRT[2];
	TRefCountPtr<IPooledRenderTarget> VolumetricReconstructRTDepth[2];

	uint32 VolumetricTracingRTDownsampleFactor;
	FIntPoint VolumetricTracingRTResolution;
	TRefCountPtr<IPooledRenderTarget> VolumetricTracingRT;
	TRefCountPtr<IPooledRenderTarget> VolumetricTracingRTDepth;
}

FVolumetricRenderTargetViewStateData::FVolumetricRenderTargetViewStateData()
	: CurrentRT(1)
	, bFirstTimeUsed(true)
	, bHistoryValid(false)
	, FullResolution(FIntPoint::ZeroValue)
	, VolumetricReconstructRTResolution(FIntPoint::ZeroValue)
	, VolumetricTracingRTResolution(FIntPoint::ZeroValue)
{
	VolumetricReconstructRTDownsampleFactor = 0;
	VolumetricTracingRTDownsampleFactor = 0;
	FrameId = 0;
	NoiseFrameIndex = 0;
	NoiseFrameIndexModPattern = 0;
	CurrentPixelOffset = FIntPoint::ZeroValue;
}

static uint32 GetMainDownsampleFactor(int32 Mode)
{
	return Mode == 0 ? 2 : 1;
}

static uint32 GetTraceDownsampleFactor(int32 Mode)
{
	return Mode < 2 ? 2 : 4;
}

void FVolumetricRenderTargetViewStateData::Initialise(
	FIntPoint& ViewRectResolutionIn,
	float InUvNoiseScale,
	int32 InMode,
	int32 InUpsamplingMode)
{
	UvNoiseScale = InUvNoiseScale;
	Mode = FMath::Clamp(InMode, 0, 2);
	UpsamplingMode = Mode == 2 ? 2 : FMath::Clamp(InUpsamplingMode, 0, 4); // if we are using mode 2 then we cannot intersect with depth and upsampling should be 2 (simple on/off intersection)

	const uint32 PreviousRT = CurrentRT;
	CurrentRT = 1 - CurrentRT;

	if (FullResolution != ViewRectResolutionIn || GetMainDownsampleFactor(Mode) != VolumetricReconstructRTDownsampleFactor || GetTraceDownsampleFactor(Mode) != VolumetricTracingRTDownsampleFactor)
	{
		VolumetricReconstructRTDownsampleFactor = GetMainDownsampleFactor(Mode);
		VolumetricTracingRTDownsampleFactor = GetTraceDownsampleFactor(Mode);
		FullResolution = ViewRectResolutionIn;
		VolumetricReconstructRTResolution = FIntPoint::DivideAndRoundUp(FullResolution, VolumetricReconstructRTDownsampleFactor);				// Half resolution
		VolumetricTracingRTResolution = FIntPoint::DivideAndRoundUp(VolumetricReconstructRTResolution, VolumetricTracingRTDownsampleFactor);	// Half resolution of the volumetric buffer
		VolumetricTracingRT.SafeRelease();
		VolumetricTracingRTDepth.SafeRelease();
	}

	FIntVector CurrentTargetResVec = VolumetricReconstructRT[CurrentRT].IsValid() ? VolumetricReconstructRT[CurrentRT]->GetDesc().GetSize() : FIntVector::ZeroValue;
	FIntPoint CurrentTargetRes = FIntPoint::DivideAndRoundUp(FullResolution, VolumetricReconstructRTDownsampleFactor);
	if (VolumetricReconstructRT[CurrentRT].IsValid() && FIntPoint(CurrentTargetResVec) != CurrentTargetRes)
	{
		VolumetricReconstructRT[CurrentRT].SafeRelease();
		VolumetricReconstructRTDepth[CurrentRT].SafeRelease();
	}

	// Regular every frame update
	{
		bHistoryValid = VolumetricReconstructRT[PreviousRT].IsValid(); // always true except at the very first
		FrameId = (FrameId + 1) % (VolumetricTracingRTDownsampleFactor * VolumetricTracingRTDownsampleFactor);

		if (VolumetricTracingRTDownsampleFactor == 2)
		{
			static int32 OrderDithering2x2[4] = { 0, 2, 3, 1 };
			int32 LocalFrameId = OrderDithering2x2[FrameId];
			CurrentPixelOffset = FIntPoint(LocalFrameId % VolumetricTracingRTDownsampleFactor, LocalFrameId / VolumetricTracingRTDownsampleFactor);
		}
		else if (VolumetricTracingRTDownsampleFactor == 4)
		{
			static int32 OrderDithering4x4[16] = { 0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5 };
			int32 LocalFrameId = OrderDithering4x4[FrameId];
			CurrentPixelOffset = FIntPoint(LocalFrameId % VolumetricTracingRTDownsampleFactor, LocalFrameId / VolumetricTracingRTDownsampleFactor);
		}
	}
}

void FSceneRenderer::InitVolumetricRenderTargetForViews(FRDGBuilder& GraphBuilder)
{
	FViewInfo& ViewInfo = Views[0];
	if (!ShouldViewRenderVolumetricCloudRenderTarget(ViewInfo))
		return;

	FVolumetricRenderTargetViewStateData& VCRT = ViewInfo.ViewState->VolumetricCloudRenderTarget;

	VCRT.Initialise(	// TODO this is going to reallocate a buffer each time dynamic resolution scaling is applied 
		ViewInfo.ViewRect.Size(),
		TEXT("r.VolumetricRenderTarget.UvNoiseScale"), 
		TEXT("r.VolumetricRenderTarget.Mode"),
		TEXT("r.VolumetricRenderTarget.UpsamplingMode"));

	FViewUniformShaderParameters VCViewParameters = *ViewInfo.CachedViewUniformShaderParameters;
	{
		FViewMatrices ViewMatrices = ViewInfo.ViewMatrices;
		{
			FVector2D CenterCoord = FVector2D(VCRT.VolumetricReconstructRTDownsampleFactor / 2.0f);
			FVector2D TargetCoord = FVector2D(VCRT.CurrentPixelOffset) + FVector2D(0.5f, 0.5f);
			FVector2D OffsetCoord = (TargetCoord - CenterCoord) * (FVector2D(-2.0f, 2.0f) / FVector2D(VCRT.VolumetricReconstructRTResolution));
			ViewMatrices.HackRemoveTemporalAAProjectionJitter();
			ViewMatrices.HackAddTemporalAAProjectionJitter(OffsetCoord);
		}

		ViewInfo.SetupViewRectUniformBufferParameters(
			VCViewParameters,
			VCRT.VolumetricTracingRTResolution,
			FIntRect(FIntPoint(0), VCRT.VolumetricTracingRTResolution),
			ViewMatrices, );
	}

	// This uniform buffer is used for tracing pass, not related to reconstruct pass!!
	ViewInfo.VolumetricRenderTargetViewUniformBuffer = TUniformBufferRef<FViewUniformShaderParameters>::CreateUniformBufferImmediate(VCViewParameters, UniformBuffer_SingleFrame);
}

void FSceneRenderer::ReconstructVolumetricRenderTarget(FRDGBuilder& GraphBuilder)
{
	FViewInfo& View = Views[0];
	if (!ShouldViewRenderVolumetricCloudRenderTarget(View))
		return;

	FVolumetricRenderTargetViewStateData& VCRT = View.ViewState->VolumetricCloudRenderTarget;
	uint32 CurrentRT = VCRT.CurrentRT;
	assume(VCRT.GetHistoryValid());

	FRDGTextureRef PrevVolumetric = GraphBuilder.RegisterExternalTexture(VCRT.VolumetricReconstructRT[1u - CurrentRT]);
	FRDGTextureRef PrevVolumetricDepth = GraphBuilder.RegisterExternalTexture(VCRT.VolumetricReconstructRTDepth[1u - CurrentRT]);
	FRDGTextureRef DstVolumetric = GraphBuilder.RegisterExternalTexture(VCRT.VolumetricReconstructRT[CurrentRT]);
	FRDGTextureRef DstVolumetricDepth = GraphBuilder.RegisterExternalTexture(VCRT.VolumetricReconstructRTDepth[CurrentRT]);
	FRDGTextureRef SrcTracingVolumetric = GraphBuilder.RegisterExternalTexture(VCRT.VolumetricTracingRT);
	FRDGTextureRef SrcTracingVolumetricDepth = GraphBuilder.RegisterExternalTexture(VCRT.VolumetricTracingRTDepth);
	FRDGTextureRef SceneDepth = GraphBuilder.RegisterExternalTexture(VCRT.Mode == 0 ? View.HalfResDepthSurfaceCheckerboardMinMax : FSceneRenderTargets::Get(GraphBuilder.RHICmdList).SceneDepthZ);

	FIntPoint SrcSize = SrcTracingVolumetric->Desc.Extent;
	FIntPoint DstSize = DstVolumetric->Desc.Extent;
	FIntPoint PrevSize = PrevVolumetric->Desc.Extent;
	
	FReconstructVolumetricRenderTargetPS::FParameters* PassParameters = GraphBuilder.AllocParameters<FReconstructVolumetricRenderTargetPS::FParameters>();
	PassParameters->RenderTargets[0] = FRenderTargetBinding(DstVolumetric, ERenderTargetLoadAction::ENoAction);
	PassParameters->RenderTargets[1] = FRenderTargetBinding(DstVolumetricDepth, ERenderTargetLoadAction::ENoAction);
	PassParameters->TracingVolumetricTexture = SrcTracingVolumetric;
	PassParameters->TracingVolumetricDepthTexture = SrcTracingVolumetricDepth;
	PassParameters->PreviousFrameVolumetricTexture = PrevVolumetric;
	PassParameters->PreviousFrameVolumetricDepthTexture = PrevVolumetricDepth;
	PassParameters->HalfResDepthTexture = SceneDepth;
	PassParameters->LinearTextureSampler = TStaticSamplerState<SF_Bilinear>::GetRHI();
	
	PassParameters->DstVolumetricTextureSizeAndInvSize = FVector4(DstSize.X, DstSize.Y, 1.0f / DstSize.X, 1.0f / DstSize.Y);
	PassParameters->TracingVolumetricTextureValidCoordRect = FUintVector4(0, 0, SrcSize.X - 1, SrcSize.Y - 1);
	PassParameters->TracingVolumetricTextureValidUvRect = FVector4(
		0.51f / SrcSize.X, 
		0.51f / SrcSize.Y, 
		(SrcSize.X - 0.51f) / SrcSize.X, 
		(SrcSize.Y - 0.51f) / SrcSize.Y );
	PassParameters->PreviousFrameVolumetricTextureValidCoordRect = FUintVector4(0, 0, PrevSize.X - 1, PrevSize.Y - 1);
	PassParameters->PreviousFrameVolumetricTextureValidUvRect = FVector4(
		0.51f / PrevSize.X, 
		0.51f / PrevSize.Y, 
		(PrevSize.X - 0.51f) / PrevSize.X, 
		(PrevSize.Y - 0.51f) / PrevSize.Y );

	PassParameters->VolumetricRenderTargetMode = VCRT.Mode;
	PassParameters->DownSampleFactor = VCRT.VolumetricTracingRTDownsampleFactor;
	PassParameters->CurrentTracingPixelOffset = VCRT.CurrentPixelOffset;
	PassParameters->ViewUniformBuffer = View.VolumetricRenderTargetViewUniformBuffer;

	FPixelShaderUtils::AddFullscreenPass<FReconstructVolumetricRenderTargetPS>(
		GraphBuilder, View.ShaderMap, RDG_EVENT_NAME("VolumetricReconstruct"), PixelShader, PassParameters, 
		FIntRect(0, 0, DstSize.X, DstSize.Y));
}

IMPLEMENT_GLOBAL_SHADER(FReconstructVolumetricRenderTargetPS, "/Engine/Private/VolumetricRenderTarget.usf", "ReconstructVolumetricRenderTargetPS", SF_Pixel);


float2 GetPrevScreenUV(float2 CurrUV, float CurrSceneDepth)
{
	float2 CurrScreenPos = float2(2 * CurrUV.x - 1, 1 - 2 * CurrUV.y);
	float4 PrevClip = mul(float4(CurrScreenPos, ConvertToDeviceZ(CurrSceneDepth), 1), View.ClipToPrevClip);
	float2 PrevScreenPos = PrevClip.xy / PrevClip.w;
	return float2(0.5 + 0.5 * PrevScreenPos.x, 0.5 - 0.5 * PrevScreenPos.y);
}

float2 SafeLoadTracingVolumetricDepthTexture(uint2 Coord)
{
	Coord = clamp(Coord, TracingVolumetricTextureValidCoordRect.xy, TracingVolumetricTextureValidCoordRect.zw);
	return TracingVolumetricDepthTexture.Load( uint3(Coord, 0) ).rg;
}

float2 SafeSampleTracingVolumetricDepthTexture(float2 UV)
{
	UV = clamp(UV, TracingVolumetricTextureValidUvRect.xy, TracingVolumetricTextureValidUvRect.zw);
	return TracingVolumetricDepthTexture.SampleLevel( LinearTextureSampler, UV, 0 ).rg;
}

void ReconstructVolumetricRenderTargetPS(
	in float4 SVPos : SV_POSITION,
	out float4 OutputRt0 : SV_Target0,
	out float2 OutputRt1 : SV_Target1)
{
	float2 ScreenUV = SVPos.xy * DstVolumetricTextureSizeAndInvSize.zw; // UV in [0,1]

	int2 PixelPos = int2(SVPos.xy);
	int2 PixelPosDownsample = PixelPos / DownSampleFactor; // DownSampleFactor == 2 or 4
	float TracingDepthKm = SafeLoadTracingVolumetricDepthTexture(PixelPosDownsample).x;
	float2 PrevScreenUV = GetPrevScreenUV(ScreenUV, TracingDepthKm * KmToCm);
	
	const bool bValidPreviousUVs = all(PrevScreenUV > 0.0) && all(PrevScreenUV < 1.0f);
	const bool bUseNewSample = ((PixelPos.x - PixelPosDownsample.x * DownSampleFactor) == CurrentTracingPixelOffset.x) && 
								((PixelPos.y - PixelPosDownsample.y * DownSampleFactor) == CurrentTracingPixelOffset.y);

	float4 RGBA = 0.0f;
	float2 Depths = 0.0f;
	
	if (VolumetricRenderTargetMode == 2)
	{
		if (bUseNewSample || !bValidPreviousUVs)
		{
			RGBA = SafeLoadTracingVolumetricTexture(PixelPosDownsample);
			Depths = SafeLoadTracingVolumetricDepthTexture(PixelPosDownsample);
		}
		else
		{
			RGBA = SafeSamplePreviousFrameVolumetricTexture(PrevScreenUV);
			Depths = SafeSamplePreviousFrameVolumetricDepthTexture(PrevScreenUV);
		}
	}
	else
	{
		if (!bValidPreviousUVs)
		{
			RGBA = SafeSampleTracingVolumetricTexture(ScreenUV);
			Depths = SafeSampleTracingVolumetricDepthTexture(ScreenUV);
		}
		else
		{
			float4 NewRGBA = SafeLoadTracingVolumetricTexture(PixelPosDownsample);
			float2 NewDepths = SafeLoadTracingVolumetricDepthTexture(PixelPosDownsample);

			if (bUseNewSample)// Load the new sample for this pixel we have just traced
			{
				RGBA = NewRGBA;
				Depths = NewDepths;
			}
			else
			{
				float4 HistoryRGBA = SafeSamplePreviousFrameVolumetricTexture(PrevScreenUV);
				float2 HistoryDepths = SafeSamplePreviousFrameVolumetricDepthTexture(PrevScreenUV);
				RGBA = HistoryRGBA;
				Depths = HistoryDepths;

				const float ReconstructDepthZ = HalfResDepthTexture.Load(int3(PixelPos, 0)).r;
				const float3 WorldPosition = SvPositionToWorld(float4(PixelPosDownsample, ReconstructDepthZ, 1.0));
				const float PixelDistanceFromViewKm = length(WorldPosition - View.WorldCameraOrigin) * CmToKm;

				if (abs(PixelDistanceFromViewKm - HistoryDepths.y) > PixelDistanceFromViewKm * 0.1f)
				{
					// History has a too large depth difference at depth discontinuities, use the data with closest depth within the neightborhood
					const int2 NeightboorsOffset[8] = { int2(1,0), int2(1,1), int2(0,1), int2(-1,1), int2(-1,0), int2(-1,-1), int2(0,-1), int2(1,-1)};
					float ClosestDepth = 99999999.0f;
					for (int i = 0; i < 8; ++i)
					{
						float2 NeighboorsDepths = SafeLoadTracingVolumetricDepthTexture(PixelPosDownsample + NeightboorsOffset[i]);
						const float NeighboorsClosestDepth = abs(PixelDistanceFromViewKm - NeighboorsDepths.y);
						if (NeighboorsClosestDepth < ClosestDepth)
						{
							ClosestDepth = NeighboorsClosestDepth;
							float4 NeighboorsRGBA = SafeLoadTracingVolumetricTexture(PixelPosDownsample + NeightboorsOffset[i]);
							RGBA   = NeighboorsRGBA;
							Depths = NeighboorsDepths;
						} 
					}
					if (abs(PixelDistanceFromViewKm - NewDepths.y) < ClosestDepth)
					{
						RGBA = NewRGBA;
						Depths = NewDepths;
					}
				}
			}
		}
	}
	
	OutputRt0 = RGBA;
	OutputRt1 = Depths;
}


