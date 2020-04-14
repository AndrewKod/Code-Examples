// Copyright 2016-2018 Chris Conway (Koderz). All Rights Reserved.

#include "RuntimeTerrainActor.h"

#include "RuntimeTerrainComponent.h"
#include "Runtime/Engine/Classes/Kismet/GameplayStatics.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Kismet/KismetSystemLibrary.h"


#include "RuntimeMeshLibrary.h"


#include "CaptureBoxComponent.h"

#include "CaptureCanvasRenderTarget2D.h"
#include "CompositeCanvasRenderTarget2D.h"
#include "PersistentCanvasRenderTarget2D.h"
#include "CustomSceneCaptureComponent2D.h"
#include "Engine/Canvas.h"

#include "Materials/MaterialParameterCollection.h"

//Material Expressions
//#include "Materials/MaterialExpression.h"
//#include "Materials/MaterialExpressionOneMinus.h"
//#include "Materials/MaterialExpressionTextureCoordinate.h"
//#include "Materials/MaterialExpressionTextureSample.h"
//#include "Materials/MaterialExpressionMultiply.h"
//#include "Materials/MaterialExpressionAdd.h"
//#include "Materials/MaterialExpressionSubtract.h"
//#include "Materials/MaterialExpressionMaterialFunctionCall.h"
//#include "Materials/MaterialExpressionFunctionInput.h"
//#include "Materials/MaterialExpressionFunctionOutput.h"
//#include "Materials/MaterialFunction.h"
//#include "Materials/MaterialExpressionConstant.h"
//#include "Materials/MaterialExpressionConstant2Vector.h"
//#include "Materials/MaterialExpressionVectorParameter.h"
//#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
//#include "Materials/MaterialExpressionWorldPosition.h"
//#include "Materials/MaterialExpressionComponentMask.h"


#include "ComponentReregisterContext.h"

#include "VoxelWorld.h"

#include "Octree.h"
#include "ChunkComponent.h"

#include "TransVoxel.h"

#include "Densities.h"
#include "DensityValue.h"
#include "ChunkPolygonizeTask.h"


#include <string>
using namespace std;


// Sets default values
ARuntimeTerrainActor::ARuntimeTerrainActor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
 	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;
	//PrimaryActorTick.TickGroup = TG_PostPhysics;

	SceneComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Scene"));	
		
	RootComponent = SceneComponent;	

	//Detalization = 9;
	Roughness = 0.7f;
	Stride = 5;
	Scale = 1.0;
	UVScale = 1.0;
	//PartSize = FMath::Pow(2, Detalization) + 1;
	Size = 512;
	PartSize = 11;
	MaxHeight = 2;
	//MaxHeight = PartSize - 1;

	PixelsPerUV = 100;
	HorizontalSurfaceTreshold = 10;

	MaxSnowHeight = 30;
	CaptureCellSize = 1;
	SnowElevationTreshold = 0.8;	

	FadingCellsPerFrame = 1;
	bTrailsFadingStarted = false;

	TerrainOctree = nullptr;
	densities = nullptr;

	ChunkSize = 4;
	ExtendedChunkSize = this->ChunkSize + 1;	

	VoxelSize = 100;
	OctreeDepth = 8;

	threadPool = FQueuedThreadPool::Allocate();
	verify(threadPool->Create(1, 1024, EThreadPriority::TPri_Lowest/*TPri_SlightlyBelowNormal*/));

	noiseFrequency = 1.f;
	noiseOctaveCount = 16;
	amplitude = 6000.f;
	heightOffset = 5000.f;

	editRadius = 100;
	editStrength = 0.1f;

	LODstep = 10000;
	LODtreshold = 4;
}

void ARuntimeTerrainActor::CreateOctree()
{
	/////////////////////////////////////////////////////////////OCTREE TESTS///////////////////////////////////////////////////////////////
	FVector OctreePosition =this->GetActorLocation(); /*FVector::ZeroVector;*/ 	
	this->TerrainOctree = new Octree<FOctreeData*>(OctreePosition, this->VoxelSize, this->ChunkSize, this->OctreeDepth);
	

	/*this->TerrainOctree->ExecuteOnLeafNodes(
	[](OctreeNode<FOctreeData*>* LeafNode)
	{
		FVector NodePosition = LeafNode->Position;
		float NodeHalfSize = LeafNode->Size / 2;
		FVector BoxPosition(NodePosition.X + NodeHalfSize, NodePosition.Y + NodeHalfSize, NodePosition.Z + NodeHalfSize);
		FVector BoxExtent(NodeHalfSize, NodeHalfSize, NodeHalfSize);
		FColor BoxColor(255 / 4 * LeafNode->Depth, 255, 0);

		DrawDebugBox(LeafNode->World.Get(), BoxPosition, BoxExtent, BoxColor, true, 100, (uint8)'\000', LeafNode->Depth + 2);
	});*/
	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
}

void ARuntimeTerrainActor::FillOctree()
{

}

void ARuntimeTerrainActor::CreateChunkComponent(int32 ChunkId, FVector ChunkWorldPosition,
	const TArray<FVector>& Vertices, const TArray<int32>& Indices, const TArray<FVector>& Normals,
	const TArray<FVector2D>& UVs, const TArray<FColor>& Colors, const TArray<FRuntimeMeshTangent>& Tangents)
{			
	if (Vertices.Num() > 0 && Indices.Num() > 0 && Normals.Num() > 0 && UVs.Num() > 0)
	{		
		if (!this->Chunks.Contains(ChunkId))
		{//Chunk Creation
			wstring ChunkName = L"Chunk_" + to_wstring(ChunkId);
			UChunkComponent* NewChunk = NewObject<UChunkComponent>(RootComponent, ChunkName.data());

			NewChunk->CreateMeshSection(0, Vertices, Indices, Normals, UVs, Colors, Tangents/*, false, EUpdateFrequency::Infrequent, ESectionUpdateFlags::None, true, true*/);
			NewChunk->SetMeshSectionCollisionEnabled(0, true);

			NewChunk->SetWorldLocation(ChunkWorldPosition);

			NewChunk->RegisterComponent();

			this->Chunks.Add(ChunkId, NewChunk);
		}
		else
		{
			UChunkComponent* chunk = this->Chunks[ChunkId];
			chunk->UpdateMeshSection(0, Vertices, Indices, Normals, UVs, Colors, Tangents/*, false, EUpdateFrequency::Infrequent, ESectionUpdateFlags::None, true, true*/);
		}
	}	
}


void ARuntimeTerrainActor::CreateTerrain()
{		
	this->densities = new FDensities(this->TerrainOctree->position, this->TerrainOctree->size, this->VoxelSize, this->ChunkSize);
	this->densities->GenerateDensities(this->noiseFrequency, this->noiseOctaveCount, this->amplitude, this->heightOffset);

	this->densitiesPtr = MakeShareable(this->densities);

	//this->StartPolygonizeTasks();
}

void ARuntimeTerrainActor::StartPolygonizeTasks()
{
	int SizeInChunks = this->densities->ChunksPerEdge();

	const TSet<uint32>& ChunksToUpdate = this->densities->GetChunksToUpdate();
	for (int32 ChunkId : ChunksToUpdate)
	{
		//calculate Chunk's left bot far corner coordinates in voxels in octree
		int32 X = ChunkId % SizeInChunks * this->ChunkSize;
		int32 Y = ChunkId / SizeInChunks % SizeInChunks * this->ChunkSize;
		int32 Z = ChunkId / SizeInChunks / SizeInChunks % SizeInChunks * this->ChunkSize;

		FVector ChunkWorldPosition = this->densities->ChunkLocalToWorldPosition(FVector(X, Y, Z));

		FIntVector chunkLocalPosition(X, Y, Z);

		FChunkBuffers* NewBuffer = new FChunkBuffers(ChunkWorldPosition, chunkLocalPosition);

		this->ChunkBuffers.Add(ChunkId, NewBuffer);

		float UVSize = 400.f;
		FAsyncTask<FChunkPolygonizeTask>* chunkPolygonizeTask = new FAsyncTask<FChunkPolygonizeTask>(ChunkId, chunkLocalPosition, ChunkWorldPosition, UVSize, this->densitiesPtr, this,
			&NewBuffer->Vertices,
			&NewBuffer->Indices,
			&NewBuffer->Normals,
			&NewBuffer->UVs,
			&NewBuffer->Colors,
			&NewBuffer->Tangents,
			&NewBuffer->bIsCreated/*, &NewBuffer->LOD*/
			);

		//add task to map for deletion in further
		this->chunkPolygonizeTasks.Add(ChunkId, chunkPolygonizeTask);
		chunkPolygonizeTask->StartBackgroundTask(this->threadPool);
	}

	this->densities->ClearChunksToUpdate();
}

void ARuntimeTerrainActor::CreateChunks(int32 chunksPerFrame)
{
	int32 chunksCount = 0;

	TArray<uint32> ElemsToRemove;
	for (auto& Elem : this->ChunkBuffers)
	{
		const int32& chunkId = Elem.Key;
		FChunkBuffers* chunkBuffers = Elem.Value;
		if (chunkBuffers->bIsCreated == true && this->chunkPolygonizeTasks[chunkId]->IsDone())
		{
			if (chunkBuffers->IsValid())
			{
				CreateChunkComponent(chunkId, chunkBuffers->chunkWorldPosition,
					chunkBuffers->Vertices, chunkBuffers->Indices, chunkBuffers->Normals,
					chunkBuffers->UVs, chunkBuffers->Colors, chunkBuffers->Tangents);

				chunkBuffers->ClearBuffers();

				chunksCount++;
			}

			ElemsToRemove.Add(chunkId);

			if (chunksCount >= chunksPerFrame)
				break;
		}
	}

	for (int32 i = 0; i < ElemsToRemove.Num(); i++)
	{
		//deleting FChunkBuffers because it was created with operation 'new'
		delete this->ChunkBuffers[ElemsToRemove[i]];
		this->ChunkBuffers.Remove(ElemsToRemove[i]);

		//deleting task because it was created with operation 'new'
		//this->chunkPolygonizeTasks[ElemsToRemove[i]]->EnsureCompletion();
		delete this->chunkPolygonizeTasks[ElemsToRemove[i]];
		this->chunkPolygonizeTasks.Remove(ElemsToRemove[i]);
	}
}

void ARuntimeTerrainActor::TestTask()
{
	UChunkComponent* TestChunk = NewObject<UChunkComponent>(RootComponent, "New Test Chunk");

	TestChunk->Initialize(this->GetWorld(), 4, 100, 0, FIntVector(0, 0, 0));

	TestChunk->CompleteMeshGeneration();

	TestChunk->SetWorldLocation(FVector(0, 0, 0));

	TestChunk->RegisterComponent();

}


// Called when the game starts or when spawned
void ARuntimeTerrainActor::BeginPlay()
{
	Super::BeginPlay();	

	//////////////////////////////////////////////////////////////ASYNC TESTS////////////////////////////////////////////////////////////////
	//TestTask();
	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////



	/////////////////////////////////////////////////////////////OCTREE TESTS///////////////////////////////////////////////////////////////
	CreateOctree();
	CreateTerrain();
	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	//this->Ticks = 0;

	//SetPlayerStartPosition();	


	//Temporary Disable Terrain Generation 
	//GenerateTerrain();		

	//for (int32 CellId = 0; CellId < TerrainParts[0]->CaptureCells.Num(); CellId++)
	//{	
	//	//THIS SETTING WORKING ONLY IN BeginPlay() or later
	//	TerrainParts[0]->CaptureCells[CellId].CaptureComponent2D->SetComponentTickEnabled(false);

	//	//Temporary Clear Runtime Mesh Sections
	//	//TerrainParts[0]->GetRuntimeMesh()->ClearAllMeshSections();
	//}	

	

}

void ARuntimeTerrainActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	threadPool->Destroy();
	threadPool = nullptr;

	delete this->TerrainOctree;
	/*this->densitiesPtr.Reset();
	delete this->densities;*/

	Super::EndPlay(EndPlayReason);		
}



// Called every frame
void ARuntimeTerrainActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);		

	this->StartPolygonizeTasks();

	//int32 chunksPerFrame = 1;//Default Parameter
	CreateChunks(/*chunksPerFrame*/);

	//uint32 ColorsPerPixel = 4;
	//int32 Size = 100;


	//static int32 count = 0;
	//static int32 count1 = 0;
	//static uint32 DestX = 0;	//region to update offset X (in width) in pixels
	//static uint32 DestY = 0;	//region to update offset Y (in height) in pixels
	//static uint32 height = 1;	//region to update height in pixels

	//int32 Interval = 50;

	//if (count == Size * Interval + 500)
	//{
	//	count = 0;
	//	count1 = 0;
	//	DestX = 0;	//region to update offset X (in width) in pixels
	//	DestY = 0;	//region to update offset Y (in height) in pixels
	//	height = 1;
	//}	
	//

	//if (count % Interval == 0 && count1 < Size)
	//{
	//	uint32 width = Size - count1;		//region to update width in pixels
	//	

	//	FUpdateTextureRegion2D* UpdateTextureRegion = new FUpdateTextureRegion2D(DestX, DestY, 0, 0, width, height);

	//	uint32 Size = width * height * 4;
	//	uint8* DynamicColors = new uint8[Size];

	//	for (uint32 i = 0; i < Size; i++)
	//	{
	//		DynamicColors[i] = 255;
	//	}
	//	if (count1 % 10 != 0 || count1 == 0)
	//	{
	//		this->TestUTexture2D->UpdateTextureRegions(0, 1,
	//			UpdateTextureRegion,
	//			width * ColorsPerPixel,							//region to update width in colors
	//			ColorsPerPixel,									//Colors per pixel
	//			DynamicColors/*,
	//			false*/);
	//	}
	//		//delete[]DynamicColors;

	//	DestX++;
	//	DestY++;
	//	//DestY += height;

	//	//height++;
	//	count1++;
	//}
	//

	//Test Generating Capture Cells After Voxel World Generation
	//count++;
	//static bool bCanGenerate = false;
	//static bool bGenerated = false;
	//
	//if (Ticks == 0)
	//{
	//	bGenerated = false;
	//}
	//if (Ticks == 1000)
	//{
	//	if (!bGenerated)
	//	{
	//		bGenerated = true;

	//		GenerateTerrain();

	//		for (int32 CellId = 0; CellId < TerrainParts[0]->CaptureCells.Num(); CellId++)
	//		{
	//			//THIS SETTING WORKING ONLY IN BeginPlay() or later
	//			TerrainParts[0]->CaptureCells[CellId].CaptureComponent2D->SetComponentTickEnabled(false);

	//			//Temporary Clear Runtime Mesh Sections
	//			TerrainParts[0]->GetRuntimeMesh()->ClearAllMeshSections();
	//		}
	//	}
	//}
	//this->Ticks++;

	///////////////////////////////////////////////////////UPDATE SNOW TRAILS///////////////////////////////////////////////////////////
	//if(bGenerated)
		//UpdateSnowTrails();
	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	if (TerrainParts.Num() > 0)
		TerrainParts[0]->BuildAdjacency();
}

void ARuntimeTerrainActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

#if WITH_EDITOR

	//RecreateCombineFunction();

#endif
}

#if WITH_EDITOR

void ARuntimeTerrainActor::PostEditChangeProperty(struct FPropertyChangedEvent& Event)
{	
	Super::PostEditChangeProperty(Event);

	FName PropertyName = (Event.Property != NULL) ? Event.Property->GetFName() : NAME_None;

	
	//Correction of PartSize if it isn't suitable with CaptureCellSize
	//And Recreating Combine_Render_Targets_Func Because This Properties Influencing On Render Targets Size And Amount
	if (PropertyName == GET_MEMBER_NAME_CHECKED(ARuntimeTerrainActor, CaptureCellSize) ||
		PropertyName == GET_MEMBER_NAME_CHECKED(ARuntimeTerrainActor, PartSize))
	{
		int32 MaxCellsNum = 100;//10*10=100
		int32 MaxCellsPerLength = 10;

		float CellsPerLength_f = (this->PartSize - 1) / this->CaptureCellSize;		
		if (CellsPerLength_f > MaxCellsPerLength)
		{
			this->PartSize = MaxCellsPerLength * this->CaptureCellSize + 1;
		}
		else //CellsPerLength <= MaxCellsPerLength
		{
			int32 LengthRest = (this->PartSize - 1) % this->CaptureCellSize;
			if (LengthRest > 0)
			{
				this->PartSize = this->PartSize - LengthRest + this->CaptureCellSize;
			}
		}
		

		//RecreateCombineFunction();
	}
	
	//Recreating Combine_Render_Targets_Func If Other Properties That Influencing On Render Targets Are Changed
	if (PropertyName == GET_MEMBER_NAME_CHECKED(ARuntimeTerrainActor, Scale) ||
		PropertyName == GET_MEMBER_NAME_CHECKED(ARuntimeTerrainActor, UVScale) ||
		PropertyName == GET_MEMBER_NAME_CHECKED(ARuntimeTerrainActor, Stride))
	{
		//RecreateCombineFunction();
	}	

	//Check if ChunkSize is power of 2
	if (PropertyName == GET_MEMBER_NAME_CHECKED(ARuntimeTerrainActor, ChunkSize))
	{
		//Round down ChunkSize to the closest power of 2
		if (this->ChunkSize > 1)
		{
			int32 PowOf2 = FMath::FloorLog2(this->ChunkSize);
			this->ChunkSize = FMath::Pow(2, PowOf2);

			//OctreeDepth correction for prevent Chunk larger Octree 
			if (this->OctreeDepth < PowOf2)
				this->OctreeDepth = PowOf2;
		}
	}

	//Check if Octree Size isn't lesser than ChunkSize
	if (PropertyName == GET_MEMBER_NAME_CHECKED(ARuntimeTerrainActor, OctreeDepth))
	{
		int32 PowOf2 = FMath::FloorLog2(this->ChunkSize);

		//ChunkSize correction for prevent Chunk larger Octree 
		if (PowOf2 > this->OctreeDepth)
			this->ChunkSize = FMath::Pow(2, this->OctreeDepth);
	}
}
#endif


void ARuntimeTerrainActor::GenerateTerrain()
{
	URuntimeTerrainComponent* TerrainPart = NewObject<URuntimeTerrainComponent>(RootComponent, "New Terrain Part");

	TerrainParts.Add(TerrainPart);


	TerrainPart->SetupAttachment(RootComponent);

	TerrainPart->SetWorldLocation(RootComponent->GetComponentLocation());
	TerrainPart->SetWorldRotation(RootComponent->GetComponentRotation());

	TerrainPart->SetCollisionUseComplexAsSimple(true);
	TerrainPart->SetSimulatePhysics(false);
	TerrainPart->SetCollisionProfileName(TEXT("BlockAll"));
	
	TerrainPart->SetCastShadow(false);

	//TerrainPart->Detalization = this->Detalization;
	TerrainPart->Roughness = this->Roughness;
	TerrainPart->Stride = this->Stride;
	TerrainPart->Scale = this->Scale;
	TerrainPart->UVScale = this->UVScale;
	TerrainPart->Length = this->PartSize;
	TerrainPart->Width = this->PartSize;
	TerrainPart->MaxHeight = this->MaxHeight;

	TerrainPart->PixelsPerUV = this->PixelsPerUV;
	TerrainPart->HorizontalSurfaceTreshold = this->HorizontalSurfaceTreshold;

	TerrainPart->MaxSnowHeight = this->MaxSnowHeight;
	TerrainPart->CaptureCellSize = this->CaptureCellSize;
	TerrainPart->SnowAmount = 0;
	TerrainPart->SnowElevationTreshold = this->SnowElevationTreshold;

	
	
	//Copy Materials
	if (this->LandscapeMaterial != nullptr)
	{
		UMaterial* Mat = Cast<UMaterial>(this->LandscapeMaterial);
		TerrainPart->DynamicLandscapeMaterial = UMaterialInstanceDynamic::Create(this->LandscapeMaterial, TerrainPart);		
	}

	if (this->PPMat_DepthCheck != nullptr)
		TerrainPart->PPMat_DepthCheck = this->PPMat_DepthCheck;	

	if (this->MPC_TrailsSettings != nullptr)
		TerrainPart->MPC_TrailsSettings = this->MPC_TrailsSettings;

	


	//Generate TerrainPart
	TerrainPart->GenerateTerrain(/*this->LandscapeMaterial*/);
	

	TerrainPart->RegisterComponent();

	//Set Terrain Parts Ticking before RuntimeTerrainActor
	for (int32 PartID = 0; PartID < this->TerrainParts.Num(); PartID++)
	{
		AddTickPrerequisiteComponent(TerrainParts[PartID]);
	}
}




void ARuntimeTerrainActor::SetPlayerStartPosition()
{
	URuntimeTerrainComponent* TerrainPart = TerrainParts[0];
	int32 X = FMath::RandRange(5, TerrainPart->Length - 6);
	int32 Y = FMath::RandRange(5, TerrainPart->Width - 6);
	int32 Z = TerrainPart->GetHeight(X, Y);

	FVector PlayerLocalPosition(X*TerrainPart->Stride, Y*TerrainPart->Stride,Z+1000);

	const FTransform& TerrainPartTransform = TerrainPart->GetComponentTransform();

	FVector PlayerGlobalPosition = TerrainPartTransform.TransformPositionNoScale(PlayerLocalPosition);

	TArray<AActor*> FoundActors;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), APlayerStart::StaticClass(), FoundActors);

	APlayerStart* PlayerStart= Cast<APlayerStart>(FoundActors[0]);

	if (PlayerStart != nullptr)
	{
		/*PlayerStart->GetCapsuleComponent()->SetCollisionProfileName("BlockAllDynamic");
		PlayerStart->SetActorLocation(PlayerGlobalPosition);
		PlayerStart->GetCapsuleComponent()->SetCollisionProfileName("NoCollision");*/

		PlayerStart->GetCapsuleComponent()->SetWorldLocation(PlayerGlobalPosition);
	}
}


void ARuntimeTerrainActor::AddR(float Amount)
{
	if(TerrainParts.Num() > 0)
		TerrainParts[0]->AddR(Amount);
}

void ARuntimeTerrainActor::AddG(float Amount)
{
	if (TerrainParts.Num() > 0)
		TerrainParts[0]->AddG(Amount);
}

void ARuntimeTerrainActor::AddB(float Amount)
{
	if (TerrainParts.Num() > 0)
		TerrainParts[0]->AddB(Amount);
}

void ARuntimeTerrainActor::AddA(float Amount)
{
	if (TerrainParts.Num() > 0)
		TerrainParts[0]->AddA(Amount);
}

void ARuntimeTerrainActor::SubtractR(float Amount)
{
	if (TerrainParts.Num() > 0)
		TerrainParts[0]->SubtractR(Amount);
}

void ARuntimeTerrainActor::SubtractG(float Amount)
{
	if (TerrainParts.Num() > 0)
		TerrainParts[0]->SubtractG(Amount);
}

void ARuntimeTerrainActor::SubtractB(float Amount)
{
	if (TerrainParts.Num() > 0)
		TerrainParts[0]->SubtractB(Amount);
}

void ARuntimeTerrainActor::SubtractA(float Amount)
{
	if (TerrainParts.Num() > 0)
		TerrainParts[0]->SubtractA(Amount);
}

///////////////////////////////////////////////////////////////////////////UPDATE SNOW TRAILS//////////////////////////////////////////////////////////////////////////////////

//TO DO: Implement Trails Fading By Using Shader Instead of Shaking System 
void ARuntimeTerrainActor::UpdateSnowTrails()
{
	//Update Composite RT every 10th frame 
	if (GFrameNumber % 10 == 0)
	{
		for (int32 PartId = 0; PartId < TerrainParts.Num(); PartId++)
		{
			//Draw CaptureCells To Composite Render Target If Needs
			if (TerrainParts[PartId]->CaptureCellsToDraw.Num() > 0)
			{
				FDrawToRenderTargetContext Context;
				UCanvas* Canvas = nullptr;
				UCompositeCanvasRenderTarget2D* RT_Composite = TerrainParts[PartId]->RT_Composite;
				FVector2D CanvasSize(RT_Composite->SizeX, RT_Composite->SizeX);

				
				UKismetRenderingLibrary::BeginDrawCanvasToRenderTarget(this, RT_Composite, Canvas, CanvasSize, Context);				
				for (int32 CellId: TerrainParts[PartId]->CaptureCellsToDraw)
				{					
					UCustomSceneCaptureComponent2D* CaptureComponent2D = TerrainParts[PartId]->CaptureCells[CellId].CaptureComponent2D;
					FVector2D ScreenPosition(CaptureComponent2D->WidthOffset, CaptureComponent2D->HeightOffset);					
					
					Canvas->K2_DrawTexture(CaptureComponent2D->RT_Capture, ScreenPosition, UCustomSceneCaptureComponent2D::Draw_Screen_Size,
						UCustomSceneCaptureComponent2D::OnePixel_UV_Size, UCustomSceneCaptureComponent2D::Draw_UV_Size);
					
				}
				UKismetRenderingLibrary::EndDrawCanvasToRenderTarget(this, Context);

				TerrainParts[PartId]->CaptureCellsToDraw.Empty();
			}

		}

		//Prepare For Trails Fading
		if (UCustomSceneCaptureComponent2D::bSnowing)
		{
			this->bTrailsFadingStarted = true;
			for (int32 CellCount = 0; CellCount < this->FadingCellsPerFrame; CellCount++)
			{
				int32 FadingCell = TerrainParts[0]->FadingCellsCount + CellCount;
				if (FadingCell >= TerrainParts[0]->CaptureCells.Num())
					break;
				//if Capture Cell Has Not Trails Yet - prevent trails fading for next 10 frames even if trails will appear during next 10 frames 
				if(TerrainParts[0]->CaptureCells[FadingCell].CaptureComponent2D->bHasTrails)
					TerrainParts[0]->CaptureCells[FadingCell].CaptureComponent2D->PreTrailsFading();
			}
		}		
		else
			this->bTrailsFadingStarted = false;

	}	
	//Implementation of trails fading during 1-8th frames
	else if (bTrailsFadingStarted && UCustomSceneCaptureComponent2D::bSnowing && GFrameNumber % 10 != 0 && GFrameNumber % 10 != 9)
	{
		for (int32 CellCount = 0; CellCount < this->FadingCellsPerFrame; CellCount++)
		{
			int32 FadingCell = TerrainParts[0]->FadingCellsCount + CellCount;
			if (FadingCell >= TerrainParts[0]->CaptureCells.Num())
				break;
			if(TerrainParts[0]->CaptureCells[FadingCell].CaptureComponent2D->bShouldFading)
				TerrainParts[0]->CaptureCells[FadingCell].CaptureComponent2D->ImplementTrailsFading();
		}
	}
	//Post trails fading on 9th frame
	else if (bTrailsFadingStarted && UCustomSceneCaptureComponent2D::bSnowing && GFrameNumber % 10 == 9)
	{
		for (int32 CellCount = 0; CellCount < this->FadingCellsPerFrame; CellCount++)
		{
			int32 FadingCell = TerrainParts[0]->FadingCellsCount + CellCount;
			if (FadingCell >= TerrainParts[0]->CaptureCells.Num())
				break;
			if (TerrainParts[0]->CaptureCells[FadingCell].CaptureComponent2D->bShouldFading)
				TerrainParts[0]->CaptureCells[FadingCell].CaptureComponent2D->PostTrailsFading();
		}
		TerrainParts[0]->FadingCellsCount += this->FadingCellsPerFrame;

		if (TerrainParts[0]->FadingCellsCount >= TerrainParts[0]->CaptureCells.Num())
			TerrainParts[0]->FadingCellsCount = 0;
	}	
}

void ARuntimeTerrainActor::ClearRenderTargets()
{
	UKismetRenderingLibrary::ClearRenderTarget2D(this, TerrainParts[0]->CaptureCells[0].CaptureComponent2D->RT_Capture);	
}

void ARuntimeTerrainActor::ChangeMaterialQuality()
{
	////Execute Mud Dry Cracked Parallax?
	//FMaterialParameterInfo ParamInfo("Execute Mud Dry Cracked Parallax?");
	//float Value = 0.f;
	//TerrainParts[0]->DynamicLandscapeMaterial->GetScalarParameterValue(ParamInfo, Value);
	//if(Value==0.f)
	//	TerrainParts[0]->DynamicLandscapeMaterial->SetScalarParameterValue("Execute Mud Dry Cracked Parallax?", 1.f);
	//else
	//	TerrainParts[0]->DynamicLandscapeMaterial->SetScalarParameterValue("Execute Mud Dry Cracked Parallax?", 0.f);
}

void ARuntimeTerrainActor::ChangeMudDryCrackedQuality()
{
	//Execute Mud Dry Cracked Parallax?
	FMaterialParameterInfo ParamInfo("Execute Mud Dry Cracked Parallax?");
	float Value = 0.f;
	TerrainParts[0]->DynamicLandscapeMaterial->GetScalarParameterValue(ParamInfo, Value);
	if (Value == 0.f)
		TerrainParts[0]->DynamicLandscapeMaterial->SetScalarParameterValue("Execute Mud Dry Cracked Parallax?", 1.f);
	else
		TerrainParts[0]->DynamicLandscapeMaterial->SetScalarParameterValue("Execute Mud Dry Cracked Parallax?", 0.f);
}

void ARuntimeTerrainActor::ChangeSandDirtyQuality()
{
	//Execute Sand Dirty Parallax?
	FMaterialParameterInfo ParamInfo("Execute Sand Dirty Parallax?");
	float Value = 0.f;
	TerrainParts[0]->DynamicLandscapeMaterial->GetScalarParameterValue(ParamInfo, Value);
	if (Value == 0.f)
		TerrainParts[0]->DynamicLandscapeMaterial->SetScalarParameterValue("Execute Sand Dirty Parallax?", 1.f);
	else
		TerrainParts[0]->DynamicLandscapeMaterial->SetScalarParameterValue("Execute Sand Dirty Parallax?", 0.f);
}

void ARuntimeTerrainActor::ChangeSnowQuality()
{
	//Execute Snow Parallax?
	FMaterialParameterInfo ParamInfo("Execute Snow Parallax?");
	float Value = 0.f;
	TerrainParts[0]->DynamicLandscapeMaterial->GetScalarParameterValue(ParamInfo, Value);
	if (Value == 0.f)
		TerrainParts[0]->DynamicLandscapeMaterial->SetScalarParameterValue("Execute Snow Parallax?", 1.f);
	else
		TerrainParts[0]->DynamicLandscapeMaterial->SetScalarParameterValue("Execute Snow Parallax?", 0.f);
}


void ARuntimeTerrainActor::SetSnowing(bool bSnowing)
{	
	UCustomSceneCaptureComponent2D::bSnowing = bSnowing;	
}

void ARuntimeTerrainActor::ChangeEditRadius(int32 amount, bool bIncrease)
{
	this->editRadius += amount * (-1 * !bIncrease);
	this->editRadius = FMath::Clamp(this->editRadius, 100, INT32_MAX);
}

void ARuntimeTerrainActor::ChangeEditStrength(float amount, bool bIncrease)
{
	this->editStrength += amount * (-1 * !bIncrease);
	this->editStrength = FMath::Clamp(this->editStrength, 0.1f, 1.0f);
}

void ARuntimeTerrainActor::EditLandscape(FVector worldPosition, bool bAddTerrain)
{
	TMap<uint32, FDensityValue> modifiedDensities;

	int32 buildModifier = bAddTerrain ? 1 : -1;

	//Local position in world units
	FVector localPosition = worldPosition - this->GetActorLocation() + this->TerrainOctree->GetOctreeSize()/2;

	//Local position in voxels
	FVector positionV = localPosition / this->VoxelSize;

	//editRadius in voxels
	float editRadiusV = this->editRadius / this->VoxelSize;

	int edgeSize = this->densities->DensitiesPerEdge();

	//Start coordinates in voxels
	int32 startX = FMath::Clamp(FMath::CeilToInt(positionV.X - editRadiusV), 0, edgeSize - 1);
	int32 startY = FMath::Clamp(FMath::CeilToInt(positionV.Y - editRadiusV), 0, edgeSize - 1);
	int32 startZ = FMath::Clamp(FMath::CeilToInt(positionV.Z - editRadiusV), 0, edgeSize - 1);

	//End coordinates in voxels
	int32 endX = FMath::Clamp(FMath::RoundToInt(positionV.X + editRadiusV), 0, edgeSize - 1);
	int32 endY = FMath::Clamp(FMath::RoundToInt(positionV.Y + editRadiusV), 0, edgeSize - 1);
	int32 endZ = FMath::Clamp(FMath::RoundToInt(positionV.Z + editRadiusV), 0, edgeSize - 1);

	const TMap<uint32, FDensityValue>& densitiesMap = this->densities->GetDensities();

	//Densities that are NOT set yet
	//Key - xyz position, value - Id
	TMap<FIntVector, uint32> undefinedDensities;

	//Densities that are set yet
	//Key - xyz position, value - Id
	TMap<FIntVector, uint32> definedDensities;

	bool bHasInaccessibleChunks = false;
	for (int32 z = startZ; z <= endZ; z++)
	{
		for (int32 y = startY; y <= endY; y++)
		{
			for (int32 x = startX; x <= endX; x++)
			{
				//Get Affected Chunks
				TSet<uint32> chunkIds;

				this->densities->GetAffectedChunkIds(x, y, z, chunkIds);
				
				
				for (uint32& chunkId : chunkIds)
				{
					if (this->ChunkBuffers.Contains(chunkId) && !this->ChunkBuffers[chunkId]->bIsCreated)
					{
						//this->densitiesPtr->ClearChunksToUpdate();
						this->densities->ClearChunksToUpdate();
						modifiedDensities.Empty();
						undefinedDensities.Empty();
						definedDensities.Empty();
						bHasInaccessibleChunks = true;
						break;
					}
					//this->densitiesPtr->AddChunkToUpdate(chunkId);
					this->densities->AddChunkToUpdate(chunkId);
				}

				if (bHasInaccessibleChunks)
				{
					break;
				}

				FVector distanceVec = positionV - FVector(x, y, z);

				float distance = distanceVec.Size();
				if (distance > editRadiusV)
				{
					//modifiedDensities.Empty();
					continue;
				}

				int32 densityId = z * edgeSize * edgeSize + y * edgeSize + x;				

				float modificationAmount = this->editStrength / distance * buildModifier;

				
				if (densitiesMap.Contains(densityId))
				{
					FDensityValue oldDensity = densitiesMap[densityId];
					FDensityValue newDensity = FDensityValue(FMath::Clamp(oldDensity.ToFloat() - modificationAmount, -1.f, 1.f));
					modifiedDensities.Add(densityId, newDensity);	
					definedDensities.Add(FIntVector(x, y, z), densityId);
				}
				else
				{
					/*oldDensity = FDensityValue(bAddTerrain ? 1.f : -1.f);*/
					undefinedDensities.Add(FIntVector(x, y, z), densityId);
				}

							
			}
			if (bHasInaccessibleChunks)
				break;
		}
		if (bHasInaccessibleChunks)
			break;
	}


	for (auto& Elem : modifiedDensities)
	{
		//this->densitiesPtr->AddDensity(Elem.Key, Elem.Value);
		this->densities->AddDensity(Elem.Key, Elem.Value);
	}

	//Set undefined densities values
	while (undefinedDensities.Num() > 0)
	{
		//Get First Element of map
		auto It = undefinedDensities.CreateConstIterator();

		TSet<uint32> densitiesGroup;
		TSet<FIntVector> visitedDensities;

		bool bIsInEmptySpace = true;
		bool bPositionDefined = false;

		//Recursive search of groups of densities from inside or outside of surface
		SearchDensitiesGroup(It->Key, It->Value, modifiedDensities, undefinedDensities, definedDensities,
												visitedDensities, densitiesGroup, bIsInEmptySpace, bPositionDefined);

		//Add found densities if necessary 
		//If terrain is adding - we need to add only densities from empty side of surface
		if ((bAddTerrain && bIsInEmptySpace) || (!bAddTerrain && !bIsInEmptySpace))
		{
			for (uint32& densityId : densitiesGroup)
			{
				this->densities->AddDensity(densityId, FDensityValue(bAddTerrain ? 1.f : -1.f));
			}
		}
	}
	
}

//Recursive search of groups of densities from inside or outside of surface
void ARuntimeTerrainActor::SearchDensitiesGroup(FIntVector key, uint32 value,
	const TMap<uint32, FDensityValue>& modifiedDensities,
	TMap<FIntVector, uint32>& undefinedDensities,
	TMap<FIntVector, uint32>& definedDensities,
	TSet<FIntVector>& visitedDensities,
	TSet<uint32>& densitiesGroup,
	bool& bIsInEmptySpace, bool& bPositionDefined)
{
	undefinedDensities.Remove(key);
	visitedDensities.Add(key);
	densitiesGroup.Add(value);

	//Search neighbour density by it's coordinaes
	for (int32 i = 0; i < 3; i++)
	{
		for (int32 j = -1; j <= 1; j += 2)
		{
			FIntVector newCoords = key;
			newCoords[i] += j;

			//Prevent to process already visited densities
			if (!visitedDensities.Contains(newCoords))
			{
				//Add element to visitedDensities to prevent it's usage in further
				visitedDensities.Add(newCoords);

				//corner at newCoords has defined value - than all densities found during recursion
				//will be on the same side of surface
				if (definedDensities.Contains(newCoords))
				{
					//Expected that recursive search will never reaches corners from opposite side of surface
					if (modifiedDensities[definedDensities[newCoords]].ToFloat() > 0)
					{
						if (!bPositionDefined)
						{
							bIsInEmptySpace = true;
							bPositionDefined = true;
						}
						else
						{
							check(bIsInEmptySpace);
						}
					}
					else
					{
						if (!bPositionDefined)
						{
							bIsInEmptySpace = false;
							bPositionDefined = true;
						}
						else
						{
							check(!bIsInEmptySpace);
						}
					}				
				}
				//Continue Recursion
				else if (undefinedDensities.Contains(newCoords))
				{
					SearchDensitiesGroup(newCoords, undefinedDensities[newCoords], modifiedDensities, undefinedDensities, definedDensities,
						visitedDensities, densitiesGroup, bIsInEmptySpace, bPositionDefined);
				}
			}

		}
	}
}