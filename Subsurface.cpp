
void ComputeSubsurfaceShim(FRHICommandListImmediate& RHICmdList, const TArray<FViewInfo>& Views)
{
	FSceneRenderTargets& SceneRenderTargets = FSceneRenderTargets::Get(RHICmdList);

	FRDGBuilder GraphBuilder(RHICmdList);

	FRDGTexture* SceneTexture = GraphBuilder.RegisterExternalTexture(SceneRenderTargets.GetSceneColor(), TEXT("SceneColor"));

	FRDGTexture* SceneTextureOutput = ComputeSubsurface(GraphBuilder, SceneTexture, Views);

	TRefCountPtr<IPooledRenderTarget> SceneTarget;
	GraphBuilder.QueueTextureExtraction(SceneTextureOutput, &SceneTarget);
	GraphBuilder.Execute();

	SceneRenderTargets.SetSceneColor(SceneTarget);
}

struct FScreenPassTexture {
	FRDGTexture* Texture = nullptr;
	FIntRect ViewRect;
};

class FScreenPassTextureViewport {
	FIntPoint Extent = FIntPoint::ZeroValue;// The texture extent, in pixels; defines a super-set [0, 0]x(Extent, Extent).
	FIntRect Rect;// The viewport rect, in pixels; defines a sub-set within [0, 0]x(Extent, Extent).
	FScreenPassTextureViewport(FRDGTexture* InTexture, FIntRect InRect)
		: Extent(InTexture->Desc.Extent), Rect(InRect) {}
};


FRDGTexture* ComputeSubsurface(FRDGBuilder& GraphBuilder, FRDGTexture* SceneTexture, const TArray<FViewInfo>& Views)
{
	FRDGTextureDesc SceneColorDesc = SceneTexture->Desc;
	SceneColorDesc.TargetableFlags |= TexCreate_RenderTargetable;
	FRDGTexture* SceneTextureOutput = GraphBuilder.CreateTexture(SceneColorDesc, TEXT("SceneColorSubsurface"));

	for (uint32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
	{
		if (bIsSubsurfaceView)
		{
			const FViewInfo& View = Views[ViewIndex];
			const FScreenPassTextureViewport SceneViewport(SceneTexture, View.ViewRect);

			ComputeSubsurfaceForView(GraphBuilder, View, SceneViewport, SceneTexture, SceneTextureOutput, );
		}
	}

	return SceneTextureOutput;
}

BEGIN_SHADER_PARAMETER_STRUCT(FSubsurfaceParameters, )
	SHADER_PARAMETER(FVector4, SubsurfaceParams)
	SHADER_PARAMETER_STRUCT_REF(FSceneTexturesUniformParameters, SceneUniformBuffer)
	SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
	SHADER_PARAMETER_SAMPLER(SamplerState, BilinearTextureSampler)
	SHADER_PARAMETER_TEXTURE(Texture2D, SSProfilesTexture)
END_SHADER_PARAMETER_STRUCT()


const uint32 kSubsurfaceGroupSize = 8;

void ComputeSubsurfaceForView(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	const FScreenPassTextureViewport& SceneViewport,
	FRDGTexture* SceneTexture,
	FRDGTexture* SceneTextureOutput,
	ERenderTargetLoadAction SceneTextureLoadAction)
{
	check(SceneTexture);
	check(SceneTextureOutput);
	check(SceneViewport.Extent == SceneTexture->Desc.Extent);

	const FSceneViewFamily* ViewFamily = View.Family;
	const FRDGTextureDesc& SceneTextureDesc = SceneTexture->Desc;
	const ESubsurfaceMode SubsurfaceMode = GetSubsurfaceModeForView(View);
	const bool bHalfRes = (SubsurfaceMode == ESubsurfaceMode::HalfRes);
	const bool bCheckerboard = IsSubsurfaceCheckerboardFormat(SceneTextureDesc.Format);
	const uint32 ScaleFactor = bHalfRes ? 2 : 1;
	const bool bForceRunningInSeparable = CVarSSSBurleyQuality.GetValueOnRenderThread() == 0|| bHalfRes || View.GetShaderPlatform() == SP_OPENGL_SM5;

	const FScreenPassTextureViewport SubsurfaceViewport = FScreenPassTextureViewport::CreateDownscaled(SceneViewport, ScaleFactor);
	const FIntPoint TileDimension = FIntPoint::DivideAndRoundUp(SubsurfaceViewport.Extent, kSubsurfaceGroupSize);
	const int32 MaxGroupCount = TileDimension.X*TileDimension.Y;

	const FRDGTextureDesc SceneTextureDescriptor = FRDGTextureDesc::Create2DDesc(
		SceneViewport.Extent,
		PF_FloatRGBA,
		FClearValueBinding(),
		TexCreate_None,
		TexCreate_RenderTargetable | TexCreate_ShaderResource | TexCreate_UAV,
		false);

	const FRDGTextureDesc SubsurfaceTextureDescriptor = FRDGTextureDesc::Create2DDesc(
		SubsurfaceViewport.Extent,
		PF_FloatRGBA,
		FClearValueBinding(),
		TexCreate_None,
		TexCreate_RenderTargetable | TexCreate_ShaderResource | TexCreate_UAV,
		false);

	const FRDGTextureDesc SubsurfaceTextureWith6MipsDescriptor = FRDGTextureDesc::Create2DDesc(
		SubsurfaceViewport.Extent,
		PF_FloatRGBA,
		FClearValueBinding(),
		TexCreate_None,
		TexCreate_RenderTargetable | TexCreate_ShaderResource | TexCreate_UAV,
		false,
		6);

	const FSubsurfaceParameters SubsurfaceCommonParameters = GetSubsurfaceCommonParameters(GraphBuilder.RHICmdList, View);
	const FScreenPassTextureViewportParameters SubsurfaceViewportParameters = GetScreenPassTextureViewportParameters(SubsurfaceViewport);
	const FScreenPassTextureViewportParameters SceneViewportParameters = GetScreenPassTextureViewportParameters(SceneViewport);

	FRDGTextureRef SetupTexture = SceneTexture;
	FRDGTextureRef SubsurfaceSubpassOneTex = nullptr;
	FRDGTextureRef SubsurfaceSubpassTwoTex = nullptr;

	FRHISamplerState* PointClampSampler = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	FRHISamplerState* BilinearBorderSampler = TStaticSamplerState<SF_Bilinear, AM_Border, AM_Border, AM_Border>::GetRHI();

	//History texture
	FSceneViewState* ViewState = (FSceneViewState*)View.State;
	TRefCountPtr<IPooledRenderTarget>* QualityHistoryState = ViewState ? &ViewState->SubsurfaceScatteringQualityHistoryRT : NULL;

	//allocate/reallocate the quality history texture. 
	FRDGTextureRef QualityHistoryTexture = RegisterExternalRenderTarget(GraphBuilder, QualityHistoryState, SceneTextureDescriptor.Extent, TEXT("QualityHistoryTexture"));
	FRDGTextureRef NewQualityHistoryTexture = nullptr;

	/**
	 * When in bypass mode, the setup and convolution passes are skipped, but lighting
	 * reconstruction is still performed in the recombine pass.
	 */
	if (SubsurfaceMode != ESubsurfaceMode::Bypass)
	{
		// Support mipmaps in full resolution only.
		SetupTexture = GraphBuilder.CreateTexture(bForceRunningInSeparable ? SubsurfaceTextureDescriptor:SubsurfaceTextureWith6MipsDescriptor, TEXT("SubsurfaceSetupTexture"));

		FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(GraphBuilder.RHICmdList);
		FRDGTextureRef VelocityTexture = RegisterExternalRenderTarget(GraphBuilder, &(SceneContext.SceneVelocity), SubsurfaceTextureDescriptor.Extent, TEXT("Velocity")); 
		FSubsurfaceUniformRef UniformBuffer = CreateUniformBuffer(View, MaxGroupCount);
		
		// Pre-allocate black UAV together.
		{
			SubsurfaceSubpassOneTex = CreateBlackUAVTexture(GraphBuilder, SubsurfaceTextureWith6MipsDescriptor, TEXT("SubsurfaceSubpassOneTex"),
				View, SubsurfaceViewport);
			SubsurfaceSubpassTwoTex = CreateBlackUAVTexture(GraphBuilder, SubsurfaceTextureWith6MipsDescriptor, TEXT("SubsurfaceSubpassTwoTex"),
				View, SubsurfaceViewport);
			// Only clear when we are in full resolution.
			if (!bForceRunningInSeparable)
			{
				NewQualityHistoryTexture = CreateBlackUAVTexture(GraphBuilder, SubsurfaceTextureDescriptor, TEXT("SubsurfaceQualityHistoryState"),
					View, SubsurfaceViewport);
			}
		}

		// Initialize the group buffer
		FRDGBufferRef SeparableGroupBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), 2*(MaxGroupCount + 1)), TEXT("SeparableGroupBuffer"));;
		FRDGBufferRef BurleyGroupBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), 2*(MaxGroupCount + 1)), TEXT("BurleyGroupBuffer"));;
		FRDGBufferRef SeparableIndirectDispatchArgsBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc(4), TEXT("SeprableIndirectDispatchArgs"));
		FRDGBufferRef BurleyIndirectDispatchArgsBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc(4), TEXT("BurleyIndirectDispatchArgs"));

		// Initialize the group counters
		{
			typedef FSubsurfaceInitValueBufferCS SHADER;
			TShaderMapRef<SHADER> ComputeShader(View.ShaderMap);
			SHADER::FParameters* PassParameters = GraphBuilder.AllocParameters<SHADER::FParameters>();
			PassParameters->RWBurleyGroupBuffer = GraphBuilder.CreateUAV(BurleyGroupBuffer, EPixelFormat::PF_R32_UINT);
			PassParameters->RWSeparableGroupBuffer = GraphBuilder.CreateUAV(SeparableGroupBuffer, EPixelFormat::PF_R32_UINT);
			FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("InitGroupCounter"), ComputeShader, PassParameters, FIntVector(1, 1, 1));
		}

		// Call the indirect setup
		{
			FRDGTextureSRVDesc SceneTextureSRVDesc = FRDGTextureSRVDesc::Create(SceneTexture);
			FRDGTextureUAVDesc SetupTextureOutDesc(SetupTexture, 0);

			typedef FSubsurfaceIndirectDispatchSetupCS SHADER;
			SHADER::FParameters* PassParameters = GraphBuilder.AllocParameters<SHADER::FParameters>();
			PassParameters->Subsurface = SubsurfaceCommonParameters;
			PassParameters->Output = SubsurfaceViewportParameters;
			PassParameters->SubsurfaceInput0 = GetSubsurfaceInput(SceneTexture, SceneViewportParameters);
			PassParameters->SubsurfaceSampler0 = PointClampSampler;
			PassParameters->SetupTexture = GraphBuilder.CreateUAV(SetupTextureOutDesc);
			PassParameters->RWBurleyGroupBuffer = GraphBuilder.CreateUAV(BurleyGroupBuffer, EPixelFormat::PF_R32_UINT);
			PassParameters->RWSeparableGroupBuffer = GraphBuilder.CreateUAV(SeparableGroupBuffer, EPixelFormat::PF_R32_UINT);
			PassParameters->SubsurfaceUniformParameters = UniformBuffer;

			SHADER::FPermutationDomain ComputeShaderPermutationVector;
			ComputeShaderPermutationVector.Set<SHADER::FDimensionHalfRes>(bHalfRes);
			ComputeShaderPermutationVector.Set<SHADER::FDimensionCheckerboard>(bCheckerboard);
			ComputeShaderPermutationVector.Set<SHADER::FRunningInSeparable>(bForceRunningInSeparable);
			TShaderMapRef<SHADER> ComputeShader(View.ShaderMap, ComputeShaderPermutationVector);

			FIntPoint ComputeGroupCount = FIntPoint::DivideAndRoundUp(SubsurfaceViewport.Extent, kSubsurfaceGroupSize);

			FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("SubsurfaceSetup"), ComputeShader, PassParameters, FIntVector(ComputeGroupCount.X, ComputeGroupCount.Y, 1));
		}

		// In half resolution, only Separable is used. We do not need this mipmap.
		if(!bForceRunningInSeparable)
		{
			// Generate mipmap for the diffuse scene color and depth, use bilinear filter
			FGenerateMips::Execute(&GraphBuilder, SetupTexture, BilinearBorderSampler);
		}

		typedef FSubsurfaceIndirectDispatchCS SHADER;

		FRHISamplerState* SubsurfaceSamplerState = SHADER::GetSamplerState(bHalfRes);
		const SHADER::EQuality SubsurfaceQuality = SHADER::GetQuality();

		// Store the buffer
		const FRDGBufferRef SubsurfaceBufferUsage[] = { BurleyGroupBuffer,                      SeparableGroupBuffer };
		const FRDGBufferRef  SubsurfaceBufferArgs[] = { BurleyIndirectDispatchArgsBuffer,       SeparableIndirectDispatchArgsBuffer };
		const TCHAR*		  SubsurfacePhaseName[] = { TEXT("BuildBurleyIndirectDispatchArgs"),TEXT("BuildSeparableIndirectDispatchArgs") };

		// Setup the indirect arguments.
		{
			const int NumOfSubsurfaceType = 2;

			for (int SubsurfaceTypeIndex = 0; SubsurfaceTypeIndex < NumOfSubsurfaceType; ++SubsurfaceTypeIndex)
			{
				typedef FSubsurfaceBuildIndirectDispatchArgsCS ARGSETUPSHADER;
				ARGSETUPSHADER::FParameters* PassParameters = GraphBuilder.AllocParameters<ARGSETUPSHADER::FParameters>();
				PassParameters->SubsurfaceUniformParameters = UniformBuffer;
				PassParameters->RWIndirectDispatchArgsBuffer = GraphBuilder.CreateUAV(SubsurfaceBufferArgs[SubsurfaceTypeIndex], EPixelFormat::PF_R32_UINT);
				PassParameters->GroupBuffer = GraphBuilder.CreateSRV(SubsurfaceBufferUsage[SubsurfaceTypeIndex], EPixelFormat::PF_R32_UINT);

				TShaderMapRef<ARGSETUPSHADER> ComputeShader(View.ShaderMap);
				FComputeShaderUtils::AddPass(GraphBuilder, FRDGEventName(SubsurfacePhaseName[SubsurfaceTypeIndex]), ComputeShader, PassParameters, FIntVector(1, 1, 1));
			}
		}

		// Major pass to combine Burley and Separable
		{
			struct FSubsurfacePassInfo
			{
				FSubsurfacePassInfo(const TCHAR* InName, FRDGTextureRef InInput, FRDGTextureRef InOutput,
					SHADER::ESubsurfaceType InSurfaceType, SHADER::ESubsurfacePass InSurfacePass)
					: Name(InName), Input(InInput), Output(InOutput), SurfaceType(InSurfaceType), SubsurfacePass(InSurfacePass)
				{}

				const TCHAR* Name;
				FRDGTextureRef Input;
				FRDGTextureRef Output;
				SHADER::ESubsurfaceType SurfaceType;
				SHADER::ESubsurfacePass SubsurfacePass;
			};

			const int NumOfSubsurfacePass = 4;

			const FSubsurfacePassInfo SubsurfacePassInfos[NumOfSubsurfacePass] =
			{
				{	TEXT("SubsurfacePassOne_Burley"),				SetupTexture, SubsurfaceSubpassOneTex, SHADER::ESubsurfaceType::BURLEY	 , SHADER::ESubsurfacePass::PassOne}, //Burley main pass
				{	TEXT("SubsurfacePassTwo_SepHon"),				SetupTexture, SubsurfaceSubpassOneTex, SHADER::ESubsurfaceType::SEPARABLE, SHADER::ESubsurfacePass::PassOne}, //Separable horizontal
				{ TEXT("SubsurfacePassThree_SepVer"),	 SubsurfaceSubpassOneTex, SubsurfaceSubpassTwoTex, SHADER::ESubsurfaceType::SEPARABLE, SHADER::ESubsurfacePass::PassTwo}, //Separable Vertical
				{	 TEXT("SubsurfacePassFour_BVar"),    SubsurfaceSubpassOneTex, SubsurfaceSubpassTwoTex, SHADER::ESubsurfaceType::BURLEY	 , SHADER::ESubsurfacePass::PassTwo}  //Burley Variance
			};

			//Dispatch the two phase for both SSS
			for (int PassIndex = 0; PassIndex < NumOfSubsurfacePass; ++PassIndex)
			{
				const FSubsurfacePassInfo& PassInfo = SubsurfacePassInfos[PassIndex];

				const SHADER::ESubsurfaceType SubsurfaceType = PassInfo.SurfaceType;
				const auto SubsurfacePassFunction = PassInfo.SubsurfacePass;
				const int SubsurfaceTypeIndex = static_cast<int>(SubsurfaceType);
				FRDGTextureRef TextureInput = PassInfo.Input;
				FRDGTextureRef TextureOutput = PassInfo.Output;

				FRDGTextureUAVDesc SSSColorUAVDesc(TextureOutput, 0);
				FRDGTextureSRVDesc InputSRVDesc = FRDGTextureSRVDesc::Create(TextureInput);

				SHADER::FParameters* PassParameters = GraphBuilder.AllocParameters<SHADER::FParameters>();
				PassParameters->Subsurface = SubsurfaceCommonParameters;
				PassParameters->Output = SubsurfaceViewportParameters;
				PassParameters->SSSColorUAV = GraphBuilder.CreateUAV(SSSColorUAVDesc);
				PassParameters->SubsurfaceInput0 = GetSubsurfaceInput(TextureInput, SubsurfaceViewportParameters);
				PassParameters->SubsurfaceSampler0 = SubsurfaceSamplerState;
				PassParameters->GroupBuffer = GraphBuilder.CreateSRV(SubsurfaceBufferUsage[SubsurfaceTypeIndex], EPixelFormat::PF_R32_UINT);
				PassParameters->IndirectDispatchArgsBuffer = SubsurfaceBufferArgs[SubsurfaceTypeIndex];

				if (SubsurfacePassFunction == SHADER::ESubsurfacePass::PassOne && SubsurfaceType == SHADER::ESubsurfaceType::BURLEY)
				{
					PassParameters->SubsurfaceInput1 = GetSubsurfaceInput(QualityHistoryTexture, SubsurfaceViewportParameters);
					PassParameters->SubsurfaceSampler1 = PointClampSampler;
				}

				if (SubsurfacePassFunction == SHADER::ESubsurfacePass::PassTwo && SubsurfaceType == SHADER::ESubsurfaceType::BURLEY)
				{
					// we do not write to history in separable mode.
					if (!bForceRunningInSeparable)
					{
						PassParameters->HistoryUAV = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(NewQualityHistoryTexture, 0));
					}

					PassParameters->SubsurfaceInput1 = GetSubsurfaceInput(QualityHistoryTexture, SubsurfaceViewportParameters);
					PassParameters->SubsurfaceSampler1 = PointClampSampler;
					PassParameters->SubsurfaceInput2 = GetSubsurfaceInput(VelocityTexture, SubsurfaceViewportParameters);
					PassParameters->SubsurfaceSampler2 = PointClampSampler;
				}

				SHADER::FPermutationDomain ComputeShaderPermutationVector;
				ComputeShaderPermutationVector.Set<SHADER::FSubsurfacePassFunction>(SubsurfacePassFunction);
				ComputeShaderPermutationVector.Set<SHADER::FDimensionQuality>(SHADER::GetQuality());
				ComputeShaderPermutationVector.Set<SHADER::FSubsurfaceSamplerType>(SHADER::GetSamplerType());
				ComputeShaderPermutationVector.Set<SHADER::FSubsurfaceType>(SubsurfaceType);
				ComputeShaderPermutationVector.Set<SHADER::FDimensionHalfRes>(bHalfRes);
				ComputeShaderPermutationVector.Set<SHADER::FRunningInSeparable>(bForceRunningInSeparable);
				TShaderMapRef<SHADER> ComputeShader(View.ShaderMap, ComputeShaderPermutationVector);

				FComputeShaderUtils::AddPass(GraphBuilder, FRDGEventName(PassInfo.Name), ComputeShader, PassParameters, SubsurfaceBufferArgs[SubsurfaceTypeIndex], 0);
			}
		}
	}

	// Recombines scattering result with scene color.
	{
		FSubsurfaceRecombinePS::FParameters* PassParameters = GraphBuilder.AllocParameters<FSubsurfaceRecombinePS::FParameters>();
		PassParameters->Subsurface = SubsurfaceCommonParameters;
		PassParameters->RenderTargets[0] = FRenderTargetBinding(SceneTextureOutput, SceneTextureLoadAction);
		PassParameters->SubsurfaceInput0 = GetSubsurfaceInput(SceneTexture, SceneViewportParameters);
		PassParameters->SubsurfaceSampler0 = BilinearBorderSampler;

		// Scattering output target is only used when scattering is enabled.
		if (SubsurfaceMode != ESubsurfaceMode::Bypass)
		{
			PassParameters->SubsurfaceInput1 = GetSubsurfaceInput(SubsurfaceSubpassTwoTex, SubsurfaceViewportParameters);
			PassParameters->SubsurfaceSampler1 = BilinearBorderSampler;
		}

		const FSubsurfaceRecombinePS::EQuality RecombineQuality = FSubsurfaceRecombinePS::GetQuality(View);

		FSubsurfaceRecombinePS::FPermutationDomain PixelShaderPermutationVector;
		PixelShaderPermutationVector.Set<FSubsurfaceRecombinePS::FDimensionMode>(SubsurfaceMode);
		PixelShaderPermutationVector.Set<FSubsurfaceRecombinePS::FDimensionQuality>(RecombineQuality);
		PixelShaderPermutationVector.Set<FSubsurfaceRecombinePS::FDimensionCheckerboard>(bCheckerboard);
		PixelShaderPermutationVector.Set<FSubsurfaceRecombinePS::FDimensionHalfRes>(bHalfRes);
		PixelShaderPermutationVector.Set<FSubsurfaceRecombinePS::FRunningInSeparable>(bForceRunningInSeparable);

		TShaderMapRef<FSubsurfaceRecombinePS> PixelShader(View.ShaderMap, PixelShaderPermutationVector);

		/**
		 * See the related comment above in the prepare pass. The scene viewport is used as both the target and
		 * texture viewport in order to ensure that the correct pixel is sampled for checkerboard rendering.
		 */
		AddDrawScreenPass(
			GraphBuilder,
			RDG_EVENT_NAME("SubsurfaceRecombine"),
			View,
			SceneViewport,
			SceneViewport,
			PixelShader,
			PassParameters,
			EScreenPassDrawFlags::AllowHMDHiddenAreaMask);
	}

	if (SubsurfaceMode != ESubsurfaceMode::Bypass && QualityHistoryState && !bForceRunningInSeparable)
	{
		GraphBuilder.QueueTextureExtraction(NewQualityHistoryTexture, QualityHistoryState, true);
	}
}