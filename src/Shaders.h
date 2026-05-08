#pragma once

#include "ShaderManager.h"

struct ShaderSource
{
  const char* vertexName;
  const char* fragmentName;
  const char* vertex;
  const char* fragment;
};

const ShaderSource* shaderSourceFor(ShaderManager::ShaderType type);
