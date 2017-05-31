// Fill out your copyright notice in the Description page of Project Settings.

#include "City.h"
#include "Spawner.h"



// Sets default values
ASpawner::ASpawner()
{
 	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;

	PathSpline = CreateDefaultSubobject<USplineMeshComponent>(TEXT("Path"));

	FString pathName = "StaticMesh'/Game/road.road'";
	ConstructorHelpers::FObjectFinder<UStaticMesh> buildingMesh(*pathName);
	meshRoad = buildingMesh.Object;
}



int convertToMapIndex(int x, int y) {
	return x * 10000 + y;
}

void addRoadToMap(TMap <int, TArray<FRoadSegment*>*> &map, FRoadSegment* current, float intervalLength) {
	FVector middle = (current->p2 - current->p1) / 2 + current->p1;
	int x = FMath::RoundToInt((middle.X / intervalLength));
	int y = FMath::RoundToInt((middle.Y / intervalLength));
	for (int i = -1; i <= 1; i++) {
		for (int j = -1; j <= 1; j++) {
			if (map.Find(convertToMapIndex(i+x, j+y)) == nullptr) {
				TArray<FRoadSegment*>* newArray = new TArray<FRoadSegment*>();
				newArray->Add(current);
				map.Emplace(convertToMapIndex(i+x, j+y), newArray);
			}
			else {
				map[convertToMapIndex(i+x, j+y)]->Add(current);
			}
		}
	}

}

TArray<TArray<FRoadSegment*>*> getRelevantSegments(TMap <int, TArray<FRoadSegment*>*> &map, FRoadSegment* current, float intervalLength) {
	TArray<TArray<FRoadSegment*>*> allInteresting;
	FVector middle = (current->p2 - current->p1) / 2 + current->p1;
	int x = FMath::RoundToInt((middle.X / intervalLength));
	int y = FMath::RoundToInt((middle.Y / intervalLength));

	for (int i = -1; i <= 1; i++) {
		for (int j = -1; j <= 1; j++) {
			if (map.Find(convertToMapIndex(i+x, j+y)) != nullptr) {
				allInteresting.Add(map[convertToMapIndex(i+x, j+y)]);
			}
		}
	}

	return allInteresting;

}


void ASpawner::addVertices(FRoadSegment* road) {
	road->v1 = road->p1 + FRotator(0, 90, 0).RotateVector(road->beginTangent) * standardWidth*road->width / 2;
	road->v2 = road->p1 - FRotator(0, 90, 0).RotateVector(road->beginTangent) * standardWidth*road->width / 2;
	FVector endTangent = road->p2 - road->p1;
	endTangent.Normalize();
	road->v3 = road->p2 + FRotator(0, 90, 0).RotateVector(endTangent) * standardWidth*road->width / 2;
	road->v4 = road->p2 - FRotator(0, 90, 0).RotateVector(endTangent) * standardWidth*road->width / 2;

}




bool ASpawner::placementCheck(TArray<FRoadSegment*> &segments, logicRoadSegment* current, TMap <int, TArray<FRoadSegment*>*> &map){

	// use SAT collision between all roads
	FVector tangent1 = current->segment->p2 - current->segment->p1;
	tangent1.Normalize();
	FVector tangent2 = FRotator(0, 90, 0).RotateVector(tangent1);

	addVertices(current->segment);
	TArray<FVector> vert1;
	vert1.Add(current->segment->v1);
	vert1.Add(current->segment->v2);
	vert1.Add(current->segment->v3);
	vert1.Add(current->segment->v4);

	for (FRoadSegment* f : segments){

		TArray<FVector> tangents;

		// can't be too close to another segment
		if (FVector::Dist((f->p2 - f->p1) / 2 + f->p1, (current->segment->p2 - current->segment->p1) / 2 + current->segment->p1) < 7000) {
			return false;
		}


		FVector tangent3 = f->p2 - f->p1;
		FVector tangent4 = FRotator(0, 90, 0).RotateVector(tangent3);

		tangents.Add(tangent1);
		tangents.Add(tangent2);
		tangents.Add(tangent3);
		tangents.Add(tangent4);

		TArray<FVector> vert2;
		vert2.Add(f->v1);
		vert2.Add(f->v2);
		vert2.Add(f->v3);
		vert2.Add(f->v4);

		if (testCollision(tangents, vert1, vert2, collisionLeniency)) {
			FVector newE = intersection(current->segment->p1, current->segment->p2, f->p1, f->p2);
			current->time = 100000;
			if (newE.X == 0) {
				// the lines themselves are not colliding, its an edge case
				continue;
			}
			else {
				FVector tangent = newE - current->segment->p1;
				tangent.Normalize();
				float len = FVector::Dist(newE, current->segment->p2);
				current->segment->p2 += (len - standardWidth / 2) * tangent;
				current->segment->p2 = newE;
				//addVertices(current->segment);
				current->segment->roadInFront = true;

				FVector naturalTangent = current->segment->p2 - current->segment->p1;
				naturalTangent.Normalize();
				FVector pot1 = FRotator(0, 90, 0).RotateVector(f->p2 - f->p1);
				FVector pot2 = FRotator(0, 270, 0).RotateVector(f->p2 - f->p1);
				current->segment->endTangent = FVector::DistSquared(naturalTangent, pot1) < FVector::DistSquared(naturalTangent, pot2) ? pot1 : pot2;
				addVertices(current->segment);
				// new road cant be too short
				if (FVector::Dist(current->segment->p1, current->segment->p2) < primaryStepLength.Size() / 6) {
					return false;
				}
			}
		}


	}

	return true;
	

}


void ASpawner::addRoadForward(std::priority_queue<logicRoadSegment*, std::deque<logicRoadSegment*>, roadComparator> &queue, logicRoadSegment* previous, std::vector<logicRoadSegment*> &allsegments) {
	FRoadSegment* prevSeg = previous->segment;
	logicRoadSegment* newRoadL = new logicRoadSegment();
	FRoadSegment* newRoad = new FRoadSegment();

	newRoadL->secondDegreeRot = previous->secondDegreeRot + FRotator(0, (prevSeg->type == RoadType::main ? changeIntensity : secondaryChangeIntensity)*(randFloat() - 0.5f), 0);
	newRoadL->firstDegreeRot = previous->firstDegreeRot + newRoadL->secondDegreeRot;
	FVector stepLength = prevSeg->type == RoadType::main ? primaryStepLength : secondaryStepLength;

	newRoad->p1 = prevSeg->p2;
	newRoad->p2 = newRoad->p1 + newRoadL->firstDegreeRot.RotateVector(stepLength);
	newRoad->beginTangent = prevSeg->p2 - prevSeg->p1;
	newRoad->beginTangent.Normalize();
	newRoad->width = prevSeg->width;
	newRoad->type = prevSeg->type;
	newRoad->endTangent = newRoad->p2 - newRoad->p1;
	newRoadL->segment = newRoad;
	newRoadL->time = previous->segment->type != RoadType::main ? previous->time + 1 : previous->time;
	newRoadL->roadLength = previous->roadLength + 1;
	newRoadL->previous = previous;
	addVertices(newRoad);
	queue.push(newRoadL);
	allsegments.push_back(newRoadL);
	
}

void ASpawner::addRoadSide(std::priority_queue<logicRoadSegment*, std::deque<logicRoadSegment*>, roadComparator> &queue, logicRoadSegment* previous, bool left, float width, std::vector<logicRoadSegment*> &allsegments, RoadType newType) {
	FRoadSegment* prevSeg = previous->segment;
	logicRoadSegment* newRoadL = new logicRoadSegment();
	FRoadSegment* newRoad = new FRoadSegment();

	newRoadL->secondDegreeRot = FRotator(0, (prevSeg->type == RoadType::main ? changeIntensity : secondaryChangeIntensity)*(randFloat() - 0.5f), 0);
	FRotator newRotation = left ? FRotator(0, 90, 0) : FRotator(0, 270, 0);
	newRoadL->firstDegreeRot = previous->firstDegreeRot + newRoadL->secondDegreeRot + newRotation;

	FVector stepLength = newType == RoadType::main ? primaryStepLength : secondaryStepLength;
	FVector startOffset = newRoadL->firstDegreeRot.RotateVector(FVector(standardWidth*previous->segment->width / 2, 0, 0));
	newRoad->p1 = /*prevSeg->end - (standardWidth * (prevSeg->end - prevSeg->start).Normalize()/2) + startOffset; */prevSeg->p1 + (prevSeg->p2 - prevSeg->p1) / 2 + startOffset;
	newRoad->p2 = newRoad->p1 + newRoadL->firstDegreeRot.RotateVector(stepLength);
	newRoad->beginTangent = FRotator(0, left ? 90 : 270, 0).RotateVector(previous->segment->p2 - previous->segment->p1); //->p2 - newRoad->p1;
	newRoad->beginTangent.Normalize();
	newRoad->width = width;
	newRoad->type = newType;
	newRoad->endTangent = newRoad->p2 - newRoad->p1;

	newRoadL->segment = newRoad;
	// every side track has less priority

	newRoadL->time = previous->segment->type != RoadType::main ? previous->time: (previous->time + 1);
	newRoadL->roadLength = (previous->segment->type == RoadType::main && newType != RoadType::main) ? 1 : previous->roadLength+1;
	newRoadL->previous = previous;

	addVertices(newRoad);
	queue.push(newRoadL);
	allsegments.push_back(newRoadL);

}

void ASpawner::addExtensions(std::priority_queue<logicRoadSegment*, std::deque<logicRoadSegment*>, roadComparator> &queue, logicRoadSegment* current, std::vector<logicRoadSegment*> &allsegments) {
	FVector tangent = current->segment->p2 - current->segment->p1;
	if (current->segment->type == RoadType::main) {
		// on the main road
		if (current->roadLength < maxMainRoadLength)
			addRoadForward(queue, current, allsegments);

		if (randFloat() < mainRoadBranchChance) {
			if (randFloat() < 0.15f) {
				addRoadSide(queue, current, true, 3.0f, allsegments, RoadType::main);
			}

			else if (randFloat() < 0.15f) {
				addRoadSide(queue, current, false, 3.0f, allsegments, RoadType::main);
			}
			else {
				if (randFloat() < secondaryRoadBranchChance) {
					addRoadSide(queue, current, true, 2.0f, allsegments, RoadType::secondary);
				}
				if (randFloat() < secondaryRoadBranchChance) {
					addRoadSide(queue, current, false, 2.0f, allsegments, RoadType::secondary);

				}
			}
		}
	}

	else if (current->segment->type == RoadType::secondary) {
		// side road
		if (current->roadLength < maxSecondaryRoadLength) {
			addRoadForward(queue, current, allsegments);

			addRoadSide(queue, current, true, 2.0f, allsegments, RoadType::secondary);
			addRoadSide(queue, current, false, 2.0f, allsegments, RoadType::secondary);
		}
	}


}

TArray<FRoadSegment> ASpawner::determineRoadSegments()
{
	FVector origin;


	TArray<FRoadSegment*> determinedSegments;
	TArray<FRoadSegment> finishedSegments;


	std::priority_queue<logicRoadSegment*, std::deque<logicRoadSegment*>, roadComparator> queue;


	std::vector<logicRoadSegment*> allSegments;
	logicRoadSegment* start = new logicRoadSegment();
	start->time = 0;
	FRoadSegment* startR = new FRoadSegment();
	startR->beginTangent = primaryStepLength;

	startR->p1 = FVector(0, 0, 0);
	startR->p2 = startR->p1 + primaryStepLength;
	startR->width = 3.0f;
	startR->type = RoadType::main;
	startR->endTangent = startR->p2 - startR->p1;

	start->segment = startR;
	start->firstDegreeRot = FRotator(0, 0, 0);
	start->secondDegreeRot = FRotator(0, 0, 0);
	start->roadLength = 1;
	addVertices(startR);

	queue.push(start);

	// map for faster comparisons
	TMap <int, TArray<FRoadSegment*>*> segmentsOrganized;

	// loop for everything else

	while (queue.size() > 0 && determinedSegments.Num() < length) {
		logicRoadSegment* current = queue.top();
		queue.pop();
		if (placementCheck(determinedSegments, current, segmentsOrganized)) {
			determinedSegments.Add(current->segment);
			if (current->previous && FVector::Dist(current->previous->segment->p2, current->segment->p1) < 100)
				current->previous->segment->roadInFront = true;
			addRoadToMap(segmentsOrganized, current->segment, primaryStepLength.Size() / 2);

			//UE_LOG(LogTemp, Warning, TEXT("CURRENT SEGMENT START X %f"), current->segment->start.X);
			addExtensions(queue, current, allSegments);
		}
	}



	// make sure roads attach properly if there is another road not too far in front of them
	for (FRoadSegment* f2 : determinedSegments) {
		if (f2->roadInFront)
			continue;
		FVector tangent = f2->p2 - f2->p1;
		tangent.Normalize();
		f2->p2 += tangent*maxAttachDistance;
		float closestDist = 100000.0f;
		FRoadSegment* closest = nullptr;
		FVector impactP;
		addVertices(f2);
		bool foundCollision = false;
		for (FRoadSegment* f : determinedSegments) {
			if (f == f2 || FVector::Dist(f->p2, f2->p1) < 400)
				continue;
			FVector res = intersection(f2->p1, f2->p2, f->p1, f->p2);
			if (res.X != 0.0f && FVector::Dist(f2->p2, res) < closestDist) {
				closestDist = FVector::Dist(f2->p2, res);
				closest = f;
				impactP = res;
				foundCollision = true;
				//break;
			}


		}
		if (foundCollision) {
			FVector tangent2 = impactP - f2->p1;
			tangent2.Normalize();
			float len = FVector::Dist(impactP, f2->p1);
			f2->p2 = f2->p1 + (len - standardWidth / 2) * tangent2;

			FVector naturalTangent = f2->p2 - f2->p1;
			naturalTangent.Normalize();
			FVector pot1 = FRotator(0, 90, 0).RotateVector(closest->p2 - closest->p1);
			FVector pot2 = FRotator(0, 270, 0).RotateVector(closest->p2 - closest->p1);
			f2->endTangent = FVector::DistSquared(naturalTangent, pot1) < FVector::DistSquared(naturalTangent, pot2) ? pot1 : pot2;
			addVertices(f2);
			f2->roadInFront = true;
		}
		else {
			f2->p2 -= tangent*maxAttachDistance;
			addVertices(f2);
		}


	}


	TArray<int> keys;
	segmentsOrganized.GetKeys(keys);
	UE_LOG(LogTemp, Warning, TEXT("deleting %i keys in map"), keys.Num());
	for (int key : keys) {
		delete(segmentsOrganized[key]);
	}

	for (FRoadSegment* f : determinedSegments) {
		finishedSegments.Add(*f);
	}

	for (logicRoadSegment* s : allSegments){
		delete(s->segment);
		delete(s);
	}

	return finishedSegments;
}

TArray<FPolygon> ASpawner::roadsToPolygons(TArray<FRoadSegment> segments)
{
	TArray<FPolygon> polygons;
	for (FRoadSegment f : segments) {
		FPolygon p;
		p.points.Add(f.v1);
		p.points.Add(f.v2);
		p.points.Add(f.v3);
		p.points.Add(f.v4);
		polygons.Add(p);
	}
	return polygons;
}




TArray<FMetaPolygon> ASpawner::getSurroundingPolygons(TArray<FLine> segments)
{
	return BaseLibrary::getSurroundingPolygons(segments, segments, standardWidth, 500, 500, 200, 100);
}

// Called when the game starts or when spawned
void ASpawner::BeginPlay()
{
	Super::BeginPlay();
	
}

// Called every frame
void ASpawner::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

}
