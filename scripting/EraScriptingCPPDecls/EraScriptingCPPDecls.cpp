﻿#include "pch.h"
#include "framework.h"
#include "EraScriptingCPPDecls.h"
#include <cstdint>

enative_scripting_builder* enative_scripting_factory::builder;

ERASCRIPTINGCPPDECLS_API enative_scripting_builder* createNativeScriptingBuilder(void)
{
    return enative_scripting_factory::builder;
}

ERASCRIPTINGCPPDECLS_API void addForce(uint32_t id, uint32_t mode, float* force)
{
    (*((enative_scripting_builder::func_obj<void, uint32_t, uint32_t, float*>*)enative_scripting_factory::builder->functions["addForce"].obj))(id, mode, force);
}

enative_scripting_builder::enative_scripting_builder()
{
}

enative_scripting_factory::enative_scripting_factory()
{
    if (!builder)
        builder = new enative_scripting_builder();
}

enative_scripting_factory::~enative_scripting_factory()
{
}