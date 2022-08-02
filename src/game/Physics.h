#pragma once

#include"Chunk.h"
#include"Position.h"
#include"Vector.h"

void updateCollision(chunk::Chunks &chunks, pFrac &origin, pFrac const offsetMin, pFrac const offsetMax, vec3d &force, bool &isOnGround);