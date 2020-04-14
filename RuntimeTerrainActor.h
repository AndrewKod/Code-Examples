// Copyright 2016-2018 Chris Conway (Koderz). All Rights Reserved.

#pragma once

//#include "DensityValue.h"

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RuntimeTerrainActor.generated.h"

#define EDGE_INDEX_COUNT 4

class URuntimeTerrainComponent;

template<class InElementType>
class Octree;

template<class ElementType>
class OctreeNode;

class UChunkComponent;

class FDensities;
struct FDensityValue;

//class FAsyncTask;
class FChunkPolygonizeTask;

class FOctreeData {
	
	//position of left bot far corner in octree in voxels
	FIntVector cornerPosition;

public:

	FOctreeData() {}

	FOctreeData(FIntVector cornerPosition)
		:		
		cornerPosition(cornerPosition)
	{}

	~FOctreeData() {}
};

class FChunkBuffers {

public:	
	uint8 LOD;

	FVector chunkWorldPosition;
	FIntVector chunkLocalPosition;

	TArray<FVector> Vertices;
	TArray<int32> Indices;
	TArray<FVector> Normals;
	TArray<FVector2D> UVs;
	TArray<FColor> Colors;
	TArray<FRuntimeMeshTangent> Tangents;

	//Indicates if chunk buffers are generated already 
	FThreadSafeBool bIsCreated;

	FChunkBuffers() :bIsCreated(false) {}
	FChunkBuffers(FVector chunkWorldPosition, FIntVector chunkLocalPosition, uint8 LOD = 0)
		:
		chunkWorldPosition(chunkWorldPosition),
		chunkLocalPosition(chunkLocalPosition),
		LOD(LOD),
		bIsCreated(false)
	{}

	bool IsValid() {
		return Vertices.Num() > 0 && Indices.Num() > 0 && Normals.Num() > 0 && UVs.Num() > 0;
	}

	void ClearBuffers()
	{
		Vertices.Empty();
		Indices.Empty();
		Normals.Empty();
		UVs.Empty();
		Colors.Empty();
		Tangents.Empty();
	}
};




UCLASS()
class RUNTIMEMESHCOMPONENT_API ARuntimeTerrainActor : public AActor
{
	GENERATED_BODY()

		//int32 Ticks = 0;

public:	
	// Sets default values for this actor's properties
	//ARuntimeTerrainActor();
	ARuntimeTerrainActor(const FObjectInitializer& ObjectInitializer);

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:	
	// Called every frame
	virtual void Tick(float DeltaTime) override;

protected:

	virtual void OnConstruction(const FTransform& Transform) override;

#if WITH_EDITOR
	
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& Event) override;

	//for TArrays:
	//virtual void PostEditChangeChainProperty(struct FPropertyChangedChainEvent& ChainEvent) override;

	//virtual void OnConstruction(const FTransform& Transform) override;
#endif

public:

	

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Scene Component", DisplayName = "Scene")
	USceneComponent* SceneComponent;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Material", DisplayName = "Landscape Material")
	UMaterialInterface* LandscapeMaterial;
	
	UPROPERTY(EditDefaultsOnly, Transient, BlueprintReadOnly, Category = "Terrain Parts", DisplayName = "Terrain Parts")
	TArray<URuntimeTerrainComponent*> TerrainParts;

	
	///////////////////////////////////////////////////////////////////"Terrain Parameters"//////////////////////////////////////////////////////////////

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain Parameters", DisplayName = "Terrain Size",
		meta = (ClampMin = "2.0", UIMin = "2.0"))
		/**
		Length And Width of Terrain
		**/		
		int32 Size;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain Parameters", DisplayName = "Terrain Part Size",
		meta = (ClampMin = "2.0", UIMin = "2.0"))
	/**
	Length And Width of TerrainParts
	**/
	/**Currently Heightmap is 2-dimensional array of height values
	X is rows, Y is columns
	But looks like in UE4 it mirrored by Y-axis
	So ↓(X)→(Y) turns into ↑(X)→(Y)
	
	↓(X)
	**/
	/**
	Texture Parameters are limited to 128 (see UE_4.20\Engine\Source\Runtime\Engine\Private\Materials\MaterialUniformExpressions.cpp)
	It important for Combine_Render_Targets_Func in further
	So the ratio of PartSize and CaptureCellSize should be such that result CaptureCells Num will be no larger than 128
	And PartLength and PartWidth must suit to integer CaptureCells Num
	**/
	int32 PartSize;
	

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain Parameters", DisplayName = "Max Height",
		meta = (ClampMin = "0", UIMin = "0"))
	int32 MaxHeight;

	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain Parameters", DisplayName = "Stride",
		meta = (ClampMin = "0.0", UIMin = "0.0"))
	//Distance between two points
	float Stride;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain Parameters", DisplayName = "Scale",
		meta = (ClampMin = "0.0", UIMin = "0.0"))
	float Scale;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain Parameters", DisplayName = "UV Scale",
		meta = (ClampMin = "0.0", UIMin = "0.0"))
	float UVScale;

	/*UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain Parameters", DisplayName = "Detalization",
		meta = (ClampMin = "0", UIMin = "0"))
	int32 Detalization;*/

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain Parameters", DisplayName = "Roughness",
		meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	float Roughness;

	///////////////////////////////////////////////////////////////Material Parameters Collection/////////////////////////////////////////////////////////



	//////////////////////////////////////////////////////////////////"Puddle Map Parameters"/////////////////////////////////////////////////////////////

	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Puddle Map Parameters", DisplayName = "Pixels Per UV",
		meta = (ClampMin = "0", UIMin = "0"))
	/**
	Pixels amount between 0 and 1 UV coordinates
	Puddle Map texture generated for whole Terrain Part (orthogonal or rectangular mesh) and its 0 and 1 UVs are in the corners of this mesh
	But other textures are left with original UV
 	**/
	int32 PixelsPerUV;

	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Puddle Map Parameters", DisplayName = "Horizontal Surface Treshold",
		meta = (ClampMin = "0.0", ClampMax = "90.0", UIMin = "0.0", UIMax = "90.0"))
	/**
	Defines angle in degrees (0 - 90) at which polygons can be considered as Horizontal surface for puddles appearance
	If polygon isn't Horizontal - it remains wet, but without puddles
	**/
	float HorizontalSurfaceTreshold;


	/////////////////////////////////////////////////////////////// Snow Trails Capture ///////////////////////////////////////////////////////////

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Snow Trails Capture", DisplayName = "Max Snow Height",
		meta = (ClampMin = "0.0", ClampMax = "50.0", UIMin = "0.0", UIMax = "50.0"))
	float MaxSnowHeight;

	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Snow Trails Capture", DisplayName = "Capture Cell Size",
		meta = (ClampMin = "1.0", ClampMax = "11.0"))
	/**Determines how many mesh cells (square composed of two mesh triangles) will be inside snow trails capture cell**/
	/**Texture Parameters are limited to 128 (see UE_4.20\Engine\Source\Runtime\Engine\Private\Materials\MaterialUniformExpressions.cpp)
	It important for Combine_Render_Targets_Func in further
	So the ratio of PartSize and  CaptureCellSize should be such that result CaptureCells Num will be no larger than 116
	And PartLength and PartWidth must suit to integer CaptureCells Num
	I Found Information On Forums That TextureSamples Number is limited:
	16 if SamplerSource Mode is "From texture asset"
    128 if "Shared: Wrap"
	But in fact this values are 14 and 116 respectively
	**/
	int32 CaptureCellSize;	

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Snow Trails Capture", DisplayName = "Snow Elevation Treshold")
		/**Vertex Color Alpha After Which Snow Starts Elevating**/
		float SnowElevationTreshold;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Snow Trails Capture", DisplayName = "Depth Check Material")
		/**Post Process Depth Check Material**/
		UMaterialInterface* PPMat_DepthCheck;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Snow Trails Capture", DisplayName = "Trails Settings Parameter Collection")
		 class UMaterialParameterCollection* MPC_TrailsSettings;
	   
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Snow Trails Capture", DisplayName = "Fading Cells Per Frame")
		int32 FadingCellsPerFrame;

	bool bTrailsFadingStarted;


	Octree<FOctreeData*>* TerrainOctree;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Voxel", DisplayName = "Voxel Size In World Units",
		meta = (ClampMin = "1.0"))
		int32 VoxelSize;

	//Chunk Size In Voxels
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Voxel", DisplayName = "Chunk Size In Voxels",
		meta = (ClampMin = "1.0", ClampMax = "256.0"))
		int32 ChunkSize;
	//Chunk Size In Densities
	int32 ExtendedChunkSize;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Voxel", DisplayName = "Octree Depth",
		meta = (ClampMin = "0.0", ClampMax = "8.0"))
		int32 OctreeDepth;
	
	TMap<uint32, FChunkBuffers*> ChunkBuffers;
	TMap<uint32, FAsyncTask<FChunkPolygonizeTask>*> chunkPolygonizeTasks;
	TMap<uint32, UChunkComponent*> Chunks;
	FQueuedThreadPool* threadPool;
	//TArray<int32> ChunksToUpdate;
	FCriticalSection mutex;	

	FDensities* densities;
	TSharedPtr<FDensities, ESPMode::ThreadSafe> densitiesPtr;

	//Landscape Generation Settings

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Landscape Generation Settings", DisplayName = "Noise Frequency",
		meta = (ClampMin = "1.0"))
		float noiseFrequency;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Landscape Generation Settings", DisplayName = "Noise Octave Count",
		meta = (ClampMin = "1.0", ClampMax = "16.0"))
		int32 noiseOctaveCount;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Landscape Generation Settings", DisplayName = "Amplitude",
		meta = (ClampMin = "0.0"))
		float amplitude;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Landscape Generation Settings", DisplayName = "Height Offset")
		float heightOffset;


	//Landscape Edition

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Landscape Edition", DisplayName = "Radius",
		meta = (ClampMin = "100.0"))
		int32 editRadius;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Landscape Edition", DisplayName = "Strength",
		meta = (ClampMin = "0.1", ClampMax = "1.0"))
		float editStrength;

	
	//LOD Update

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "LOD Update", DisplayName = "LOD Step",
		meta = (ClampMin = "1000.0", ClampMax = "1.0"))
		//Distance from camera to LODs
		int32 LODstep;

	
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "LOD Update", DisplayName = "LOD Treshold",
		meta = (ClampMin = "1.0", ClampMax = "8.0"))
		//LOD with lowest quality
		int32 LODtreshold;

public:

	void CreateOctree();
	void FillOctree();
	void CreateTerrain();

private:

	void StartPolygonizeTasks();
	void CreateChunks(int32 chunksPerFrame = 1);

	//Recursive search of groups of densities from inside or outside of surface
	void SearchDensitiesGroup(FIntVector key, uint32 value,
		const TMap<uint32, FDensityValue>& modifiedDensities,
		TMap<FIntVector, uint32>& undefinedDensities,
		TMap<FIntVector, uint32>& definedDensities,
		TSet<FIntVector>& visitedDensities,
		TSet<uint32>& densitiesGroup,
		bool& bIsInEmptySpace, bool& bPositionDefined);


public:
	
	void CreateChunkComponent(int32 ChunkId, FVector ChunkWorldPosition,
		const TArray<FVector>& Vertices, const TArray<int32>& Indices, const TArray<FVector>& Normals,
		const TArray<FVector2D>& UVs, const TArray<FColor>& Colors, const TArray<struct FRuntimeMeshTangent>& Tangents);

	void GenerateTerrain();	

	UFUNCTION(BlueprintCallable)
	void AddR(float Amount);

	UFUNCTION(BlueprintCallable)
	void AddG(float Amount);

	UFUNCTION(BlueprintCallable)
	void AddB(float Amount);

	UFUNCTION(BlueprintCallable)
	void AddA(float Amount);

	UFUNCTION(BlueprintCallable)
	void SubtractR(float Amount);

	UFUNCTION(BlueprintCallable)
	void SubtractG(float Amount);

	UFUNCTION(BlueprintCallable)
	void SubtractB(float Amount);

	UFUNCTION(BlueprintCallable)
	void SubtractA(float Amount);

	UFUNCTION(BlueprintCallable)
	void ClearRenderTargets();

	UFUNCTION(BlueprintCallable)
	void ChangeMaterialQuality();

	UFUNCTION(BlueprintCallable)
	void ChangeMudDryCrackedQuality();

	UFUNCTION(BlueprintCallable)
	void ChangeSandDirtyQuality();

	UFUNCTION(BlueprintCallable)
	void ChangeSnowQuality();

	UFUNCTION(BlueprintCallable)
	void SetSnowing(bool bSnowing);


	//Landscape Edition
	UFUNCTION(BlueprintCallable)
	void ChangeEditRadius(int32 amount, bool bIncrease);

	UFUNCTION(BlueprintCallable)
	void ChangeEditStrength(float amount, bool bIncrease);

	UFUNCTION(BlueprintCallable)
	void EditLandscape(FVector worldPosition, bool bAddTerrain);

	
private:			

	void SetPlayerStartPosition();

	void UpdateSnowTrails();



	void TestTask();
};
