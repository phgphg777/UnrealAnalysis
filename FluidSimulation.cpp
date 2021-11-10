
void Grid3D_SetResolution(
in ENiagaraGrid2DResolutopn ResolutionMethod,
inOut Grid3DCollection Grid,
in Grid3DCollection OtherGrid,
in Vector WorldGridExtents,
in int NumCellsX,
in int NumCellsY,
in int NumCellsZ,
in int NumCellsMaxAxis,
in float ResolutionMult,
in float WorldCellSize,
Out Vector Emitter_Module_WorldGridExtents,
Out Vector Emitter_Module_WorldCellSize)
{
	int L_NumCellsX;
	int L_NumCellsY;
	int L_NumCellsZ;
	Vector L_WorldGridExtents;

	if(ResolutionMethod == ENiagaraGrid2DResolutopn::Independent)
	{
		L_NumCellsX = NumCellsX;
		L_NumCellsY = NumCellsY;
		L_NumCellsZ = NumCellsZ;
		L_WorldGridExtents = WorldGridExtents;
	}
	else if(ResolutionMethod == ENiagaraGrid2DResolutopn::OtherGrid)
	{
		L_NumCellsX = OtherGrid.NumCellsX;
		L_NumCellsY = OtherGrid.NumCellsY;
		L_NumCellsZ = OtherGrid.NumCellsZ;
		L_WorldGridExtents = WorldGridExtents;
	}
	else if(ResolutionMethod == ENiagaraGrid2DResolutopn::MaxAxis)
	{
		float CellSize = max(WorldGridExtents.x, WorldGridExtents.y, WorldGridExtents.z) / NumCellsMaxAxis;
		L_NumCellsX = floor(WorldGridExtents.x / CellSize);
		L_NumCellsY = floor(WorldGridExtents.y / CellSize);
		L_NumCellsZ = floor(WorldGridExtents.z / CellSize);
		L_WorldGridExtents = float3(L_NumCellsX, L_NumCellsY, L_NumCellsZ) * CellSize;
	}
	else if(ResolutionMethod == ENiagaraGrid2DResolutopn::WorldCellSize)
	{
		const float CellSize = WorldCellSize;
		L_NumCellsX = floor(WorldGridExtents.x / CellSize);
		L_NumCellsY = floor(WorldGridExtents.y / CellSize);
		L_NumCellsZ = floor(WorldGridExtents.z / CellSize);
		L_WorldGridExtents = float3(L_NumCellsX, L_NumCellsY, L_NumCellsZ) * CellSize;
	}

	Grid.NumCellsX = L_NumCellsX * ResolutionMult;
	Grid.NumCellsY = L_NumCellsY * ResolutionMult;
	Grid.NumCellsZ = L_NumCellsZ * ResolutionMult;
	Emitter_Module_WorldGridExtents = L_WorldGridExtents;
	Emitter_Module_WorldCellSize = L_WorldGridExtents / float3(Grid.NumCellsX, Grid.NumCellsY, Grid.NumCellsZ);
}


int SimGrid_VelocityIndex = 0;
int SimGrid_DivergenceIndex = 0;
int SimGrid_CurlIndex = 1;
int SimGrid_BoundaryIndex = 2;
int SimGrid_SolidVelocityIndex = 3;
int SimGrid_PressureIndex = 0;
int SimGrid_DensityIndex = 0;
int SimGrid_TemperatureIndex = 1;

void SetGridValues(
in bool bScalars,
in bool bSimGrid,
in bool bPressure,
in bool bTransientValues,
in Grid3DCollection ScalarGrid,
in Grid3DCollection Grid = Emitter.SimGrid,
in Grid3DCollection PressureGrid = Emitter.PressureGrid,
in Grid3DCollection TransientGrid = Emitter.TransientGrid)
{
	int IndexX;
	int IndexY;
	int IndexZ;

	if(bSimGrid)
	{
		Vector3 Velocity = Transient.Velocity;
		ExcutionIndexToGridIndex(Grid, IndexX, IndexY, IndexZ);
		Grid.SetGridValue(IndexX, IndexY, IndexZ, SimGrid_VelocityIndex  , Velocity.x, 0);
		Grid.SetGridValue(IndexX, IndexY, IndexZ, SimGrid_VelocityIndex+1, Velocity.y, 0);
		Grid.SetGridValue(IndexX, IndexY, IndexZ, SimGrid_VelocityIndex+2, Velocity.z, 0);
	}

	if(bTransientValues)
	{
		float Divergence = Transient.Divergence;
		Vector Curl = Transient.Curl;
		float Boundary = Transient.Boundary;
		Vector SolidVelocity = Transient.SolidVelocity;
		ExcutionIndexToGridIndex(TransientGrid, IndexX, IndexY, IndexZ);
		TransientGrid.SetGridValue(IndexX, IndexY, IndexZ, SimGrid_DivergenceIndex, Divergence, 0); // 0
		TransientGrid.SetGridValue(IndexX, IndexY, IndexZ, SimGrid_CurlIndex  , Curl.x, 0);	// 1
		TransientGrid.SetGridValue(IndexX, IndexY, IndexZ, SimGrid_CurlIndex+1, Curl.y, 0); // 2
		TransientGrid.SetGridValue(IndexX, IndexY, IndexZ, SimGrid_CurlIndex+2, Curl.z, 0); // 3
		TransientGrid.SetGridValue(IndexX, IndexY, IndexZ, SimGrid_BoundaryIndex, Boundary, 0); // 2
		TransientGrid.SetGridValue(IndexX, IndexY, IndexZ, SimGrid_SolidVelocityIndex  , SolidVelocity.x, 0); // 3
		TransientGrid.SetGridValue(IndexX, IndexY, IndexZ, SimGrid_SolidVelocityIndex+1, SolidVelocity.y, 0); // 4
		TransientGrid.SetGridValue(IndexX, IndexY, IndexZ, SimGrid_SolidVelocityIndex+2, SolidVelocity.z, 0); // 5
	}

	if(bPressure)
	{
		float Pressure = Transient.Pressure;
		ExcutionIndexToGridIndex(PressureGrid, IndexX, IndexY, IndexZ);
		PressureGrid.SetGridValue(IndexX, IndexY, IndexZ, SimGrid_PressureIndex, Pressure, 0);
	}

	if(bScalars)
	{
		float Density = Transient.Density;
		float Temperature = Transient.Temperature;
		ExcutionIndexToGridIndex(ScalarGrid, IndexX, IndexY, IndexZ);
		ScalarGrid.SetGridValue(IndexX, IndexY, IndexZ, SimGrid_DensityIndex, Density, 0);
		ScalarGrid.SetGridValue(IndexX, IndexY, IndexZ, SimGrid_TemperatureIndex, Temperature, 0);
	}
}






{
	// INIT
	{
		ClearGrid(Emitter.SimGrid, SimGrid_VelocityIndex, float3(0));
		ClearGrid(Emitter.ScalarGrid, SimGrid_DensityIndex, 0);
	}
}






/////////////////////////////////////////////////////////////////////////////////////////////////////////
/*
1. Using MAC grid ?
2. Numerical scheme for advection
3. Conjugate gradient method
4. Vorticity Confinement
5. Handles Boundary conditions
*/

/*
IxJxK <- Volume Resolution
XxY   <- XY Frames
=> assert(X*Y == K && I*X == RTSize.X && J*Y == RTSize.Y)
For example, Volume Resolution is 256^3, RTSize is 4096x4096 and XYFrames is 16x16
*/


float3 EncodeVolumeCoordinates(float2 InUV, float2 TileDim)
{
	float2 IJ = InUV * TileDim;	// 0 <= IJ <= TileDim
	uint TileIndex = TileDim.x * Floor(IJ).y + Floor(IJ).x;
	return float3(Frac(IJ), (TileIndex + 0.5) / (TileDim.x * TileDim.y));
}

float4 VolumeTextureFunction(Texture2D Texture, float3 InPQR, float2 NumFrames)
{
	float2 UVOffsetInTile = Frac(InPQR.rg) / NumFrames;

	float TileIndex = (InPQR.b * NumFrames.x * NumFrames.y) - 0.5 + 0.000001;
	float Z0 = Floor(TileIndex);
	float Z1 = Z0 + 1;
	float alpha = TileIndex - Z0;

	float2 Z0Tile2DIndex = float2( FMod( Z0, NumFrames.x ), Floor( Z0 / NumFrames.x ) );
	float2 Z1Tile2DIndex = float2( FMod( Z1, NumFrames.x ), Floor( Z1 / NumFrames.x ) );
	
	float2 UV1Base = Z0Tile2DIndex / NumFrames;
	float2 UV2Base = Z1Tile2DIndex / NumFrames;

	float4 Sample1 = TextureSample(Texture, UV1Base + UVOffsetInTile);
	float4 Sample2 = TextureSample(Texture, UV2Base + UVOffsetInTile);
	return lerp(Sample1, Sample2, alpha);
}


Texture2D<float3> Color;	// Color Texture
Texture2D<float3> Source;	// Velocity Texture, Color Texture
Texture2D<float3> Velocity;	// Velocity Texture
bool IsVelocity;
float2 XYFrames;
float TimeStep;				// Simulation::Time_Step
float3 FrameResolution;		// 256x256x256

float Temp_Buoyancy;		// Simulation::Temperature_Buoyancy
float Color_Erosion;		// Simulation::Density_Erosion
float3 P;
float SourceRadius;			// Seed_Color_Setup::Source_Radius
float Emission_Strength;	// Simulation::Emission_Multiplier * Simulation::Emission_Curve(t)
float Emission_Temperature;	// Simulation::Emission_Temperature_Multiplier

float Velocity_Dampening;	// 0.99
float Color_Dampening;		// 1 - Simulation::Density_Dampening
float Temperature_Dampening;// 1 - Simulation::Temperature_Dampening

void M_Advect_3D(
	in float2 UV,
	out float3 OutColor : SV_Target0, 
	out float OutOpacity : SV_Target1 )
{
	float3 UVW = EncodeVolumeCoordinates(UV, XYFrames);

	float3 SampleVelocity = TextureSample(Velocity, UV);
	float3 BackTracedPos = UVW - SampleVelocity * (TimeStep / FrameResolution);
	float4 BackTracedSource = VolumeTextureFunction(Source, BackTracedPos, XYFrames);

	float4 SampleSource;
	
	if(IsVelocity)
	{
		float Buoyancy = TextureSample(Color, UV).a * Temp_Buoyancy;
		SampleSource.rgb = (BackTracedSource.rgb + float3(0,0,Buoyancy)) * Velocity_Dampening;
	}
	else
	{
		float Emission = distance(UVW, P) < SourceRadius ? Emission_Strength : 0.0;
		BackTracedSource.rgb = max(0, (BackTracedSource.rgb - Color_Erosion) / (1 - Color_Erosion));
		SampleSource.rgb = (BackTracedSource.rgb + float3(Emission)) * Color_Dampening;
		SampleSource.a = (BackTracedSource.a + Emission * Emission_Temperature * 0.01) * Temperature_Dampening;
	}

	if(!IsVelocity)
	{
		SampleSource *= SoftColorMask();
	}

	OutColor = SampleSource.rgb;
	OutOpacity = SampleSource.a;
}


float2 XYFrames;
float3 P;
float SourceRadius; 			// Seed_Color_Setup::Source_Radius
float Hardness;					// Seed_Color_Setup::Edge_Hardness
float Noise_Strength;			// Seed_Color_Setup::Noise_Strength
float Density_Multiplier;		// Seed_Color_Setup::Density_Multiplier
float Temperature_Multiplier;	// Seed_Color_Setup::Seed_Temperature_Multiplier
float Heat_Inset;				// Seed_Color_Setup::Temperature_Inset
float Color_Voronoi_Scale;
float Offset;


void M_Seed_Color_3D(
	in float2 UV,
	out float3 OutColor : SV_Target0, 
	out float OutOpacity : SV_Target1 )
{

}

Texture2D<float3> Color;	// Color Texture
float2 XYFrames;
float3 P;
float Force; 				// Seed_Velocity_Setup::Overall_Velocity_Multiplier
float Radial_Force;			// Seed_Velocity_Setup::Radial_Force
float3 Offset;				// Seed_Velocity_Setup::Noise_Field_Pos_Offset
float Curl_1_Tiling;		// Seed_Velocity_Setup::Curl_1_Tiling
float Curl_2_Tiling;		// Seed_Velocity_Setup::Curl_2_Tiling
float Curl_1_Strength;		// Seed_Velocity_Setup::Curl_1_Strength
float Curl_2_Strength;		// Seed_Velocity_Setup::Curl_2_Strength
float SeedTex;


void M_Seed_Velocity_3D(
	in float2 UV,
	out float3 OutColor : SV_Target0, 
	out float OutOpacity : SV_Target1 )
{

}






float VolumeExtent = 256;
float2 XYFrames;
Texture2D Color;

float MAxSteps = 128.0;
float PlaneAlignment = 1.0;
float Density = 16.0;
float ShadowSteps = 16.0;
float ShadowDensity = 4.0;
float ShadowThreshold = 0.01;


void RayMarchCubeSetup(float Steps, float PlaneAlignment, float scenedepth)
{
	float3 camerafwd = mul(float3(0,0,1), (float3x3) ResolvedView.ViewToTranslatedWorld);
	float MaxRayEnd = scenedepth / abs(dot(camerafwd, Parameters.CameraVector)) / VolumeExtent;
	
	//bring vectors into local space to support object transforms
	float3 localcamvec = -normalize( mul(Parameters.CameraVector, (float3x3)GetPrimitiveData(Parameters.PrimitiveId).WorldToLocal) );
	float3 localcampos = mul(float4(ResolvedView.WorldCameraOrigin,1.0), GetPrimitiveData(Parameters.PrimitiveId).WorldToLocal).xyz;
	localcampos = localcampos / VolumeExtent + 0.5;

	float3 firstintersections = (0 - localcampos) / localcamvec;
	float3 secondintersections = (1 - localcampos) / localcamvec;
	float3 closest = min(firstintersections, secondintersections);
	float3 furthest = max(firstintersections, secondintersections);

	float t0 = max(closest.x, max(closest.y, closest.z));
	float t1 = min(furthest.x, min(furthest.y, furthest.z));
	//float planeoffset = 1 - frac( (t0 - length(localcampos - 0.5)) * Steps );
	//t0 += (planeoffset / Steps) * PlaneAlignment;
	t0 = max(0, t0);
	t1 = min(t1, MaxRayEnd);

	float3 entrypos = localcampos + t0 * localcamvec;
	float boxthickness = max(0, t1 - t0);

	return float4( entrypos, boxthickness );
}


float Steps = 128.0;
float2 XYFrames = FromBlueprint();
float StepSize = 1.0 / Steps; 
float NumSteps = floor(VolumeBoxIntersection().w * Steps);
float3 CurPos = VolumeBoxIntersection().xyz;
float Density = 16.0;
float ShadowSteps = 16.0;
float ShadowDensity = 4.0;
float ShadowThreshold = 0.01;
float3 LightVector =;
float3 LightColor =;
float3 SkyColor = ;
float AmbientDensity = 0.2;
float LightOffset = 0.75;
float Temperature = 10000;
float Jitter = Jitter();

void Density_RayMarch()
{
	float numFrames = XYFrames.x * XYFrames.y;
	float3 lightenergy = 0;
	float transmittance = 1;
	float3 localcamvec = -normalize( mul(Parameters.CameraVector, (float3x3)GetPrimitiveData(Parameters.PrimitiveId).WorldToLocal) );
	
	float shadowstepsize = 1 / ShadowSteps;
	LightVector *= shadowstepsize * 0.25;
	ShadowDensity *= shadowstepsize;

	Density *= StepSize;
	float shadowthresh = -log(ShadowThreshold) / ShadowDensity;

	CurPos +=  localcamvec * Jitter;

	for (int i = 0; i < NumSteps; i++)
	{	
		float4 cursample = PseudoVolumeTexture(Color, TexSampler, saturate(CurPos), XYFrames, numFrames);
		
		//Sample Light Absorption and Scattering
		if( cursample.r + cursample.g + cursample.b > 0.001)
		{
			float3 lpos = CurPos - LightVector * LightOffset;
			float shadowdist = 0;

			for (int s = 0; s < ShadowSteps; s++)
			{
				lpos += LightVector;
				float3 lsample = PseudoVolumeTexture(Color, TexSampler, saturate(lpos), XYFrames, numFrames).rgb;
				
				float3 shadowboxtest = floor( 0.5 + ( abs( 0.5 - lpos ) ) );
				float exitshadowbox = shadowboxtest .x + shadowboxtest .y + shadowboxtest .z;

	       		if(shadowdist > shadowthresh || exitshadowbox >= 1) 
	       			break;

				shadowdist += dot(lsample, 0.3333);
			}
			
			float3 StepTransmittance = exp(-cursample.rgb * Density);
			float3 curdensity = 1 - StepTransmittance;
			lightenergy += curdensity * transmittance * 
				(exp(-shadowdist * ShadowDensity) * LightColor + MaterialExpressionBlackBody(Temperature*cursample.a));
			
			transmittance *= dot(StepTransmittance, 0.333);
		
		#if 1 //Sky Lighting
			float3 lsample0 = PseudoVolumeTexture(Color, TexSampler, saturate(CurPos + float3(0,0,0.025)), XYFrames, numFrames).rgb;
			float3 lsample1 = PseudoVolumeTexture(Color, TexSampler, saturate(CurPos + float3(0,0,0.05 )), XYFrames, numFrames).rgb;
			float3 lsample2 = PseudoVolumeTexture(Color, TexSampler, saturate(CurPos + float3(0,0,0.15 )), XYFrames, numFrames).rgb;
			shadowdist = dot(lsample0 + lsample1 + lsample2, 0.3333);
			lightenergy += curdensity * transmittance * (exp(-shadowdist * AmbientDensity) * SkyColor);
		#endif

		}

		CurPos += localcamvec * StepSize;
	}

	return float4( lightenergy, transmittance);
}

