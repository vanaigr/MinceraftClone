#pragma once

#include"Chunk.h"
#include"Units.h"
#include"Position.h"
#include"Vector.h"

void updateCollision(chunk::Chunks &chunks, pos::Fractional &origin, pFrac const offsetMin, pFrac const offsetMax, vec3d &force, bool &isOnGround);