#pragma once

#include "../client/renderSDL/RenderHandler.h"

class RenderHandlerExt : public RenderHandler
{
public:
	RenderHandlerExt();

	int getScalingFactor() const override;
};
